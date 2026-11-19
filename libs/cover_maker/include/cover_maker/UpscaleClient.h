#pragma once
#include <QObject>
#include <QImage>
#include <QUrl>
class QNetworkAccessManager;
class QNetworkReply;
namespace darkeye::cover_maker {
class UpscaleClient final : public QObject {
    Q_OBJECT
public:
    explicit UpscaleClient(QObject *parent = nullptr);
    bool busy() const { return m_reply != nullptr; }
    void start(const QUrl &url, const QString &token, const QImage &image);
    void cancel();
    static bool validEndpoint(const QUrl &url);
signals:
    void completed(const QImage &image);
    void failed(const QString &message);
    void busyChanged(bool busy);
private:
    QNetworkAccessManager *m_network;
    QNetworkReply *m_reply = nullptr;
    QByteArray m_data;
    bool m_cancelled = false;
    bool m_tooLarge = false;
    QSize m_expected;
};
}
