#include "database/WebDavBackupService.h"

#include "database/DatabaseMaintenanceService.h"
#include "database/WebDavCredentialStore.h"

#include <QAuthenticator>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTemporaryDir>
#include <QTimer>
#include <QUrlQuery>
#include <QXmlStreamReader>

namespace darkeye
{
namespace
{

WebDavActionResult failure(const QString &message) { return {.succeeded = false, .message = message}; }

QString normalizedRoot(const QString &value)
{
    QString path = value.trimmed().replace(QLatin1Char('\\'), QLatin1Char('/'));
    while (path.startsWith(QLatin1Char('/'))) path.removeFirst();
    while (path.endsWith(QLatin1Char('/'))) path.chop(1);
    return QLatin1Char('/') + path + QLatin1Char('/');
}

QUrl remoteUrl(const CrawlerSettings::WebDav &settings, const QString &remotePath)
{
    QUrl url = settings.baseUrl;
    QString basePath = url.path();
    if (!basePath.endsWith(QLatin1Char('/'))) basePath += QLatin1Char('/');
    QString path = remotePath;
    if (!path.startsWith(QLatin1Char('/'))) path = normalizedRoot(settings.remoteRoot) + path;
    url.setPath(basePath + path.mid(1));
    return url;
}

QString errorCode(QNetworkReply *reply)
{
    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (reply->property("webDavTimedOut").toBool() || reply->error() == QNetworkReply::TimeoutError)
        return QStringLiteral("network_error: WebDAV 请求超时。");
    if (reply->error() != QNetworkReply::NoError && status < 400)
        return QStringLiteral("network_error: %1").arg(reply->errorString());
    switch (status)
    {
    case 401: return QStringLiteral("auth_failed");
    case 403: return QStringLiteral("permission_denied");
    case 404: return QStringLiteral("not_found");
    case 409: return QStringLiteral("conflict");
    case 423: return QStringLiteral("locked");
    case 507: return QStringLiteral("insufficient_storage");
    default: return status > 0 ? QStringLiteral("http_%1").arg(status) : reply->errorString();
    }
}

QNetworkReply *send(QNetworkAccessManager &manager, QNetworkRequest request, const QByteArray &verb,
                    QIODevice *body = nullptr)
{
    return manager.sendCustomRequest(request, verb, body);
}

void waitFor(QNetworkReply *reply)
{
    QEventLoop loop;
    QTimer timer;
    timer.setSingleShot(true);
    timer.setTimerType(Qt::PreciseTimer);
    timer.setInterval(reply->request().transferTimeout());
    QObject::connect(&timer, &QTimer::timeout, &loop, [reply] {
        reply->setProperty("webDavTimedOut", true);
        reply->abort();
    });
    // Match Python's socket inactivity timeout rather than limiting the total
    // duration of a backup that is still making progress.
    QObject::connect(reply, &QNetworkReply::readyRead, &timer, [&timer] { timer.start(); });
    QObject::connect(reply, &QNetworkReply::uploadProgress, &timer,
                     [&timer](qint64 sent, qint64) { if (sent > 0) timer.start(); });
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    if (!reply->isFinished())
    {
        timer.start();
        loop.exec();
    }
}

class Client final
{
public:
    explicit Client(const CrawlerSettings::WebDav &settings) : m_settings(settings)
    {
        m_credentials = WebDavCredentialStore::load(settings.profileName);
        if (m_credentials)
        {
            QObject::connect(&m_manager, &QNetworkAccessManager::authenticationRequired,
                             [this](QNetworkReply *, QAuthenticator *authenticator) {
                                 authenticator->setUser(m_credentials->username);
                                 authenticator->setPassword(m_credentials->password);
                             });
        }
    }

    [[nodiscard]] bool valid(QString *message) const
    {
        if (!m_settings.enabled) { *message = QStringLiteral("WebDAV 云备份未启用。"); return false; }
        if (!m_settings.baseUrl.isValid() || m_settings.baseUrl.scheme().isEmpty())
        { *message = QStringLiteral("WebDAV Base URL 无效。"); return false; }
        if (!m_credentials) { *message = QStringLiteral("未找到 WebDAV 凭据，请先保存凭据。"); return false; }
        return true;
    }

