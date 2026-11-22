#pragma once

#include <QObject>
#include <QUrl>

namespace darkeye
{

class ImageFetchService final : public QObject
{
    Q_OBJECT

public:
    explicit ImageFetchService(
        QUrl endpoint = QUrl(QStringLiteral("http://127.0.0.1:56790/api/v1/image")),
        QObject *parent = nullptr);

    [[nodiscard]] quint64 fetchToJpeg(const QUrl &sourceUrl, const QString &destinationPath);
    bool cancel(quint64 requestId);
    void cancelAll();
    void setTimeoutMilliseconds(int timeoutMilliseconds);
    [[nodiscard]] int timeoutMilliseconds() const noexcept;
    [[nodiscard]] bool isBusy() const noexcept;

    [[nodiscard]] static QString suggestedJpegFileName(const QUrl &sourceUrl);

signals:
    void requestStarted(quint64 requestId);
    void requestFinished(quint64 requestId, bool succeeded, const QString &destinationPath,
                         const QString &errorMessage);

private:
    class Private;
    Private *d = nullptr;
};

} // namespace darkeye
