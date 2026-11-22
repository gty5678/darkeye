#pragma once

#include <QJsonObject>
#include <QObject>
#include <QSqlDatabase>
#include <QUrl>

namespace darkeye
{

class CollectorClient;
class ImageFetchService;

struct ActressSyncResult final
{
    bool succeeded = false;
    qint64 actressId = 0;
    QString errorMessage;
    QJsonObject capture;
};

class ActressSyncService final : public QObject
{
    Q_OBJECT

public:
    explicit ActressSyncService(
        QSqlDatabase database,
        QUrl endpoint = QUrl(QStringLiteral("http://127.0.0.1:56790/api/v1/actress")),
        QString actressImageDirectory = {},
        QUrl imageFetchEndpoint = QUrl(QStringLiteral("http://127.0.0.1:56790/api/v1/image")),
        QObject *parent = nullptr);

    bool start(qint64 actressId);
    /// Fetch a Minnano capture for an editor without modifying the database.
    bool fetchCapture(qint64 actressId);
    bool cancel();
    [[nodiscard]] bool isBusy() const noexcept;

signals:
    void actressesChanged();
    void finished(const darkeye::ActressSyncResult &result);

private:
    bool startFetch(qint64 actressId, bool persistCapture);
    bool persist(qint64 actressId, const QJsonObject &data, QString *errorMessage);
    bool downloadAvatar(qint64 actressId, const QJsonObject &data);
    void finish(const ActressSyncResult &result);

    QSqlDatabase m_database;
    CollectorClient *m_collector = nullptr;
    ImageFetchService *m_imageFetch = nullptr;
    quint64 m_requestId = 0;
    quint64 m_avatarRequestId = 0;
    qint64 m_actressId = 0;
    bool m_persistCapture = true;
    QString m_actressImageDirectory;
    QString m_avatarFileName;
    ActressSyncResult m_pendingResult;
};

} // namespace darkeye

Q_DECLARE_METATYPE(darkeye::ActressSyncResult)