    WebDavActionResult propfind(const QString &path, const QString &depth, QByteArray *content = nullptr)
    {
        QNetworkRequest request = requestFor(path);
        request.setRawHeader("Depth", depth.toLatin1());
        QNetworkReply *reply = send(m_manager, request, "PROPFIND");
        waitFor(reply);
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (content) *content = reply->readAll();
        const QString code = errorCode(reply);
        const bool succeeded = reply->error() == QNetworkReply::NoError && (status == 200 || status == 207);
        reply->deleteLater();
        return succeeded ? WebDavActionResult{true} : failure(code);
    }

    WebDavActionResult ensureDirectory(const QString &directory)
    {
        QString current;
        for (const QString &part : normalizedRoot(directory).split(QLatin1Char('/'), Qt::SkipEmptyParts))
        {
            current += QLatin1Char('/') + part;
            const auto found = propfind(current, QStringLiteral("0"));
            if (found.succeeded) continue;
            if (found.message.startsWith(QStringLiteral("network_error:"))) return found;
            QNetworkRequest request = requestFor(current);
            QNetworkReply *reply = send(m_manager, request, "MKCOL");
            waitFor(reply);
            const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            const QString code = errorCode(reply);
            reply->deleteLater();
            if (code.startsWith(QStringLiteral("network_error:")) ||
                (status != 201 && status != 301 && status != 405)) return failure(code);
        }
        return {.succeeded = true};
    }

    WebDavActionResult upload(const QString &localPath, const QString &path)
    {
        QFile file(localPath);
        if (!file.open(QIODevice::ReadOnly)) return failure(QStringLiteral("无法读取本地备份文件：%1").arg(localPath));
        const QString parent = QFileInfo(path).path();
        const auto ready = ensureDirectory(parent);
        if (!ready.succeeded) return ready;
        QNetworkReply *reply = m_manager.put(requestFor(path), &file);
        waitFor(reply);
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QString code = errorCode(reply);
        const bool succeeded = reply->error() == QNetworkReply::NoError &&
                               (status == 200 || status == 201 || status == 204);
        reply->deleteLater();
        return succeeded ? WebDavActionResult{true} : failure(code);
    }

    WebDavActionResult download(const QString &path, const QString &localPath)
    {
        QNetworkReply *reply = m_manager.get(requestFor(path));
        waitFor(reply);
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (reply->error() != QNetworkReply::NoError || status != 200)
        { const auto result = failure(errorCode(reply)); reply->deleteLater(); return result; }
        QDir().mkpath(QFileInfo(localPath).absolutePath());
        QFile file(localPath);
        if (!file.open(QIODevice::WriteOnly)) { reply->deleteLater(); return failure(QStringLiteral("无法写入下载文件。")); }
        file.write(reply->readAll());
        reply->deleteLater();
        return {.succeeded = true};
    }

private:
    QNetworkRequest requestFor(const QString &path) const
    {
        QNetworkRequest request(remoteUrl(m_settings, path));
        request.setTransferTimeout(qBound(3, m_settings.timeoutSeconds, 300) * 1000);
        return request;
    }

