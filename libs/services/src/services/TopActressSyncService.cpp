#include "services/TopActressSyncService.h"

#include "crawler/CollectorClient.h"

#include <QJsonArray>

namespace darkeye
{

QString TopActressSyncResult::summary() const
{
    if (!succeeded)
        return errorMessage;
    return QStringLiteral("热门女优同步完成：收到 %1，新增 %2，已存在 %3，失败 %4")
        .arg(received)
        .arg(inserted)
        .arg(existing)
        .arg(failed);
}

TopActressSyncService::TopActressSyncService(QSqlDatabase database, QUrl endpoint, QObject *parent)
    : QObject(parent), m_people(std::move(database))
{
    qRegisterMetaType<TopActressSyncResult>();
    m_collector = new CollectorClient({}, {}, std::move(endpoint), this);
    connect(m_collector, &CollectorClient::requestFinished, this,
            [this](quint64 requestId, CollectorRequestKind kind, bool requestSucceeded,
                   const QJsonObject &payload, const QString &requestError, int)
            {
                if (requestId != m_requestId || kind != CollectorRequestKind::TopActresses)
                    return;
                m_requestId = 0;
                TopActressSyncResult result;
                if (!requestSucceeded)
                {
                    result.errorMessage = requestError;
                    emit finished(result);
                    return;
                }
                if (!payload.value(QStringLiteral("ok")).toBool())
                {
                    result.errorMessage = payload.value(QStringLiteral("error"))
                                              .toString(QStringLiteral("热门女优采集失败"));
                    emit finished(result);
                    return;
                }
                const QJsonArray names = payload.value(QStringLiteral("names")).toArray();
                if (names.isEmpty())
                {
                    result.errorMessage = QStringLiteral("采集成功但未返回女优名称");
                    emit finished(result);
                    return;
                }

                const int limit = qMin(50, names.size());
                result.received = limit;
                for (int index = 0; index < limit; ++index)
                {
                    if (!names.at(index).isString())
                    {
                        ++result.failed;
                        continue;
                    }
                    QString name = names.at(index).toString().trimmed();
                    name.replace(QStringLiteral("卜"), QStringLiteral("ト"));
                    if (name.isEmpty())
                    {
                        ++result.failed;
                        continue;
                    }
                    QString errorMessage;
                    const std::optional<qint64> existing =
                        m_people.findByName(PersonKind::Actress, name, &errorMessage);
                    if (!errorMessage.isEmpty())
                    {
                        ++result.failed;
                        if (result.errorMessage.isEmpty())
                            result.errorMessage = errorMessage;
                        continue;
                    }
                    if (existing.has_value())
                    {
                        ++result.existing;
                        continue;
                    }
                    if (m_people.create(PersonKind::Actress, name, name, &errorMessage).has_value())
                        ++result.inserted;
                    else
                    {
                        ++result.failed;
                        if (result.errorMessage.isEmpty())
                            result.errorMessage = errorMessage;
                    }
                }
                result.succeeded = true;
                emit finished(result);
            });
}

bool TopActressSyncService::start()
{
    if (isBusy())
        return false;
    m_requestId = m_collector->fetchTopActresses();
    return true;
}

bool TopActressSyncService::cancel()
{
    return isBusy() && m_collector->cancel(m_requestId);
}

bool TopActressSyncService::isBusy() const noexcept
{
    return m_requestId != 0;
}

} // namespace darkeye
