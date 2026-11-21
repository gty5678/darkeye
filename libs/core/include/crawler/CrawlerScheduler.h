#pragma once

#include <QHash>
#include <QJsonObject>
#include <QList>
#include <QQueue>
#include <QSet>
#include <QString>
#include <QTimer>
#include <QUrl>

namespace darkeye
{

class CollectorClient;

enum class CrawlWorkflowState
{
    Queued,
    Crawling,
    Persisting,
};

struct CrawlTaskSnapshot final
{
    QString serialNumber;
    CrawlWorkflowState state = CrawlWorkflowState::Queued;
    bool withGui = false;
    QSet<QString> selectedFields;
};

class CrawlerScheduler final : public QObject
{
    Q_OBJECT

public:
    explicit CrawlerScheduler(QUrl workApiBaseUrl, const QStringList &unfinishedSerials = {},
                              QObject *parent = nullptr);

    void enqueue(const QStringList &serialNumbers, bool withGui = false,
                 const QSet<QString> &selectedFields = {}, bool prepend = false);
    void pause(bool clearQueue = false);
    void resume();
    void complete(const QString &serialNumber, bool succeeded);

    [[nodiscard]] bool isPaused() const noexcept;
    [[nodiscard]] int queuedCount() const noexcept;
    [[nodiscard]] QStringList pendingSerials() const;
    [[nodiscard]] QList<CrawlTaskSnapshot> activeTasks() const;

signals:
    void queueChanged();
    void pausedChanged(bool paused);
    void unfinishedSerialsChanged(const QStringList &serialNumbers);
    void taskStateChanged(const darkeye::CrawlTaskSnapshot &task);
    void workFetched(const QString &serialNumber, const QJsonObject &data,
                     const QSet<QString> &selectedFields, bool withGui);
    void workFailed(const QString &serialNumber, const QString &errorMessage);

private slots:
    void onScheduleTimeout();
    void onCollectorFinished(quint64 requestId, int kind, bool succeeded,
                             const QJsonObject &payload, const QString &errorMessage,
                             int httpStatus);

private:
    struct QueueItem final
    {
        QString serialNumber;
        bool withGui = false;
        QSet<QString> selectedFields;
    };

    [[nodiscard]] static QString normalizedSerial(const QString &serialNumber);
    void scheduleNext(int delayMilliseconds = -1);
    void publishUnfinished();
    void finishCurrent(const QString &serialNumber, bool succeeded);

    CollectorClient *m_collector = nullptr;
    QQueue<QueueItem> m_queue;
    QHash<QString, CrawlTaskSnapshot> m_activeTasks;
    QSet<QString> m_unfinishedSerials;
    QTimer m_scheduleTimer;
    qint64 m_lastScheduleMilliseconds = 0;
    int m_prependPriorityTicks = 0;
    bool m_paused = false;
    bool m_workFetchBusy = false;
    QString m_currentSerial;
};

} // namespace darkeye

Q_DECLARE_METATYPE(darkeye::CrawlWorkflowState)
Q_DECLARE_METATYPE(darkeye::CrawlTaskSnapshot)
