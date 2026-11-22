#include "services/ActressSyncService.h"

#include "crawler/CollectorClient.h"
#include "database/Transaction.h"
#include "services/ImageFetchService.h"

#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QSqlError>
#include <QSqlQuery>

namespace darkeye
{

namespace
{

int number(const QJsonObject &data, const QString &key)
{
    bool ok = false;
    const int value = data.value(key).toVariant().toInt(&ok);
    return ok ? value : 0;
}

QString text(const QJsonObject &data, const QString &key)
{
    return data.value(key).toString().trimmed();
}

QJsonObject captureData(const QJsonObject &payload)
{
    const QJsonObject nested = payload.value(QStringLiteral("data")).toObject();
    return nested.isEmpty() ? payload : nested;
}

} // namespace

ActressSyncService::ActressSyncService(QSqlDatabase database, QUrl endpoint,
                                       QString actressImageDirectory, QUrl imageFetchEndpoint,
                                       QObject *parent)
    : QObject(parent), m_database(std::move(database)),
      m_actressImageDirectory(QDir::cleanPath(std::move(actressImageDirectory)))
{
    qRegisterMetaType<ActressSyncResult>();
    m_collector = new CollectorClient({}, std::move(endpoint), {}, this);
    if (!m_actressImageDirectory.isEmpty())
    {
        m_imageFetch = new ImageFetchService(std::move(imageFetchEndpoint), this);
        connect(m_imageFetch, &ImageFetchService::requestFinished, this,
                [this](quint64 requestId, bool succeeded, const QString &, const QString &errorMessage)
        {
            if (requestId != m_avatarRequestId)
                return;
            m_avatarRequestId = 0;
            ActressSyncResult result = m_pendingResult;
            if (succeeded)
            {
                QSqlQuery image(m_database);
                image.prepare(QStringLiteral("UPDATE actress SET image_urlA=?,"
                                             "update_time=datetime('now','localtime') WHERE actress_id=?"));
                image.addBindValue(m_avatarFileName);
                image.addBindValue(result.actressId);
                if (!image.exec() || image.numRowsAffected() != 1)
                    result.errorMessage = image.lastError().text().isEmpty()
                        ? QStringLiteral("头像已下载，但写入数据库失败") : image.lastError().text();
            }
            else
            {
                // Keep Python's batch contract: metadata remains persisted when an avatar fails.
                result.errorMessage = QStringLiteral("头像下载失败：%1").arg(errorMessage);
            }
            finish(result);
        });
    }
    connect(m_collector, &CollectorClient::requestFinished, this,
            [this](quint64 requestId, CollectorRequestKind kind, bool succeeded,
                   const QJsonObject &payload, const QString &requestError, int)
            {
                if (requestId != m_requestId || kind != CollectorRequestKind::Actress)
                    return;
                m_requestId = 0;
                ActressSyncResult result;
                result.actressId = m_actressId;
                if (!succeeded)
                    result.errorMessage = requestError;
                else if (!payload.value(QStringLiteral("ok")).toBool())
                    result.errorMessage = payload.value(QStringLiteral("error"))
                                              .toString(QStringLiteral("女优采集失败"));
                else if (m_persistCapture &&
                         !persist(m_actressId, captureData(payload), &result.errorMessage))
                {
                }
                else
                {
                    result.succeeded = true;
                    result.capture = captureData(payload);
                }
                if (result.succeeded && m_persistCapture && downloadAvatar(result.actressId, result.capture))
                {
                    m_pendingResult = result;
                    return;
                }
                finish(result);
            });
}

void ActressSyncService::finish(const ActressSyncResult &result)
{
    const bool changed = result.succeeded && m_persistCapture;
    m_requestId = 0;
    m_actressId = 0;
    m_persistCapture = true;
    m_avatarFileName.clear();
    if (changed)
        emit actressesChanged();
    emit finished(result);
}

bool ActressSyncService::start(qint64 actressId)
{
    return startFetch(actressId, true);
}

bool ActressSyncService::fetchCapture(qint64 actressId)
{
    return startFetch(actressId, false);
}

bool ActressSyncService::startFetch(qint64 actressId, bool persistCapture)
{
    if (isBusy() || actressId <= 0)
        return false;

    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "SELECT an.jp, a.minnano_url FROM actress a "
        "JOIN actress_name an ON an.actress_id=a.actress_id "
        "WHERE a.actress_id=? ORDER BY an.name_type DESC, an.actress_name_id LIMIT 1"));
    query.addBindValue(actressId);
    if (!query.exec() || !query.next() || query.value(0).toString().trimmed().isEmpty())
        return false;

    m_actressId = actressId;
    m_persistCapture = persistCapture;
    m_requestId = m_collector->fetchActress(query.value(0).toString(), query.value(1).toString());
    return true;
}

