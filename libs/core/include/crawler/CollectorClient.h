#pragma once

#include <QJsonObject>
#include <QObject>
#include <QUrl>

namespace darkeye
{

enum class CollectorRequestKind
{
    Work,
    Actress,
    TopActresses,
};

class CollectorClient final : public QObject
{
    Q_OBJECT

public:
    explicit CollectorClient(
        QUrl workApiBaseUrl = QUrl(QStringLiteral("http://127.0.0.1:56790/api/v1/work")),
        QUrl actressApiBaseUrl = QUrl(QStringLiteral("http://127.0.0.1:56790/api/v1/actress")),
        QUrl topActressesApiUrl =
            QUrl(QStringLiteral("http://127.0.0.1:56790/api/v1/top-actresses")),
        QObject *parent = nullptr);

    [[nodiscard]] quint64 fetchWork(const QString &serialNumber);
    [[nodiscard]] quint64 fetchActress(const QString &japaneseName, const QString &minnanoUrl = {});
    [[nodiscard]] quint64 fetchTopActresses();
    bool cancel(quint64 requestId);
    void cancelAll();
    void setTimeoutMilliseconds(int timeoutMilliseconds);
    [[nodiscard]] int timeoutMilliseconds() const noexcept;
    [[nodiscard]] bool isBusy() const noexcept;

signals:
    void requestStarted(quint64 requestId, darkeye::CollectorRequestKind kind);
    void requestFinished(quint64 requestId, darkeye::CollectorRequestKind kind, bool succeeded,
                         const QJsonObject &payload, const QString &errorMessage, int httpStatus);

private:
    [[nodiscard]] quint64 startGet(CollectorRequestKind kind, const QUrl &url);
    class Private;
    Private *d = nullptr;
};

} // namespace darkeye

Q_DECLARE_METATYPE(darkeye::CollectorRequestKind)
