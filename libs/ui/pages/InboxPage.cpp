#include "ui/pages/InboxPage.h"
#include "ui/pages/PageScrollArea.h"

#include "crawler/CrawlerScheduler.h"
#include "services/CrawlerPersistenceService.h"
#include "darkeye_ui/components/DesignButton.h"
#include "darkeye_ui/components/DesignLabel.h"
#include "darkeye_ui/components/TokenViews.h"
#include "ui/components/WorkCompletenessIndicators.h"

#include <QHBoxLayout>
#include <QDataStream>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollBar>
#include <QTimer>
#include <QVBoxLayout>

namespace darkeye
{

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
    auto *layout = new QVBoxLayout(addPageScrollArea(this));
    layout->setContentsMargins(24, 24, 24, 24);
    auto *title = new DesignLabel(QStringLiteral("通知与爬虫任务"), this);
    layout->addWidget(title);

    auto *toolbar = new QHBoxLayout;
    m_schedulerLabel = new QLabel(this);
    toolbar->addWidget(m_schedulerLabel);
    toolbar->addStretch();
    auto *resume = new DesignButton(QStringLiteral("开始队列"), this);
    auto *pause = new DesignButton(QStringLiteral("暂停队列"), this);
    auto *clear = new DesignButton(QStringLiteral("清除全部队列"), this);
    toolbar->addWidget(resume);
    toolbar->addWidget(pause);
    toolbar->addWidget(clear);
    resume->setToolTip(QStringLiteral("恢复调度：按待爬队列继续处理尚未开始的番号。"));
    pause->setToolTip(QStringLiteral("暂停调度：不再从队列弹出新的番号；已在请求、入库或下载中的任务会继续完成。"));
    connect(resume, &QPushButton::clicked, this, [this] { m_crawlerScheduler.resume(); });
    connect(pause, &QPushButton::clicked, this, [this] { m_crawlerScheduler.pause(false); });
    clear->setToolTip(QStringLiteral("清空待爬队列并进入暂停态；不会中断已在进行的请求或下载。"));
    connect(clear, &QPushButton::clicked, this,
            [this]
            {
                const int count = m_crawlerScheduler.queuedCount();
                if (count == 0)
                    return;
                const auto answer = QMessageBox::question(
                    this, QStringLiteral("清除全部队列"),
                    QStringLiteral("确定清空待爬队列中的 %1 个番号吗？\n已在进行中的任务不会被中断；清空后调度将暂停。")
                        .arg(count),
                    QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
                if (answer == QMessageBox::Yes)
                    m_crawlerScheduler.pause(true);
            });
    layout->addLayout(toolbar);

    auto *columns = new QHBoxLayout;
    auto *pending = new QVBoxLayout;
    m_pendingLabel = new DesignLabel(QStringLiteral("待爬 (0)"), this);
    pending->addWidget(m_pendingLabel);
    m_pendingList = new TokenListWidget(this);
    m_pendingList->setObjectName(QStringLiteral("inboxPendingList"));
    m_pendingList->setSelectionMode(QAbstractItemView::NoSelection);
    m_pendingList->setEditTriggers(QAbstractItemView::NoEditTriggers);
    pending->addWidget(m_pendingList);
    columns->addLayout(pending);
    auto *active = new QVBoxLayout;
    m_activeLabel = new DesignLabel(QStringLiteral("进行中 (0)"), this);
    active->addWidget(m_activeLabel);
    m_activeList = new TokenListWidget(this);
    m_activeList->setObjectName(QStringLiteral("inboxActiveList"));
    m_activeList->setSelectionMode(QAbstractItemView::NoSelection);
    m_activeList->setEditTriggers(QAbstractItemView::NoEditTriggers);
    active->addWidget(m_activeList);
    columns->addLayout(active);
    auto *completed = new QVBoxLayout;
    m_completedLabel = new DesignLabel(QStringLiteral("已完成 (0)"), this);
    completed->addWidget(m_completedLabel);
    m_completedList = new TokenListWidget(this);
    m_completedList->setObjectName(QStringLiteral("inboxCompletedList"));
    m_completedList->setSelectionMode(QAbstractItemView::NoSelection);
    m_completedList->setEditTriggers(QAbstractItemView::NoEditTriggers);
    completed->addWidget(m_completedList);
    columns->addLayout(completed);
    layout->addLayout(columns, 1);
    auto *hint = new DesignLabel(
        QStringLiteral("左：待爬队列；中：API 请求、入库与封面下载；右：任务收尾状态；"
                       "绿/红灯为入库后库内 15 项完整度（封面落盘后可能二次刷新）。"), this);
    hint->setWordWrap(true);
    layout->addWidget(hint);
    refresh();
}

void InboxPage::refresh()
{
    if (m_schedulerLabel == nullptr)
        return;
    m_schedulerLabel->setText(
        QStringLiteral("调度：%1 · 队列长度 %2")
            .arg(m_crawlerScheduler.isPaused() ? QStringLiteral("已暂停")
                                                : QStringLiteral("运行中"))
            .arg(m_crawlerScheduler.queuedCount()));
    const QStringList pending = m_crawlerScheduler.pendingSerials();
    QStringList pendingRows;
    for (const QString &serial : pending)
        pendingRows.append(QStringLiteral("%1 (待爬)").arg(serial));
    if (pendingRows != m_pendingFingerprint)
    {
        m_pendingFingerprint = pendingRows;
        m_pendingList->clear();
        m_pendingList->addItems(pendingRows);
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
    m_pendingLabel->setText(QStringLiteral("待爬 (%1)").arg(pending.size()));
    m_activeLabel->setText(
        QStringLiteral("进行中 (%1)").arg(activeTasks.size()));
    m_completedLabel->setText(QStringLiteral("已完成 (%1)").arg(m_finishedSerials.size()));
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
    const int horizontalPosition = m_completedList->horizontalScrollBar()->value();
    m_completedList->setUpdatesEnabled(false);
    m_completedList->clear();
    for (const QString &serial : m_finishedSerials)
    {
        auto *item = new QListWidgetItem;
        auto *row = new QWidget(m_completedList);
        auto *layout = new QHBoxLayout(row);
        layout->setContentsMargins(6, 4, 6, 4);
        layout->setSpacing(10);
        auto *title = new DesignLabel(serial, row);
        const QString error = m_finishedErrors.value(serial);
        if (!m_finishedSuccess.value(serial) && !error.isEmpty())
        {
            title->setToolTip(error);
            row->setToolTip(error);
        }
        layout->addWidget(title, 1);
        const auto flags = m_completeness.constFind(serial);
        const std::optional<QMap<QString, bool>> completeness = flags == m_completeness.cend()
            ? std::nullopt : std::optional<QMap<QString, bool>>(*flags);
        layout->addWidget(new WorkCompletenessLedStrip(completeness, row));
        item->setSizeHint(row->sizeHint());
        m_completedList->addItem(item);
        m_completedList->setItemWidget(item, row);
    }
    m_completedList->doItemsLayout();
    m_completedList->verticalScrollBar()->setValue(verticalPosition);
    m_completedList->horizontalScrollBar()->setValue(horizontalPosition);
    m_completedList->setUpdatesEnabled(true);
}

} // namespace darkeye