    CrawlerSettings::WebDav m_settings;
    std::optional<WebDavCredentials> m_credentials;
    QNetworkAccessManager m_manager;
};

QStringList hrefs(const QByteArray &xml)
{
    QStringList result;
    QXmlStreamReader reader(xml);
    while (!reader.atEnd())
    {
        reader.readNext();
        if (reader.isStartElement() && reader.name() == u"href") result.append(reader.readElementText().trimmed());
    }
    return result;
}

} // namespace

WebDavActionResult WebDavBackupService::testConnection(const CrawlerSettings::WebDav &settings)
{
    Client client(settings); QString message;
    if (!client.valid(&message)) return failure(message);
    const auto tested = client.propfind(QStringLiteral("/"), QStringLiteral("0"));
    if (!tested.succeeded && tested.message != QStringLiteral("not_found")) return failure(QStringLiteral("WebDAV 连接失败：%1").arg(tested.message));
    const auto ready = client.ensureDirectory(settings.remoteRoot);
    return ready.succeeded ? WebDavActionResult{true, QStringLiteral("WebDAV 连接成功，远端根目录已就绪：%1").arg(settings.remoteRoot)}
                           : failure(QStringLiteral("WebDAV 连接成功，但根目录创建失败：%1").arg(ready.message));
}

WebDavActionResult WebDavBackupService::uploadDatabaseBackup(QSqlDatabase database, const QString &backupDirectory,
                                                              const QString &prefix, const CrawlerSettings::WebDav &settings)
{
    const auto backup = DatabaseMaintenanceService::createBackup(database, backupDirectory, prefix);
    if (!backup.succeeded) return {.message = backup.message};
    if (!settings.autoUploadOnBackup) return {.succeeded = true, .message = backup.message, .localPath = backup.outputPath};
    return uploadFile(backup.outputPath, settings);
}

WebDavActionResult WebDavBackupService::uploadPublicSnapshot(
    QSqlDatabase database, const QString &snapshotRoot, const QString &workCoversDirectory,
    const QString &fanartDirectory, const QString &actressImagesDirectory,
    const QString &actorImagesDirectory, const CrawlerSettings::WebDav &settings)
{
    const auto snapshot = DatabaseMaintenanceService::createPublicSnapshot(
        database, snapshotRoot, workCoversDirectory, fanartDirectory,
        actressImagesDirectory, actorImagesDirectory);
    if (!snapshot.succeeded) return {.message = snapshot.message, .localPath = snapshot.outputPath};
    const auto archive = DatabaseMaintenanceService::compressPublicSnapshot(snapshot.outputPath);
    if (!archive.succeeded) return {.message = archive.message, .localPath = snapshot.outputPath};
    if (!settings.autoUploadOnBackup)
        return {.succeeded = true, .message = archive.message, .localPath = archive.outputPath};
    auto uploaded = uploadFile(archive.outputPath, settings);
    if (!uploaded.succeeded)
    {
        uploaded.localPath = archive.outputPath;
        uploaded.message = QStringLiteral("本地完整快照及 ZIP 已保存，云端上传失败：%1").arg(uploaded.message);
    }
    return uploaded;
}

WebDavActionResult WebDavBackupService::uploadFile(const QString &localPath, const CrawlerSettings::WebDav &settings)
{
    Client client(settings); QString message;
    if (!client.valid(&message)) return failure(message);
    const QString remotePath = normalizedRoot(settings.remoteRoot) + QFileInfo(localPath).fileName();
    const auto uploaded = client.upload(localPath, remotePath);
    if (!uploaded.succeeded) return {.message = QStringLiteral("上传失败：%1").arg(uploaded.message), .localPath = localPath, .remotePath = remotePath};
    return {.succeeded = true, .message = QStringLiteral("本地备份成功并已上传云端。"), .localPath = localPath, .remotePath = remotePath};
}

WebDavActionResult WebDavBackupService::listBackups(const CrawlerSettings::WebDav &settings, QStringList *files)
{
    if (files) files->clear();
    Client client(settings); QString message;
    if (!client.valid(&message)) return failure(message);
    QByteArray xml; const auto listed = client.propfind(settings.remoteRoot, QStringLiteral("1"), &xml);
    if (!listed.succeeded) return failure(QStringLiteral("列举备份失败：%1").arg(listed.message));
    const QString root = normalizedRoot(settings.remoteRoot);
    QStringList result;
    for (QString href : hrefs(xml))
    {
        href = QUrl::fromPercentEncoding(href.toUtf8());
        const int index = href.indexOf(root);
        if (index < 0) continue;
        href = href.mid(index);
        const QString relative = href.mid(root.size()).trimmed();
        if (!relative.isEmpty() && !relative.contains(QLatin1Char('/')) && !relative.endsWith(QStringLiteral(".meta.json"))) result.append(root + relative);
    }
    result.removeDuplicates(); result.sort();
    if (files) *files = result;
    return {.succeeded = true, .message = QStringLiteral("列举成功，共 %1 条。").arg(result.size())};
}

WebDavActionResult WebDavBackupService::restoreDatabaseBackup(QSqlDatabase database, const QString &remotePath,
                                                               const QString &temporaryDirectory, const CrawlerSettings::WebDav &settings)
{
    Client client(settings); QString message;
    if (!client.valid(&message)) return failure(message);
    const QString localPath = QDir(temporaryDirectory).filePath(QFileInfo(remotePath).fileName());
    const auto downloaded = client.download(remotePath, localPath);
    if (!downloaded.succeeded) return failure(QStringLiteral("下载备份失败：%1").arg(downloaded.message));
    const auto restored = DatabaseMaintenanceService::restoreBackup(database, localPath);
    return {.succeeded = restored.succeeded, .message = restored.succeeded ? QStringLiteral("恢复成功。") : restored.message, .localPath = localPath, .remotePath = remotePath};
}

} // namespace darkeye
