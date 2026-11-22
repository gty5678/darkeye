#pragma once

#include "utils/UpdateUtils.h"

#include <QNetworkAccessManager>
#include <QObject>
#include <QPointer>
#include <QTimer>
#include <QUrl>
#include <QStringList>

class QNetworkReply;

namespace darkeye
{

/// Retrieves and evaluates the release manifest without blocking the UI thread.
class UpdateService final : public QObject
{
    Q_OBJECT

public:
    explicit UpdateService(QObject *parent = nullptr);

    /// Starts a new check. A running check is cancelled before the new one begins.
    void check(const QUrl &manifestUrl, const QString &localVersion,
               int timeoutMilliseconds = 8000, int maximumAttempts = 3);

    void cancel();

    static QUrl manifestUrl();
    // Stages the native updater outside the installation before launching it.
    static bool launchUpdater(QString *error, const QStringList &extraArguments = {});

signals:
    void finished(const darkeye::utils::UpdateCheckResult &result);

private:
    void requestManifest();
    void completeFailure(const QString &message);
    void retryOrFail(const QString &message, bool retryable);

    QNetworkAccessManager m_network;
    QPointer<QNetworkReply> m_reply;
    QTimer m_timeout;
    QTimer m_retry;
    QUrl m_manifestUrl;
    QString m_localVersion;
    int m_attempt = 0;
    int m_maximumAttempts = 1;
    int m_timeoutMilliseconds = 8000;
    bool m_active = false;
};

} // namespace darkeye