bool ActressSyncService::persist(qint64 actressId, const QJsonObject &data, QString *errorMessage)
{
    if (data.isEmpty())
    {
        if (errorMessage != nullptr)
            *errorMessage = QStringLiteral("采集结果为空");
        return false;
    }

    Transaction transaction(m_database);
    if (!transaction.isActive())
    {
        if (errorMessage != nullptr)
            *errorMessage = transaction.errorString();
        return false;
    }

    QSqlQuery entity(m_database);
    entity.prepare(QStringLiteral(
        "UPDATE actress SET birthday=?,height=?,bust=?,waist=?,hip=?,cup=?,debut_date=?,"
        "need_update=0,minnano_url=CASE WHEN ?='' THEN minnano_url ELSE ? END,"
        "update_time=datetime('now','localtime') WHERE actress_id=?"));
    entity.addBindValue(text(data, QStringLiteral("出生日期")));
    entity.addBindValue(number(data, QStringLiteral("身高")));
    entity.addBindValue(number(data, QStringLiteral("胸围")));
    entity.addBindValue(number(data, QStringLiteral("腰围")));
    entity.addBindValue(number(data, QStringLiteral("臀围")));
    entity.addBindValue(text(data, QStringLiteral("罩杯")));
    entity.addBindValue(text(data, QStringLiteral("出道日期")));
    const QString minnanoId = text(data, QStringLiteral("minnano_actress_id"));
    entity.addBindValue(minnanoId);
    entity.addBindValue(minnanoId);
    entity.addBindValue(actressId);
    if (!entity.exec() || entity.numRowsAffected() != 1)
    {
        if (errorMessage != nullptr)
            *errorMessage = entity.lastError().text().isEmpty() ? QStringLiteral("女优不存在")
                                                                : entity.lastError().text();
        return false;
    }

    QSqlQuery primary(m_database);
    primary.prepare(QStringLiteral(
        "SELECT actress_name_id,cn,jp FROM actress_name WHERE actress_id=? "
        "ORDER BY name_type DESC, actress_name_id LIMIT 1"));
    primary.addBindValue(actressId);
    if (!primary.exec() || !primary.next())
    {
        if (errorMessage != nullptr)
            *errorMessage = primary.lastError().text().isEmpty() ? QStringLiteral("女优缺少姓名")
                                                                 : primary.lastError().text();
        return false;
    }

    const qint64 headId = primary.value(0).toLongLong();
    const QString japanese = text(data, QStringLiteral("日文名"));
    QSqlQuery updateHead(m_database);
    updateHead.prepare(QStringLiteral(
        "UPDATE actress_name SET jp=?,en=?,kana=? WHERE actress_name_id=?"));
    updateHead.addBindValue(japanese.isEmpty() ? primary.value(2).toString() : japanese);
    updateHead.addBindValue(text(data, QStringLiteral("英文名")));
    updateHead.addBindValue(text(data, QStringLiteral("假名")));
    updateHead.addBindValue(headId);
    if (!updateHead.exec())
    {
        if (errorMessage != nullptr)
            *errorMessage = updateHead.lastError().text();
        return false;
    }

    QSqlQuery unlink(m_database);
    unlink.prepare(QStringLiteral(
        "UPDATE actress_name SET redirect_actress_name_id=NULL WHERE actress_id=?"));
    unlink.addBindValue(actressId);
    if (!unlink.exec())
    {
        if (errorMessage != nullptr)
            *errorMessage = unlink.lastError().text();
        return false;
    }
    QSqlQuery removeAliases(m_database);
    removeAliases.prepare(QStringLiteral(
        "DELETE FROM actress_name WHERE actress_id=? AND actress_name_id!=?"));
    removeAliases.addBindValue(actressId);
    removeAliases.addBindValue(headId);
    if (!removeAliases.exec())
    {
        if (errorMessage != nullptr)
            *errorMessage = removeAliases.lastError().text();
        return false;
    }

    qint64 previousId = headId;
    const QJsonArray aliases = data.value(QStringLiteral("alias_chain")).toArray();
    for (qsizetype index = aliases.size(); index > 0; --index)
    {
        const QJsonObject alias = aliases.at(index - 1).toObject();
        const QString jp = text(alias, QStringLiteral("jp"));
        const QString kana = text(alias, QStringLiteral("kana"));
        const QString en = text(alias, QStringLiteral("en"));
        if (jp.isEmpty() && kana.isEmpty() && en.isEmpty())
            continue;
        QSqlQuery insert(m_database);
        insert.prepare(QStringLiteral(
            "INSERT INTO actress_name(actress_id,name_type,cn,jp,en,kana,"
            "redirect_actress_name_id) VALUES(?,0,'',?,?,?,?)"));
        insert.addBindValue(actressId);
        insert.addBindValue(jp);
        insert.addBindValue(en);
        insert.addBindValue(kana);
        insert.addBindValue(previousId);
        if (!insert.exec())
        {
            if (errorMessage != nullptr)
                *errorMessage = insert.lastError().text();
            return false;
        }
        previousId = insert.lastInsertId().toLongLong();
    }
    if (!transaction.commit())
    {
        if (errorMessage != nullptr)
            *errorMessage = transaction.errorString();
        return false;
    }
    return true;
}

bool ActressSyncService::downloadAvatar(qint64 actressId, const QJsonObject &data)
{
    if (m_imageFetch == nullptr || m_avatarRequestId != 0)
        return false;
    const QUrl source(text(data, QStringLiteral("头像地址")));
    if (!source.isValid() || (source.scheme() != QStringLiteral("http") &&
                              source.scheme() != QStringLiteral("https")))
        return false;
    QString japanese = text(data, QStringLiteral("日文名"));
    if (japanese.isEmpty())
        japanese = QStringLiteral("actress");
    for (const QChar character : QStringLiteral("\\/:*?\"<>|"))
        japanese.replace(character, QChar('_'));
    m_avatarFileName = QStringLiteral("%1-%2.jpg").arg(actressId).arg(japanese);
    m_avatarRequestId = m_imageFetch->fetchToJpeg(
        source, QDir(m_actressImageDirectory).filePath(m_avatarFileName));
    return m_avatarRequestId != 0;
}

bool ActressSyncService::cancel()
{
    if (m_avatarRequestId != 0 && m_imageFetch != nullptr)
        return m_imageFetch->cancel(m_avatarRequestId);
    return m_requestId != 0 && m_collector->cancel(m_requestId);
}

bool ActressSyncService::isBusy() const noexcept
{
    return m_requestId != 0 || m_avatarRequestId != 0;
}

} // namespace darkeye
