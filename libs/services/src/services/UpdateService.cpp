#include "services/UpdateService.h"

#include "settings/Paths.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QSettings>
#include <QTemporaryDir>
#include <QNetworkReply>
#include <QNetworkRequest>

namespace darkeye
{

namespace
{
constexpr auto kUserAgent = "Darkeye-Updater/2.0";
}

QUrl UpdateService::manifestUrl()
{
    QSettings config(QDir(settings::Paths().configDirectory()).filePath(QStringLiteral("update.ini")),
                     QSettings::IniFormat);
    QString address = config.value(QStringLiteral("Update/LatestJsonUrl")).toString().trimmed();
    if (address.isEmpty())
        address = QStringLiteral("https://darkeye.win/updates/stable/{target}/latest.json");
    address.replace(QStringLiteral("{target}"), utils::updateTarget());
    return QUrl(address);
}

bool UpdateService::launchUpdater(QString *error, const QStringList &extraArguments)
{
    const auto fail = [error](const QString &message) {
        if (error) *error = message;
        return false;
    };
    const QDir executableDir(QCoreApplication::applicationDirPath());
#ifdef Q_OS_WIN
    const QString updaterName = QStringLiteral("updater.exe");
#else
    const QString updaterName = QStringLiteral("updater");
#endif
    const QString source = executableDir.filePath(updaterName);
    if (!QFileInfo(source).isExecutable())
        return fail(QStringLiteral("未找到可执行的更新程序：%1").arg(source));
    QString payloadRoot = QStringLiteral(".");
    QString installDir = executableDir.absolutePath();
    QString mainExe = QFileInfo(QCoreApplication::applicationFilePath()).fileName();
#ifdef Q_OS_MACOS
    // QDir(".../MacOS/..").dirName() is "..", not "Contents".
    // Normalize before checking the parent or a bundle is treated as a bin dir.
    const QDir contentsDir(QDir::cleanPath(executableDir.filePath(QStringLiteral(".."))));
    if (executableDir.dirName() == QStringLiteral("MacOS") &&
        contentsDir.dirName() == QStringLiteral("Contents")) {
        installDir = QDir::cleanPath(contentsDir.filePath(QStringLiteral("..")));
        mainExe.prepend(QStringLiteral("Contents/MacOS/"));
        payloadRoot = QStringLiteral("Darkeye.app");
    }
#elif defined(Q_OS_LINUX)
    if (executableDir.dirName() == QStringLiteral("bin")) {
        installDir = QDir::cleanPath(executableDir.filePath(QStringLiteral("..")));
        mainExe.prepend(QStringLiteral("bin/"));
    }
#endif
    if (!QFileInfo(installDir).isWritable())
        return fail(QStringLiteral("程序目录不可写，请将程序移到可写位置或使用系统安装程序更新：%1").arg(installDir));

    QTemporaryDir staging(QDir::tempPath() + QStringLiteral("/darkeye-update-XXXXXX"));
    if (!staging.isValid()) return fail(QStringLiteral("无法创建更新器临时目录。"));
    const QString executable = staging.filePath(updaterName);
    if (!QFile::copy(source, executable) ||
        !QFile::setPermissions(executable, QFile::permissions(source) | QFileDevice::ExeOwner))
        return fail(QStringLiteral("无法复制更新器到临时目录。"));

    // Resolve paths before the application exits; a new working directory must
    // not change the selected profile or custom resource locations.
    const settings::Paths paths;
    const QStringList restartArguments{QStringLiteral("--data-dir"), paths.dataDirectory(),
        QStringLiteral("--config-dir"), paths.configDirectory(),
        QStringLiteral("--avwiki-dir"), paths.wikiDirectory()};
    QStringList quoted;
    for (QString argument : restartArguments) {
#ifdef Q_OS_WIN
        // Windows command-line quoting (including trailing backslashes).
        QString escaped;
        int slashes = 0;
        for (const QChar c : argument) {
            if (c == u'\\') { ++slashes; continue; }
            escaped += QString(slashes * (c == u'"' ? 2 : 1), u'\\');
            slashes = 0;
            if (c == u'"') escaped += u'\\';
            escaped += c;
        }
        escaped += QString(slashes * 2, u'\\');
        argument = escaped;
#else
        // The Unix updater parses std::quoted arguments.
        argument.replace(QStringLiteral("\\"), QStringLiteral("\\\\"));
        argument.replace(QStringLiteral("\""), QStringLiteral("\\\""));
#endif
        quoted.append(u'"' + argument + u'"');
    }
    QStringList arguments{QStringLiteral("--install-dir"), installDir,
        QStringLiteral("--current-version"), QStringLiteral(DARKEYE_VERSION),
        QStringLiteral("--main-exe"), mainExe,
        QStringLiteral("--latest-json-url"), manifestUrl().toString(),
        QStringLiteral("--payload-root"), payloadRoot,
        QStringLiteral("--keep"), QStringLiteral("data,portable.ini"),
        QStringLiteral("--main-args"), quoted.join(u' '),
        QStringLiteral("--pid"), QString::number(QCoreApplication::applicationPid())};
    arguments.append(extraArguments);
    QProcess process;
    process.setProgram(executable);
    process.setArguments(arguments);
    process.setWorkingDirectory(staging.path());
    process.setStandardOutputFile(staging.filePath(QStringLiteral("update.log")));
    process.setStandardErrorFile(staging.filePath(QStringLiteral("update.log")), QIODevice::Append);
    if (!process.startDetached())
        return fail(QStringLiteral("无法启动更新程序：%1").arg(process.errorString()));
    staging.setAutoRemove(false); // The detached process still needs its executable.
    return true;
}

UpdateService::UpdateService(QObject *parent) : QObject(parent)
{
    m_timeout.setSingleShot(true);
    m_retry.setSingleShot(true);
    connect(&m_timeout, &QTimer::timeout, this, [this] {
        if (m_reply) m_reply->abort();
    });
    connect(&m_retry, &QTimer::timeout, this, &UpdateService::requestManifest);
}

void UpdateService::check(const QUrl &manifestUrl, const QString &localVersion,
                          int timeoutMilliseconds, int maximumAttempts)
{
    cancel();
    m_manifestUrl = manifestUrl;
    m_localVersion = localVersion;
    m_timeoutMilliseconds = qMax(1, timeoutMilliseconds);
    m_maximumAttempts = qMax(1, maximumAttempts);
    m_attempt = 0;
    m_active = true;
    if (!m_manifestUrl.isValid() || m_manifestUrl.scheme().isEmpty()) {
        completeFailure(QStringLiteral("更新地址无效：%1").arg(m_manifestUrl.toString()));
        return;
    }
    requestManifest();
}

void UpdateService::cancel()
{
    m_active = false;
    m_timeout.stop();
    m_retry.stop();
    if (m_reply) m_reply->abort();
    m_reply = nullptr;
}

void UpdateService::requestManifest()
{
    if (!m_active) return;
    ++m_attempt;
    QNetworkRequest request(m_manifestUrl);
    request.setRawHeader("User-Agent", kUserAgent);
    request.setRawHeader("Accept", "application/json, text/plain, */*");
    m_reply = m_network.get(request);
    QNetworkReply *reply = m_reply;
    m_timeout.start(m_timeoutMilliseconds);
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        if (!m_active || reply != m_reply) {
            reply->deleteLater();
            return;
        }
        m_timeout.stop();
        m_reply = nullptr;
        const auto error = reply->error();
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QByteArray payload = reply->readAll();
        const QString errorText = reply->errorString();
        reply->deleteLater();
        if (error != QNetworkReply::NoError) {
            // HTTP responses are definitive; transport failures may be transient.
            retryOrFail(errorText, status < 400 || status >= 600);
            return;
        }
        m_active = false;
        emit finished(utils::evaluateUpdateManifest(m_localVersion, payload));
    });
}

void UpdateService::retryOrFail(const QString &message, bool retryable)
{
    if (retryable && m_attempt < m_maximumAttempts) {
        m_retry.start(350 * (1 << (m_attempt - 1)));
        return;
    }
    completeFailure(message);
}

void UpdateService::completeFailure(const QString &message)
{
    if (!m_active) return;
    m_active = false;
    emit finished({false, QStringLiteral("更新检查失败"),
                   QStringLiteral("无法获取最新版本信息：%1").arg(message)});
}

} // namespace darkeye
