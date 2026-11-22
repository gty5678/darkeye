#include "ui/pages/InboxPage.h"
#include "ui/pages/PageScrollArea.h"

#include "crawler/CrawlerScheduler.h"
#include "services/CrawlerPersistenceService.h"
#include "darkeye_ui/components/DesignButton.h"
#include "darkeye_ui/components/DesignLabel.h"
#include "darkeye_ui/components/TokenViews.h"
#include "ui/components/WorkCompletenessIndicators.h"

#include <QDataStream>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollBar>
#include <QTimer>
#include <QVBoxLayout>

namespace darkeye
{
namespace
{

void updateQueueList(QListWidget *list, QLabel *empty)
{
    const int count = list->count();
    list->setVisible(count > 0);
    empty->setVisible(count == 0);
    if (count > 0)
    {
        const int previewRows = qMin(count, 4);
        list->setFixedHeight(previewRows * list->sizeHintForRow(0) + 2 * list->frameWidth());
    }
}

} // namespace

InboxPage::InboxPage(CrawlerScheduler &crawlerScheduler,
                     CrawlerPersistenceService &crawlerPersistence, QWidget *parent)
    : LazyWidget(parent), m_crawlerScheduler(crawlerScheduler),
      m_crawlerPersistence(crawlerPersistence)
{
    connect(&m_crawlerScheduler, &CrawlerScheduler::queueChanged, this, &InboxPage::refresh);
    connect(&m_crawlerScheduler, &CrawlerScheduler::pausedChanged, this, &InboxPage::refresh);
    connect(&m_crawlerScheduler, &CrawlerScheduler::taskStateChanged, this,
            [this](const CrawlTaskSnapshot &) { refresh(); });
    connect(&m_crawlerPersistence, &CrawlerPersistenceService::completenessChanged, this,
            [this](const QString &serial, const QMap<QString, bool> &flags)
            {
                m_completeness.insert(serial, flags);
                rebuildCompletedList();
            });
    connect(&m_crawlerPersistence, &CrawlerPersistenceService::coverDownloadStarted, this,
            [this](const QString &serial, int)
            {
                m_coverDownloads.insert(serial);
                refresh();
            });
    connect(&m_crawlerPersistence, &CrawlerPersistenceService::finished, this,
            [this](const QString &serial, bool succeeded, const QString &errorMessage)
            {
                m_coverDownloads.remove(serial);
                m_finishedSuccess.insert(serial, succeeded);
                m_finishedErrors.insert(serial, errorMessage);
                m_finishedSerials.removeAll(serial);
                m_finishedSerials.prepend(serial);
                refresh();
            });
    auto *snapshotTimer = new QTimer(this);
    snapshotTimer->setObjectName(QStringLiteral("inboxSnapshotTimer"));
    connect(snapshotTimer, &QTimer::timeout, this, &InboxPage::refresh);
    snapshotTimer->start(1000);
}

void InboxPage::lazyLoad()
{
    auto *content = addPageScrollArea(this);
    auto *layout = new QVBoxLayout(content);
    layout->setContentsMargins(8, 10, 8, 10);
    layout->setSpacing(8);

    m_schedulerLabel = new DesignLabel({}, content);
    m_schedulerLabel->setWordWrap(true);
    layout->addWidget(m_schedulerLabel);

    auto *toggleQueue = new DesignButton({}, content);
    toggleQueue->setVariant(QStringLiteral("primary"));
    m_toggleQueue = toggleQueue;
    layout->addWidget(m_toggleQueue);
    connect(m_toggleQueue, &QPushButton::clicked, this, [this] {
        if (m_crawlerScheduler.isPaused())
            m_crawlerScheduler.resume();
        else
            m_crawlerScheduler.pause(false);
    });
    m_clearQueue = new DesignButton(QStringLiteral("清空队列"), content);
    m_clearQueue->setToolTip(QStringLiteral("清空待采集队列并暂停调度，进行中的任务会继续完成。"));
    layout->addWidget(m_clearQueue);
    connect(m_clearQueue, &QPushButton::clicked, this,
            [this]
            {
                const int count = m_crawlerScheduler.queuedCount();
                if (count == 0)
                    return;
                const auto answer = QMessageBox::question(
                    this, QStringLiteral("清空队列"),
                    QStringLiteral("确定清空待采集队列中的 %1 个番号吗？\n清空后调度将暂停，进行中的任务会继续完成。")
                        .arg(count),
                    QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
                if (answer == QMessageBox::Yes)
                    m_crawlerScheduler.pause(true);
            });

    const auto addSection = [content, layout](QLabel *&heading, QLabel *&empty, QListWidget *&list,
                                             const QString &name, const QString &emptyText) {
        layout->addSpacing(4);
        heading = new DesignLabel({}, content);
        heading->setWordWrap(true);
        auto font = heading->font();
        font.setBold(true);
        heading->setFont(font);
        layout->addWidget(heading);
        empty = new DesignLabel(emptyText, QStringLiteral("muted"), content);
        empty->setWordWrap(true);
        layout->addWidget(empty);
        list = new TokenListWidget(content);
        list->setObjectName(name);
        list->setSelectionMode(QAbstractItemView::NoSelection);
        list->setEditTriggers(QAbstractItemView::NoEditTriggers);
        list->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        list->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
        list->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
        layout->addWidget(list);
    };
    addSection(m_activeLabel, m_activeEmpty, m_activeList, QStringLiteral("inboxActiveList"),
               QStringLiteral("暂无进行中的任务"));
    addSection(m_pendingLabel, m_pendingEmpty, m_pendingList, QStringLiteral("inboxPendingList"),
               QStringLiteral("队列为空"));
    addSection(m_completedLabel, m_completedEmpty, m_completedList, QStringLiteral("inboxCompletedList"),
               QStringLiteral("暂无采集结果"));
    m_completedList->hide();
    m_completedList->setMinimumHeight(110);
    layout->setStretchFactor(m_completedList, 1);
    layout->addStretch();
    refresh();
}

void InboxPage::refresh()
{
    if (m_schedulerLabel == nullptr)
        return;
    const bool paused = m_crawlerScheduler.isPaused();
    m_schedulerLabel->setText(paused ? QStringLiteral("调度已暂停") : QStringLiteral("调度运行中"));
    m_toggleQueue->setText(paused ? QStringLiteral("开始队列") : QStringLiteral("暂停队列"));
    m_toggleQueue->setToolTip(paused
        ? QStringLiteral("继续处理待采集队列。")
        : QStringLiteral("暂停领取新任务，进行中的任务会继续完成。"));
    const QStringList pending = m_crawlerScheduler.pendingSerials();
    m_clearQueue->setEnabled(!pending.isEmpty());
    if (pending != m_pendingFingerprint)
    {
        m_pendingFingerprint = pending;
        m_pendingList->clear();
        m_pendingList->addItems(pending);
    }
    QStringList activeRows;
    const auto activeTasks = m_crawlerScheduler.activeTasks();
    for (const CrawlTaskSnapshot &task : activeTasks)
    {
        const QString state = m_coverDownloads.contains(task.serialNumber)
            ? QStringLiteral("封面下载")
            : task.state == CrawlWorkflowState::Crawling ? QStringLiteral("API 请求")
                                                        : QStringLiteral("入库");
        activeRows.append(QStringLiteral("%1 · %2").arg(task.serialNumber, state));
    }
    if (activeRows != m_activeFingerprint)
    {
        m_activeFingerprint = activeRows;
        m_activeList->clear();
        m_activeList->addItems(activeRows);
    }
    updateQueueList(m_pendingList, m_pendingEmpty);
    updateQueueList(m_activeList, m_activeEmpty);
    m_pendingLabel->setText(QStringLiteral("待采集 · %1").arg(pending.size()));
    m_activeLabel->setText(
        QStringLiteral("进行中 · %1").arg(activeTasks.size()));
    m_completedLabel->setText(QStringLiteral("采集结果 · %1").arg(m_finishedSerials.size()));
    rebuildCompletedList();
}

void InboxPage::rebuildCompletedList()
{
    if (m_completedList == nullptr)
        return;

    QByteArray fingerprint;
    QDataStream stream(&fingerprint, QIODevice::WriteOnly);
    for (const QString &serial : m_finishedSerials)
        stream << serial << m_finishedSuccess.value(serial) << m_finishedErrors.value(serial)
               << m_completeness.contains(serial) << m_completeness.value(serial);
    if (fingerprint == m_completedFingerprint)
        return;
    m_completedFingerprint = fingerprint;

    const int verticalPosition = m_completedList->verticalScrollBar()->value();
    m_completedList->setUpdatesEnabled(false);
    m_completedList->clear();
    for (const QString &serial : m_finishedSerials)
    {
        auto *item = new QListWidgetItem;
        auto *row = new QWidget(m_completedList);
        auto *layout = new QVBoxLayout(row);
        layout->setContentsMargins(6, 5, 6, 5);
        layout->setSpacing(3);
        auto *title = new DesignLabel(serial, row);
        title->setTextFormat(Qt::PlainText);
        title->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
        title->setToolTip(serial);
        layout->addWidget(title);
        const bool succeeded = m_finishedSuccess.value(serial);
        const auto flags = m_completeness.constFind(serial);
        const auto &keys = workCompletenessKeys();
        QString bits;
        if (flags != m_completeness.cend())
        {
            for (const QString &key : keys)
                bits += flags->value(key) ? u'1' : u'0';
        }
        QString detail = succeeded ? QStringLiteral("成功") : QStringLiteral("失败");
        if (!bits.isEmpty())
            detail += QStringLiteral(" · %1/%2").arg(bits.count(u'1')).arg(keys.size());
        auto *status = new DesignLabel(detail, QStringLiteral("muted"), row);
        status->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
        const QString error = m_finishedErrors.value(serial);
        QString tooltip = WorkCompletenessBitsDelegate::tooltipForBits(bits);
        if (!succeeded && !error.isEmpty())
            tooltip.prepend(error + QStringLiteral("\n\n"));
        const QString richTooltip = tooltip.toHtmlEscaped().replace(u'\n', QStringLiteral("<br>"));
        status->setToolTip(QStringLiteral("<qt>%1</qt>").arg(richTooltip));
        layout->addWidget(status);
        item->setSizeHint(row->sizeHint());
        m_completedList->addItem(item);
        m_completedList->setItemWidget(item, row);
    }
    m_completedList->doItemsLayout();
    m_completedList->verticalScrollBar()->setValue(verticalPosition);
    m_completedList->setUpdatesEnabled(true);
    m_completedList->setVisible(!m_finishedSerials.isEmpty());
    m_completedEmpty->setVisible(m_finishedSerials.isEmpty());
}

} // namespace darkeye
