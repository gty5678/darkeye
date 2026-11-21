#include "crawler/CrawlerScheduler.h"

#include "crawler/CollectorClient.h"

#include <QDateTime>
#include <QRandomGenerator>
#include <utility>

namespace darkeye
{

namespace
{

constexpr int minimumIntervalMilliseconds = 12'000;
constexpr int maximumIntervalMilliseconds = 18'000;

} // namespace

CrawlerScheduler::CrawlerScheduler(QUrl workApiBaseUrl, const QStringList &unfinishedSerials,
                                   QObject *parent)
    : QObject(parent), m_collector(new CollectorClient(std::move(workApiBaseUrl), {}, {}, this))
{
    qRegisterMetaType<CrawlTaskSnapshot>();
    qRegisterMetaType<CrawlWorkflowState>();
    m_scheduleTimer.setSingleShot(true);
    connect(&m_scheduleTimer, &QTimer::timeout, this, &CrawlerScheduler::onScheduleTimeout);
    connect(m_collector, &CollectorClient::requestFinished, this,
            [this](quint64 requestId, CollectorRequestKind kind, bool succeeded,
                   const QJsonObject &payload, const QString &errorMessage, int httpStatus)
            {
                onCollectorFinished(requestId, static_cast<int>(kind), succeeded, payload,
                                    errorMessage, httpStatus);
            });

    for (const QString &serial : unfinishedSerials)
    {
        const QString normalized = normalizedSerial(serial);
        if (normalized.isEmpty() || m_unfinishedSerials.contains(normalized))
            continue;
        m_queue.enqueue({normalized, false, {}});
        m_unfinishedSerials.insert(normalized);
    }
    if (!m_queue.isEmpty())
    {
        m_paused = true;
        emit queueChanged();
    }
}

void CrawlerScheduler::enqueue(const QStringList &serialNumbers, bool withGui,
                               const QSet<QString> &selectedFields, bool prepend)
{
    QList<QueueItem> batch;
    QSet<QString> seen;
    for (const QString &value : serialNumbers)
    {
        const QString serial = normalizedSerial(value);
        if (serial.isEmpty() || seen.contains(serial))
            continue;
        seen.insert(serial);
        batch.append({serial, withGui, selectedFields});
    }
    if (batch.isEmpty())
        return;

    if (m_paused)
        resume();

    if (prepend)
    {
        QQueue<QueueItem> retained;
        while (!m_queue.isEmpty())
        {
            const QueueItem item = m_queue.dequeue();
            if (!seen.contains(item.serialNumber))
                retained.enqueue(item);
        }
        for (auto iterator = batch.crbegin(); iterator != batch.crend(); ++iterator)
            retained.prepend(*iterator);
        m_queue = std::move(retained);
        m_prependPriorityTicks = qMax(0, batch.size() - 1) + (m_workFetchBusy ? 1 : 0);
    }
    else
    {
        QSet<QString> queued;
        for (const QueueItem &item : std::as_const(m_queue))
            queued.insert(item.serialNumber);
        // A quick-add request is a background workflow, not a request to
        // restart work already being fetched or persisted.  The previous
        // check covered only m_queue, so a second submit while the first copy
        // was active appended another identical task.  Python's quick-add
        // paths deduplicate their collected serials before starting a crawl;
        // keep that one-workflow-per-serial invariant at the scheduler edge.
        QSet<QString> inFlight;
        for (auto iterator = m_activeTasks.cbegin(); iterator != m_activeTasks.cend(); ++iterator)
            inFlight.insert(iterator.key());
        if (!m_currentSerial.isEmpty())
            inFlight.insert(m_currentSerial);

        QList<QueueItem> accepted;
        for (const QueueItem &item : batch)
        {
            if (queued.contains(item.serialNumber) || inFlight.contains(item.serialNumber))
                continue;
            m_queue.enqueue(item);
            queued.insert(item.serialNumber);
            accepted.append(item);
        }
        batch = std::move(accepted);
    }
    for (const QueueItem &item : batch)
        m_unfinishedSerials.insert(item.serialNumber);
    publishUnfinished();
    emit queueChanged();

    if (prepend)
    {
        m_scheduleTimer.stop();
        scheduleNext(0);
    }
    else if (!m_scheduleTimer.isActive())
    {
        const qint64 now = QDateTime::currentMSecsSinceEpoch();
        scheduleNext(qMax<qint64>(0, minimumIntervalMilliseconds - (now - m_lastScheduleMilliseconds)));
    }
}

void CrawlerScheduler::pause(bool clearQueue)
{
    m_paused = true;
    m_scheduleTimer.stop();
    if (clearQueue)
    {
        while (!m_queue.isEmpty())
            m_unfinishedSerials.remove(m_queue.dequeue().serialNumber);
        m_prependPriorityTicks = 0;
        publishUnfinished();
        emit queueChanged();
    }
    emit pausedChanged(true);
}

void CrawlerScheduler::resume()
{
    if (!m_paused)
        return;
    m_paused = false;
    emit pausedChanged(false);
    if (!m_queue.isEmpty() && !m_scheduleTimer.isActive())
    {
        const qint64 now = QDateTime::currentMSecsSinceEpoch();
        scheduleNext(qMax<qint64>(0, minimumIntervalMilliseconds - (now - m_lastScheduleMilliseconds)));
    }
}

void CrawlerScheduler::complete(const QString &serialNumber, bool succeeded)
{
    finishCurrent(normalizedSerial(serialNumber), succeeded);
}

bool CrawlerScheduler::isPaused() const noexcept { return m_paused; }
int CrawlerScheduler::queuedCount() const noexcept { return m_queue.size(); }

QStringList CrawlerScheduler::pendingSerials() const
{
    QStringList serials;
    for (const QueueItem &item : m_queue)
        serials.append(item.serialNumber);
    return serials;
}

QList<CrawlTaskSnapshot> CrawlerScheduler::activeTasks() const { return m_activeTasks.values(); }

void CrawlerScheduler::onScheduleTimeout()
{
    if (m_paused || m_queue.isEmpty())
        return;
    if (m_workFetchBusy)
    {
        scheduleNext(m_prependPriorityTicks > 0 ? 0 : 1'000);
        return;
    }
    const QueueItem item = m_queue.dequeue();
    m_lastScheduleMilliseconds = QDateTime::currentMSecsSinceEpoch();
    m_workFetchBusy = true;
    m_currentSerial = item.serialNumber;
    CrawlTaskSnapshot task{item.serialNumber, CrawlWorkflowState::Crawling, item.withGui,
                           item.selectedFields};
    m_activeTasks.insert(item.serialNumber, task);
    emit queueChanged();
    emit taskStateChanged(task);
    const quint64 requestId = m_collector->fetchWork(item.serialNumber);
    Q_UNUSED(requestId);
}

void CrawlerScheduler::onCollectorFinished(quint64, int kind, bool succeeded,
                                           const QJsonObject &payload,
                                           const QString &errorMessage, int httpStatus)
{
    if (kind != static_cast<int>(CollectorRequestKind::Work) || m_currentSerial.isEmpty())
        return;
    const QString serial = m_currentSerial;
    m_currentSerial.clear();
    m_workFetchBusy = false;
    auto iterator = m_activeTasks.find(serial);
    if (iterator == m_activeTasks.end())
        return;
    const QJsonObject data = payload.value(QStringLiteral("data")).toObject();
    if (!succeeded || !payload.value(QStringLiteral("ok")).toBool() || data.isEmpty())
    {
        const QString detail = errorMessage.isEmpty()
            ? payload.value(QStringLiteral("error")).toString(
                  QStringLiteral("Collector 请求失败（HTTP %1）").arg(httpStatus))
            : errorMessage;
        emit workFailed(serial, detail);
        // Keep this serial persisted, matching Python's retry-after-restart behavior.
        m_activeTasks.erase(iterator);
        emit queueChanged();
        if (!m_paused && !m_queue.isEmpty())
            scheduleNext();
        return;
    }
    iterator->state = CrawlWorkflowState::Persisting;
    emit taskStateChanged(*iterator);
    emit workFetched(serial, data, iterator->selectedFields, iterator->withGui);
}

QString CrawlerScheduler::normalizedSerial(const QString &serialNumber)
{
    return serialNumber.trimmed();
}

void CrawlerScheduler::scheduleNext(int delayMilliseconds)
{
    if (m_paused || m_queue.isEmpty())
        return;
    if (delayMilliseconds < 0)
        delayMilliseconds = QRandomGenerator::global()->bounded(
            minimumIntervalMilliseconds, maximumIntervalMilliseconds + 1);
    m_scheduleTimer.start(delayMilliseconds);
}

void CrawlerScheduler::publishUnfinished()
{
    QStringList serials = m_unfinishedSerials.values();
    serials.sort();
    emit unfinishedSerialsChanged(serials);
}

void CrawlerScheduler::finishCurrent(const QString &serial, bool)
{
    if (serial.isEmpty())
        return;
    if (m_activeTasks.remove(serial) == 0)
        return;
    m_unfinishedSerials.remove(serial);
    publishUnfinished();
    emit queueChanged();
    if (!m_paused && !m_queue.isEmpty())
    {
        if (m_prependPriorityTicks > 0)
        {
            --m_prependPriorityTicks;
            scheduleNext(0);
        }
        else
        {
            scheduleNext();
        }
    }
}

} // namespace darkeye
