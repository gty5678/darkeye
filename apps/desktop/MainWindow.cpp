#include "MainWindow.h"
#include "settings/SettingsStore.h"
#include "utils/Documentation.h"

#include "ui/pages/ActorPage.h"
#include "ui/pages/ActressDetailPage.h"
#include "ui/pages/ActressPage.h"
#include "ui/pages/AvPage.h"
#include "ui/pages/ManagementPage.h"
#include "ui/pages/ForceDirectPage.h"
#include "ui/pages/FanartBrowserPage.h"
#include "ui/pages/InboxPage.h"
#include "ui/pages/HomePage.h"
#include "ui/pages/PersonDetailPage.h"
#include "ui/pages/PersonPage.h"
#include "ui/pages/StatisticsPage.h"
#include "ui/pages/SettingsPage.h"
#include "ui/pages/ShelfPage.h"
#include "ui/pages/PlaceholderPage.h"
#include "ui/pages/WorkPage.h"
#include "ui/components/WorkFilter.h"
#include "crawler/CrawlerScheduler.h"
#include "http/LocalApiServer.h"
#include "services/ActressSyncService.h"
#include "darkeye_ui/base/LazyWidget.h"
#include "graph/GraphManager.h"
#include "ui/layouts/myads/WorkspaceWidget.h"
#include "ui/layouts/myads/ThemeAdapter.h"
#include "ui/components/ActressWorkTimeline.h"
#include "ui/components/WorkspaceSearch.h"
#include "ui/components/WorkMaintenanceActions.h"
#include "ui/pages/ModifyActorPage.h"
#include "ui/pages/ModifyActressPage.h"
#include "ui/pages/PersonEditorPage.h"
#include "ui/dialogs/AddMakeLoveDialog.h"
#include "ui/dialogs/AddMasturbationDialog.h"
#include "ui/dialogs/AddSexualArousalDialog.h"
#include "ui/dialogs/AddQuickWorkDialog.h"
#include "darkeye_ui/components/ToastNotification.h"
#include "darkeye_ui/components/IconButton.h"

#include <QAction>
#include <QActionGroup>
#include <QSettings>
#include <QDir>
#include <QFileInfo>
#include <QMessageBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QDesktopServices>
#include <QElapsedTimer>
#include <QFile>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeySequence>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QPixmap>
#include <QScrollArea>
#include <QStackedLayout>
#include <QVBoxLayout>
#include <QTimer>
#include <QUrl>
#include <QUuid>
#include <algorithm>

#include <utility>

#ifdef Q_OS_WIN
#define NOMINMAX
#include <Windows.h>
#include <psapi.h>
#elif defined(Q_OS_MACOS)
#include "MacWorkspaceShortcuts.h"
#include <mach/mach.h>
#include <sys/resource.h>
#include <QThread>
#endif

namespace darkeye
{

namespace
{

struct ManagementRoute
{
    QString route;
    QString title;
    ManagementPage::Section section;
};

const auto managementRoutes = {
    ManagementRoute{QStringLiteral("manage_tags"), QStringLiteral("标签管理"), ManagementPage::Section::Tags},
    ManagementRoute{QStringLiteral("manage_makers"), QStringLiteral("片商管理"), ManagementPage::Section::Makers},
    ManagementRoute{QStringLiteral("manage_labels"), QStringLiteral("厂牌管理"), ManagementPage::Section::Labels},
    ManagementRoute{QStringLiteral("manage_series"), QStringLiteral("系列管理"), ManagementPage::Section::Series},
    ManagementRoute{QStringLiteral("manage_summary"), QStringLiteral("资料汇总"), ManagementPage::Section::SummaryQuery},
    ManagementRoute{QStringLiteral("manage_records"), QStringLiteral("个人记录"), ManagementPage::Section::PersonalRecords},
    ManagementRoute{QStringLiteral("manage_deleted"), QStringLiteral("批量移入回收站"), ManagementPage::Section::SoftDelete},
    ManagementRoute{QStringLiteral("manage_trash"), QStringLiteral("回收站"), ManagementPage::Section::RecycleBin},
};

constexpr qsizetype recentWorkspaceLimit = 9;
const auto actressSwitchRoute = QStringLiteral("actress_detail/switch");

struct ShelfReferenceRoute
{
    QString title;
    QList<NamedIdOption> (WorkRepository::*options)() const;
    void (ShelfPage::*apply)(qint64);
};

const QMap<QString, ShelfReferenceRoute> shelfReferenceRoutes{
    {QStringLiteral("maker"), {QStringLiteral("片商"), &WorkRepository::makerOptions, &ShelfPage::filterByMaker}},
    {QStringLiteral("label"), {QStringLiteral("厂牌"), &WorkRepository::labelOptions, &ShelfPage::filterByLabel}},
    {QStringLiteral("series"), {QStringLiteral("系列"), &WorkRepository::seriesOptions, &ShelfPage::filterBySeries}},
};

bool isRecentWorkspaceRoute(const QString &route)
{
    const auto view = QStringView(route);
    const auto slash = view.indexOf(u'/');
    const auto base = slash < 0 ? view : view.first(slash);
    return !base.isEmpty() && base != u"new"
        && base != u"setting" && base != u"search" && base != u"database" && base != u"person_graph"
        && base != u"manage_work" && base != u"actor_detail";
}

QString formatMemory(qint64 bytes)
{
    return QStringLiteral("%1 MiB").arg(bytes / 1024.0 / 1024.0, 0, 'f', 1);
}

QString processMemoryUsage()
{
#ifdef Q_OS_WIN
    PROCESS_MEMORY_COUNTERS_EX2 counters{};
    counters.cb = sizeof(counters);
    if (GetProcessMemoryInfo(GetCurrentProcess(),
                             reinterpret_cast<PROCESS_MEMORY_COUNTERS *>(&counters),
                             sizeof(counters)))
    {
        const qint64 privateWorkingSet = static_cast<qint64>(counters.PrivateWorkingSetSize);
        const qint64 sharedWorkingSet = qMax<qint64>(
            0, static_cast<qint64>(counters.WorkingSetSize) - privateWorkingSet);
        return QStringLiteral("公共内存 %1 | 私有内存 %2")
            .arg(formatMemory(sharedWorkingSet), formatMemory(privateWorkingSet));
    }
#elif defined(Q_OS_MACOS)
    task_vm_info_data_t counters{};
    mach_msg_type_number_t count = TASK_VM_INFO_COUNT;
    if (task_info(mach_task_self(), TASK_VM_INFO,
                  reinterpret_cast<task_info_t>(&counters), &count) == KERN_SUCCESS)
    {
        // Activity Monitor reports physical footprint, not internal + external
        // resident pages. The latter are not a private/shared memory split.
        return QStringLiteral("内存占用 %1 | 驻留内存 %2")
            .arg(formatMemory(static_cast<qint64>(counters.phys_footprint)),
                 formatMemory(static_cast<qint64>(counters.resident_size)));
    }
#endif
    return QStringLiteral("内存 N/A");
}

QString processCpuUsage()
{
#ifdef Q_OS_WIN
    FILETIME creationTime{};
    FILETIME exitTime{};
    FILETIME kernelTime{};
    FILETIME userTime{};
    if (!GetProcessTimes(GetCurrentProcess(), &creationTime, &exitTime, &kernelTime, &userTime))
    {
        return QStringLiteral("N/A");
    }

    const auto toTicks = [](const FILETIME &time)
    {
        ULARGE_INTEGER value{};
        value.LowPart = time.dwLowDateTime;
        value.HighPart = time.dwHighDateTime;
        return value.QuadPart;
    };

    static QElapsedTimer elapsed;
    static ULONGLONG previousCpuTime = 0;
    const ULONGLONG currentCpuTime = toTicks(kernelTime) + toTicks(userTime);
    if (!elapsed.isValid())
    {
        previousCpuTime = currentCpuTime;
        elapsed.start();
        return QStringLiteral("0.0%");
    }

    const qint64 elapsedNanoseconds = elapsed.nsecsElapsed();
    elapsed.restart();
    const ULONGLONG cpuTimeDelta = currentCpuTime - previousCpuTime;
    previousCpuTime = currentCpuTime;
    if (elapsedNanoseconds <= 0)
    {
        return QStringLiteral("0.0%");
    }

    SYSTEM_INFO systemInfo{};
    GetSystemInfo(&systemInfo);
    const DWORD processorCount = qMax<DWORD>(systemInfo.dwNumberOfProcessors, 1);
    const double usage = qBound(0.0, static_cast<double>(cpuTimeDelta) * 10000.0 /
        static_cast<double>(elapsedNanoseconds) / processorCount, 100.0);
    return QStringLiteral("%1%").arg(usage, 5, 'f', 1, QLatin1Char('0'));
#elif defined(Q_OS_MACOS)
    rusage counters{};
    if (getrusage(RUSAGE_SELF, &counters) != 0)
        return QStringLiteral("N/A");

    // RUSAGE_SELF includes all process threads, including terminated threads.
    // timeval uses microseconds on both Apple Silicon and Intel Macs.
    const auto toMicroseconds = [](const timeval &time) -> qint64
    {
        return static_cast<qint64>(time.tv_sec) * 1000000 + time.tv_usec;
    };
    const qint64 currentCpuTime = toMicroseconds(counters.ru_utime)
        + toMicroseconds(counters.ru_stime);
    static QElapsedTimer elapsed;
    static qint64 previousCpuTime = 0;
    if (!elapsed.isValid())
    {
        previousCpuTime = currentCpuTime;
        elapsed.start();
        return QStringLiteral("0.0%");
    }

    const qint64 elapsedNanoseconds = elapsed.nsecsElapsed();
    elapsed.restart();
    const qint64 cpuTimeDelta = qMax<qint64>(0, currentCpuTime - previousCpuTime);
    previousCpuTime = currentCpuTime;
    if (elapsedNanoseconds <= 0)
        return QStringLiteral("0.0%");

    const int processorCount = qMax(QThread::idealThreadCount(), 1);
    const double usage = qBound(0.0, static_cast<double>(cpuTimeDelta) * 100000.0 /
        static_cast<double>(elapsedNanoseconds) / processorCount, 100.0);
    return QStringLiteral("%1%").arg(usage, 5, 'f', 1, QLatin1Char('0'));
#else
    return QStringLiteral("N/A");
#endif
}

} // namespace

MainWindow::MainWindow(ThemeService &themeService,
                       QSqlDatabase publicDatabase, QSqlDatabase privateDatabase,
                       settings::Paths paths, QWidget *parent)
    : QMainWindow(parent), m_themeService(themeService),
      m_paths(std::move(paths)), m_publicDatabase(std::move(publicDatabase)),
      m_privateDatabase(std::move(privateDatabase))
{
    updateWindowTitle();
    m_graphManager = std::make_unique<graph::GraphManager>(m_publicDatabase, m_privateDatabase,
                                                             this);
    CrawlerSettings crawlerSettings = settings::crawler();
    m_crawlerScheduler = std::make_unique<CrawlerScheduler>(
        crawlerSettings.workApiBaseUrl, crawlerSettings.unfinishedSerials, this);
    m_crawlerPersistence = std::make_unique<CrawlerPersistenceService>(
        m_publicDatabase.databaseName(), m_paths.workCoverDirectory(),
        crawlerSettings.coverFetchApiUrl, this);
    m_backgroundActressSync = std::make_unique<ActressSyncService>(
        m_publicDatabase, crawlerSettings.actressApiBaseUrl, m_paths.actressImageDirectory(),
        crawlerSettings.coverFetchApiUrl, this);
    connect(m_crawlerScheduler.get(), &CrawlerScheduler::unfinishedSerialsChanged, this,
            [](const QStringList &serials)
            {
                CrawlerSettings updated = settings::crawler();
                updated.unfinishedSerials = serials;
                settings::saveCrawler(updated);
            });
    connect(m_crawlerScheduler.get(), &CrawlerScheduler::workFetched, this,
            [this](const QString &serial, const QJsonObject &payload,
                   const QSet<QString> &fields, bool withGui)
            {
                if (!withGui)
                    m_crawlerPersistence->persist(serial, payload, fields);
            });
    connect(m_crawlerPersistence.get(), &CrawlerPersistenceService::finished, this,
            [this](const QString &serial, bool succeeded, const QString &errorMessage)
            {
                if (!succeeded && !errorMessage.isEmpty())
                    qWarning() << "Crawler persistence failed" << serial << errorMessage;
                m_crawlerScheduler->complete(serial, succeeded);
            });
    connect(m_crawlerPersistence.get(), &CrawlerPersistenceService::workPersisted, this,
            [this] { queueWorkspaceRefresh(WorkspaceChanges(TagsChanged) | ReferencesChanged); });
    connect(m_crawlerPersistence.get(), &CrawlerPersistenceService::worksChanged,
            this, [this] { queueWorkspaceRefresh(WorksChanged); });
    connect(m_crawlerPersistence.get(), &CrawlerPersistenceService::actressesChanged,
            this, [this] { queueWorkspaceRefresh(ActressesChanged); });
    connect(m_crawlerPersistence.get(), &CrawlerPersistenceService::actorsChanged,
            this, [this] { queueWorkspaceRefresh(ActorsChanged); });
    connect(m_backgroundActressSync.get(), &ActressSyncService::actressesChanged,
            this, [this] { queueWorkspaceRefresh(ActressesChanged); });
    connect(m_crawlerPersistence.get(), &CrawlerPersistenceService::actressesCreated, this,
            &MainWindow::enqueueActressSyncs);
    connect(m_backgroundActressSync.get(), &ActressSyncService::finished, this,
            [this](const ActressSyncResult &result)
            {
                m_pendingActressSyncIds.remove(result.actressId);
                if (!result.succeeded && !result.errorMessage.isEmpty())
                    qWarning() << "Automatic actress enrichment failed" << result.actressId
                               << result.errorMessage;
                startNextActressSync();
            });
    auto *resourceTimer = new QTimer(this);
    connect(resourceTimer, &QTimer::timeout, this, &MainWindow::updateWindowTitle);
    resourceTimer->start(100);
    buildUi();
    resize(1200, 600);
}

void MainWindow::updateWindowTitle()
{
    setWindowTitle(QStringLiteral("暗之眼 V%1 | CPU %2 | %3")
                       .arg(QStringLiteral(DARKEYE_VERSION), processCpuUsage(),
                            processMemoryUsage()));
}

void MainWindow::showInitial()
{
    if (settings::app().maximized)
    {
        showMaximized();
    }
    else
    {
        show();
    }
}

void MainWindow::setLocalApiServer(LocalApiServer &api)
{
    attachLocalApi(api);
}

void MainWindow::attachLocalApi(LocalApiServer &api)
{
    connect(&api, &LocalApiServer::captureOneReceived, this,
            [this](const QString &serial) { m_crawlerScheduler->enqueue({serial}); });
    connect(&api, &LocalApiServer::crawlerBacklogWarning, this,
            [this](int count, const QString &browser)
            {
                Toast::showWarning(this, QStringLiteral("%1 爬虫标签已积压 %2 个，请处理验证页。").arg(browser).arg(count),
                                   &m_themeService, 6000);
            });
    connect(&api, &LocalApiServer::cloudflareChallengeReceived, this,
            [this](const QJsonObject &payload)
            {
                const QString site = payload.value(QStringLiteral("site")).toString();
                const QString serial = payload.value(QStringLiteral("serial")).toString();
                const QString target = site.isEmpty() ? QStringLiteral("目标站点") : site;
                const QString suffix = serial.isEmpty() ? QString() : QStringLiteral("（%1）").arg(serial);
                Toast::showWarning(this, QStringLiteral("%1 出现 Cloudflare 验证%2，请在浏览器中完成验证。")
                                   .arg(target, suffix), &m_themeService, 8000);
            });
    connect(&api, &LocalApiServer::minnanoActressCaptureReceived, this,
            [this](const QJsonObject &payload)
            {
                QString errorMessage;
                if (m_actressEditorPage == nullptr ||
                    !m_actressEditorPage->applyCapture(payload, &errorMessage))
                    qWarning() << "Minnano actress capture ignored:" << errorMessage;
            });
}

void MainWindow::changeEvent(QEvent *event)
{
    QMainWindow::changeEvent(event);
    if (event->type() != QEvent::ActivationChange || !isActiveWindow()) return;
    if (auto *focused = focusWidget(); focused && focused->isVisible()) return;

    // Qt picks the first focusable control after activating a window without
    // a remembered focus. Use the central widget until activation completes.
    auto *central = centralWidget();
    if (!central) return;
    central->setFocus(Qt::ActiveWindowFocusReason);
    QTimer::singleShot(0, central, [central] {
        if (central->window()->focusWidget() == central) central->clearFocus();
    });
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    for (const auto &page : m_routePages) {
        if (!page) continue;
        QCloseEvent check;
        QCoreApplication::sendEvent(page, &check);
        if (!check.isAccepted()) { event->ignore(); return; }
    }
    saveWindowState();
    QMainWindow::closeEvent(event);
}

void MainWindow::buildUi()
{
    auto *centralWidget = new QWidget(this);
    auto *mainLayout = new QHBoxLayout(centralWidget);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);
    auto *workspaceContainer = new QWidget(centralWidget);
    auto *layout = new QStackedLayout(workspaceContainer);
    layout->setStackingMode(QStackedLayout::StackAll);
    layout->setContentsMargins(0, 0, 0, 0);
    m_workFilter = new WorkFilter(m_publicDatabase, m_privateDatabase, m_themeService,
                                  centralWidget, m_paths.settingsFile());
    m_workFilter->setWindowTitle(QStringLiteral("作品筛选"));
    m_workFilter->hide();
    m_workspace = new myads::WorkspaceWidget(workspaceContainer);
    m_workspace->setObjectName(QStringLiteral("mainWorkspace"));
    const auto appSettings = settings::app(m_paths.settingsFile());
    m_workspace->setSidebarAutoCollapse(appSettings.sidebarAutoCollapse);
    myads::bindDarkeyeTheme(m_workspace, &m_themeService);
    connect(m_workspace, &myads::WorkspaceWidget::contentClosed, this, [this](const QString &route) {
        if (m_routePages.value(route) == m_localGraphSource && m_localGraphSource) {
            m_localGraphSource.clear();
            updateLocalGraph();
        }
        if (route == QStringLiteral("local_graph")) m_localGraphPage.clear();
        if (route == QStringLiteral("timeline")) m_timelinePage.clear();
        if (m_timelineSource && m_timelineSource == m_routePages.value(route)) {
            m_timelineSource.clear();
            updateTimeline();
        }
        if (route == QStringLiteral("work_filters"))
            m_workFilter->setParent(this->centralWidget());
        m_routePages.remove(route);
    });
    connect(m_workspace, &myads::WorkspaceWidget::tabContextMenuCreated, this,
            [this](QMenu *menu, const QString &route) {
        auto *detail = qobject_cast<PersonDetailPage *>(m_routePages.value(route).data());
        if (!detail || detail->currentPersonId() <= 0) return;
        const auto personRoute = QStringLiteral("actress_detail/%1").arg(detail->currentPersonId());
        menu->addSeparator();
        if (route != actressSwitchRoute) {
            menu->addAction(QStringLiteral("在切换页打开"), this, [this, personRoute] {
                navigateTo(personRoute, true, ActressOpenMode::Switching);
            });
        }
        menu->addAction(QStringLiteral("在独立页打开"), this, [this, personRoute] {
            navigateTo(personRoute, true, ActressOpenMode::Independent);
        });
    });
    connect(m_workspace, &myads::WorkspaceWidget::activePaneChanged,
            this, &MainWindow::updateLocalGraphFocus);
    connect(m_workspace, &myads::WorkspaceWidget::activeContentChanged,
            this, &MainWindow::updateLocalGraphFocus);
    connect(m_workspace, &myads::WorkspaceWidget::layoutChanged,
            this, &MainWindow::updateLocalGraphFocus);
    connect(m_workspace, &myads::WorkspaceWidget::activePaneChanged,
            this, &MainWindow::updateTimelineFocus);
    connect(m_workspace, &myads::WorkspaceWidget::activeContentChanged,
            this, &MainWindow::updateTimelineFocus);
    connect(m_workspace, &myads::WorkspaceWidget::layoutChanged,
            this, &MainWindow::updateTimelineFocus);
    setCentralWidget(centralWidget);

    registerPage(QStringLiteral("作品展厅"), QStringLiteral("home"));
    registerPage(QStringLiteral("作品库"), QStringLiteral("mutiwork"));
    registerPage(QStringLiteral("剧照浏览"), QStringLiteral("fanart"));
    registerPage(QStringLiteral("女演员"), QStringLiteral("actress"));
    registerPage(QStringLiteral("男演员"), QStringLiteral("actor"));
    registerPage(QStringLiteral("女演员详情（跟随切换）"), QStringLiteral("actress_detail"));
    registerPage(QStringLiteral("修改女演员"), QStringLiteral("modify_actress"));
    registerPage(QStringLiteral("修改男演员"), QStringLiteral("modify_actor"));
    registerPage(QStringLiteral("统计"), QStringLiteral("chart"));
    registerPage(QStringLiteral("个人数据概览"), QStringLiteral("information"));
    registerPage(QStringLiteral("全局关系图"), QStringLiteral("graph"));
    registerPage(QStringLiteral("局部关系图"), QStringLiteral("local_graph"));
    registerPage(QStringLiteral("作品时间轴"), QStringLiteral("timeline"));
    registerPage(QStringLiteral("DVD 书架"), QStringLiteral("shelf"));
    registerPage(QStringLiteral("暗黑界知识库"), QStringLiteral("av"));
    registerPage(QStringLiteral("采集任务"), QStringLiteral("inbox"));

    for (const auto &entry : managementRoutes)
        registerPage(entry.title, entry.route);
    m_globalSearch = new WorkspaceSearch(m_publicDatabase, workspaceCommandPages(), centralWidget);
    m_globalSearch->hide();
    m_globalSearch->setAutoFillBackground(true);
    connect(m_globalSearch, &WorkspaceSearch::openRequested, this, [this](const QString &route) { navigateTo(route); });
    connect(m_globalSearch, &WorkspaceSearch::independentActressRequested, this, [this](qint64 id) {
        navigateTo(QStringLiteral("actress_detail/%1").arg(id), true, ActressOpenMode::Independent);
    });
    auto *maintenance = new WorkMaintenanceActions(
        m_publicDatabase, m_themeService, m_paths.workCoverDirectory(),
        m_paths.actressImageDirectory(), this);
    m_globalSearch->setBatchActions(maintenance->actions());
    connect(maintenance, &WorkMaintenanceActions::worksChanged, this,
            [this] { queueWorkspaceRefresh(WorksChanged); });
    connect(maintenance, &WorkMaintenanceActions::actressesChanged, this,
            [this] { queueWorkspaceRefresh(ActressesChanged); });
    layout->addWidget(m_workspace);
    registerApplicationActions();
    m_navigationBar = buildNavigationBar(centralWidget);
    mainLayout->addWidget(m_navigationBar);
    mainLayout->addWidget(workspaceContainer, 1);
    m_navigationBar->setVisible(appSettings.navigationBarVisible);

    auto *emptyState = new QWidget(workspaceContainer);
    auto *hintLayout = new QVBoxLayout(emptyState);
    hintLayout->setAlignment(Qt::AlignCenter);
    m_navigationHint = new QLabel(QStringLiteral("点击左侧图标打开页面"), emptyState);
    m_navigationHint->setAlignment(Qt::AlignCenter);
    m_navigationHint->setVisible(appSettings.navigationBarVisible);
    hintLayout->addWidget(m_navigationHint);
    auto *emptyHint = new QLabel(emptyState);
    hintLayout->addWidget(emptyHint);
    auto *searchAction = m_shortcutActions->findChild<QAction *>(QStringLiteral("workspace_search"));
    auto *commandsAction = m_shortcutActions->findChild<QAction *>(QStringLiteral("workspace_commands"));
    const auto updateHintText = [emptyHint, searchAction, commandsAction] {
        QStringList hints;
        for (auto *action : {searchAction, commandsAction}) {
            if (!action->shortcut().isEmpty())
                hints.append(action->shortcut().toString(QKeySequence::NativeText) +
                             QStringLiteral("  ") + action->text());
        }
        emptyHint->setText(hints.join(QStringLiteral("\n")));
    };
    connect(searchAction, &QAction::changed, emptyHint, updateHintText);
    connect(commandsAction, &QAction::changed, emptyHint, updateHintText);
    updateHintText();
    emptyHint->setObjectName(QStringLiteral("workspaceEmptyHint"));
    emptyHint->setAlignment(Qt::AlignCenter);
    emptyHint->setWordWrap(true);
    emptyState->setAttribute(Qt::WA_TransparentForMouseEvents);
    layout->addWidget(emptyState);
    layout->setCurrentWidget(emptyState);
    const auto updateEmptyHint = [this, emptyState] {
        const auto panes = m_workspace->panes();
        emptyState->setVisible(panes.size() == 1 && panes.first()->contentCount() == 0);
    };
    connect(m_workspace, &myads::WorkspaceWidget::activeContentChanged, emptyState, updateEmptyHint);
    connect(m_workspace, &myads::WorkspaceWidget::layoutChanged, emptyState, updateEmptyHint);
    updateEmptyHint();
    QString error;
    const bool restored = QFile::exists(workspaceFile()) && m_workspace->loadLayout(workspaceFile(),
        [this](const QJsonObject &descriptor) -> std::optional<myads::ContentConfig> {
            const auto route = descriptor.value(QStringLiteral("content_id")).toString();
            auto *page = ensurePage(route, descriptor.value(QStringLiteral("actress_id")).toString().toLongLong());
            if (!page) return std::nullopt;
            auto config = myads::ContentConfig(route).setWidget(page).setWindowTitle(page->windowTitle());
            if (page == m_workFilter) config.setClosePolicy(myads::ClosePolicy::KeepAlive);
            return config;
        }, &error);
    if (!restored && !error.isEmpty()) qWarning() << "Workspace restore:" << error;
    if (restored) {
        QSettings settings(m_paths.settingsFile(), QSettings::IniFormat);
        const auto active = settings.value(QStringLiteral("workspace/activeRoute")).toString();
        if (m_workspace->findPaneByContentId(active) || active.startsWith(QStringLiteral("work_detail/")))
            navigateTo(active, false);
    }
}

QWidget *MainWindow::buildNavigationBar(QWidget *parent)
{
    auto *bar = new QWidget(parent);
    bar->setObjectName(QStringLiteral("pageNavigationBar"));
    bar->setAccessibleName(QStringLiteral("页面导航"));
    bar->setFixedWidth(56);
    auto *layout = new QVBoxLayout(bar);
    layout->setContentsMargins(0, 6, 1, 6);
    layout->setSpacing(4);

    auto *scroll = new QScrollArea(bar);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidgetResizable(true);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    auto *pages = new QWidget;
    pages->setObjectName(QStringLiteral("pageNavigationPages"));
    auto *pageLayout = new QVBoxLayout(pages);
    pageLayout->setContentsMargins(0, 0, 0, 0);
    pageLayout->setSpacing(4);
    pageLayout->setAlignment(Qt::AlignTop);
    scroll->setWidget(pages);
    layout->addWidget(scroll, 1);

    const auto addButton = [this](QVBoxLayout *column, const QString &id,
                                  const QString &title, const QString &icon) {
        auto *button = new IconButton(icon, title, &m_themeService, column->parentWidget());
        button->setProperty("navigationId", id);
        button->setButtonPixelSize(36);
        button->setIconPixelSize(20);
        button->setFocusPolicy(Qt::TabFocus);
        column->addWidget(button, 0, Qt::AlignHCenter);
        return button;
    };
    const auto addActionButton = [this, &addButton](QVBoxLayout *column, const QString &id,
                                                   const QString &title, const QString &icon) {
        auto *button = addButton(column, id, title, icon);
        auto *action = m_shortcutActions->findChild<QAction *>(id);
        connect(button, &QPushButton::clicked, action, &QAction::trigger);
    };
    addActionButton(pageLayout, QStringLiteral("workspace_search"), QStringLiteral("搜索实体"), QStringLiteral("search"));

    const struct { const char *route; const char *icon; } entries[] = {
        {"home", "house"},
        {"mutiwork", "film"},
        {"shelf", "library_big"},
        {"actress", "venus"},
        {"actor", "mars"},
        {"chart", "chart_line"},
        {"graph", "share_2"},
        {"information", "scroll_text"},
        {"av", "tv"},
        {"inbox", "arrow_down_to_line"},
    };
    for (const auto &entry : entries) {
        const auto route = QString::fromLatin1(entry.route);
        auto *button = addButton(pageLayout, route, m_routeTitles.value(route),
                                 QString::fromLatin1(entry.icon));
        connect(button, &QPushButton::clicked, this, [this, route] { navigateTo(route); });
    }

    addActionButton(pageLayout, QStringLiteral("workspace_commands"), QStringLiteral("更多页面与命令"), QStringLiteral("menu"));
    addActionButton(layout, QStringLiteral("open_help"), QStringLiteral("帮助"), QStringLiteral("circle_question"));
    addActionButton(layout, QStringLiteral("open_settings"), QStringLiteral("设置"), QStringLiteral("settings"));
    return bar;
}

void MainWindow::registerApplicationActions()
{
    m_shortcutActions = new QActionGroup(this);
    QJsonObject userShortcuts;
    QFile shortcutFile(m_paths.shortcutsFile());
    if (shortcutFile.open(QIODevice::ReadOnly))
    {
        QJsonParseError parseError;
        const QJsonDocument document = QJsonDocument::fromJson(shortcutFile.readAll(), &parseError);
        if (parseError.error == QJsonParseError::NoError && document.isObject())
        {
            userShortcuts = document.object();
        }
    }
    const auto createAction = [this, &userShortcuts](const QString &id, const QString &title,
                                                   const QKeySequence &defaultKey)
    {
        auto *action = new QAction(title, m_shortcutActions);
        action->setObjectName(id);
        action->setData(QVariant::fromValue(defaultKey));
        const auto configured = userShortcuts.value(id);
        action->setShortcut(configured.isString()
                                ? QKeySequence::fromString(configured.toString(), QKeySequence::PortableText)
                                : defaultKey);
        return action;
    };
    const auto addAction = [this, &createAction](const QString &id, const QString &title,
                                                const QKeySequence &defaultKey, auto callback)
    {
        auto *action = createAction(id, title, defaultKey);
        connect(action, &QAction::triggered, this, callback);
        this->addAction(action);
        return action;
    };

    // Qt maps Ctrl shortcuts to Command on macOS.
    [[maybe_unused]] auto *search = addAction(
        QStringLiteral("workspace_search"), QStringLiteral("搜索实体"),
        QKeySequence(Qt::CTRL | Qt::Key_O), [this] { openWorkspaceSearch(false); });
    [[maybe_unused]] auto *commands = addAction(
        QStringLiteral("workspace_commands"), QStringLiteral("打开命令面板"),
        QKeySequence(Qt::CTRL | Qt::Key_P), [this] { openWorkspaceSearch(true); });
#ifdef Q_OS_MACOS
    auto *workspaceMenu = menuBar()->addMenu(QStringLiteral("工作区"));
    workspaceMenu->setObjectName(QStringLiteral("workspaceMenu"));
    workspaceMenu->addAction(search);
    workspaceMenu->addAction(commands);
#endif
    [[maybe_unused]] auto *settingsAction = addAction(
              QStringLiteral("open_settings"), QStringLiteral("打开设置"),
              QKeySequence(Qt::CTRL | Qt::Key_Comma), [this] {
                  openSettings();
                  m_globalSearch->hide();
              });
#ifdef Q_OS_MACOS
    workspaceMenu->addAction(settingsAction);
#endif
    addAction(QStringLiteral("toggle_left_sidebar"), QStringLiteral("打开或关闭左边栏"),
              QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_1),
              [this] { m_workspace->toggleSidebar(myads::SidebarSide::Left); });
    addAction(QStringLiteral("toggle_right_sidebar"), QStringLiteral("打开或关闭右边栏"),
              QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_2),
              [this] { m_workspace->toggleSidebar(myads::SidebarSide::Right); });
    [[maybe_unused]] auto *closeTab = addAction(
        QStringLiteral("close_tab"), QStringLiteral("关闭当前标签"),
        QKeySequence::Close, &MainWindow::closeCurrentTab);
#ifdef Q_OS_MACOS
    workspaceMenu->addAction(closeTab);
#endif
    const auto cycleActivePaneTab = [this](int offset) {
        auto *pane = m_workspace->activePane();
        if (!pane || pane->contentCount() < 2) return;
        const auto tabs = pane->contentIds();
        const auto current = tabs.indexOf(pane->currentContentId());
        const auto next = (current + offset + tabs.size()) % tabs.size();
        pane->setCurrentContentId(tabs.at(next));
    };
#ifdef Q_OS_MACOS
    // Command+Tab is reserved for switching apps.
    const QKeySequence nextTab(Qt::CTRL | Qt::SHIFT | Qt::Key_BracketRight);
    const QKeySequence previousTab(Qt::CTRL | Qt::SHIFT | Qt::Key_BracketLeft);
#else
    const QKeySequence nextTab(Qt::CTRL | Qt::Key_Tab);
    const QKeySequence previousTab(Qt::CTRL | Qt::SHIFT | Qt::Key_Tab);
#endif
    addAction(QStringLiteral("next_tab"), QStringLiteral("当前窗格：下一个标签页"),
              nextTab, [cycleActivePaneTab] { cycleActivePaneTab(1); });
    addAction(QStringLiteral("previous_tab"), QStringLiteral("当前窗格：上一个标签页"),
              previousTab, [cycleActivePaneTab] { cycleActivePaneTab(-1); });
    const auto cyclePane = [this](int offset) {
        const auto panes = m_workspace->visiblePaneIds();
        const auto *active = m_workspace->activePane();
        if (!active || panes.size() < 2) return;
        const auto current = panes.indexOf(active->paneId());
        const auto next = (current + offset + panes.size()) % panes.size();
        if (auto *pane = m_workspace->pane(panes.at(next))) {
            pane->focusTabBar();
            m_workspace->setActivePane(pane);
        }
    };
    addAction(QStringLiteral("next_pane"), QStringLiteral("切换到下一个窗格"),
              QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_Right), [cyclePane] { cyclePane(1); });
    addAction(QStringLiteral("previous_pane"), QStringLiteral("切换到上一个窗格"),
              QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_Left), [cyclePane] { cyclePane(-1); });

    addAction(QStringLiteral("add_masturbation_record"), QStringLiteral("添加自慰记录"),
              QStringLiteral("M"), [this]
              {
                  AddMasturbationDialog dialog(m_publicDatabase, m_privateDatabase, this);
                  connect(&dialog, &AddMasturbationDialog::recordAdded, this, [this] {
                      refreshStatisticsPages();
                  });
                  dialog.exec();
              });
    addAction(QStringLiteral("add_quick_work"), QStringLiteral("批量录入与补全作品"),
              QStringLiteral("W"), [this] {
                  AddQuickWorkDialog dialog(m_publicDatabase, *m_crawlerScheduler, m_themeService, this);
                  dialog.exec();
              });
    addAction(QStringLiteral("add_makelove_record"), QStringLiteral("添加做爱记录"),
              QStringLiteral("L"), [this]
              {
                  AddMakeLoveDialog dialog(m_privateDatabase, this);
                  connect(&dialog, &AddMakeLoveDialog::recordAdded, this, [this] {
                      refreshStatisticsPages();
                  });
                  dialog.exec();
              });
    addAction(QStringLiteral("add_sexual_rousal_record"), QStringLiteral("添加晨勃记录"),
              QStringLiteral("A"), [this]
              {
                  AddSexualArousalDialog dialog(m_privateDatabase, this);
                  connect(&dialog, &AddSexualArousalDialog::recordAdded, this, [this] {
                      refreshStatisticsPages();
                  });
                  dialog.exec();
              });
    addAction(QStringLiteral("open_help"), QStringLiteral("打开文档"), QStringLiteral("H"),
              [] { QDesktopServices::openUrl(utils::documentationUrl()); });
    addAction(QStringLiteral("allcapture"), QStringLiteral("全软件截图"),
              QStringLiteral("Shift+C"), [this] { captureWidget(this); });
    addAction(QStringLiteral("capture"), QStringLiteral("部分截图"), QStringLiteral("C"),
              [this] { captureWidget(partialCaptureTarget()); });
#ifdef Q_OS_MACOS
    installMacWorkspaceShortcuts(*this, *m_shortcutActions);
#endif
}

void MainWindow::openWorkspaceSearch(bool commands)
{
    const QSettings settings(m_paths.settingsFile(), QSettings::IniFormat);
    const auto routes = settings.value(QStringLiteral("workspace/recentPages")).toStringList();
    const auto pages = commands ? workspaceCommandPages() : QMap<QString, QString>{};
    QList<QPair<QString, QString>> recent;
    for (const auto &route : routes) {
        if (!isRecentWorkspaceRoute(route) || (commands ? !pages.contains(route) : !route.contains(u'/')))
            continue;
        const auto title = settings.value(QStringLiteral("workspace/titles/") + route,
                                          m_routeTitles.value(route)).toString();
        if (title.isEmpty()) continue;
        recent.append({route, title});
        if (recent.size() == recentWorkspaceLimit) break;
    }
    m_globalSearch->open(commands ? WorkspaceSearch::Mode::Commands : WorkspaceSearch::Mode::Entities,
                         std::move(recent));
}

QWidget *MainWindow::partialCaptureTarget()
{
    auto *pane = m_workspace->activePane();
    auto *page = pane ? pane->contentWidget(pane->currentContentId()) : nullptr;
    if (auto *detail = qobject_cast<PersonDetailPage *>(page))
        return detail->captureContent();
    return page;
}

void MainWindow::captureWidget(QWidget *widget)
{
    if (widget == nullptr || widget->size().isEmpty())
        return;

    const QPixmap screenshot = widget->grab();
    const QString path = QFileDialog::getSaveFileName(
        widget, QStringLiteral("保存截图"), QStringLiteral("screenshot.png"),
        QStringLiteral("PNG 图片 (*.png);;JPEG 图片 (*.jpg)"));
    if (path.isEmpty())
        return;
    if (!screenshot.save(path))
    {
        Toast::showWarning(this, QStringLiteral("截图保存失败。"), &m_themeService);
    }
}

void MainWindow::populateThemeSelector()
{
    const QList<QPair<ThemeId, QString>> themes = {
        {ThemeId::Light, QStringLiteral("亮色主题")},
        {ThemeId::Dark, QStringLiteral("暗色主题")},
        {ThemeId::Red, QStringLiteral("红色")},
        {ThemeId::Yellow, QStringLiteral("黄色")},
        {ThemeId::Green, QStringLiteral("绿色")},
        {ThemeId::Blue, QStringLiteral("蓝色")},
        {ThemeId::Purple, QStringLiteral("紫色")},
    };
    for (const auto &[theme, label] : themes)
    {
        m_themeSelector->addItem(label, static_cast<int>(theme));
        if (theme == m_themeService.current())
        {
            m_themeSelector->setCurrentIndex(m_themeSelector->count() - 1);
        }
    }
}

void MainWindow::changeTheme(int index)
{
    const ThemeId theme = static_cast<ThemeId>(m_themeSelector->itemData(index).toInt());
    if (!m_themeService.setTheme(theme, m_themeService.customPrimary()))
    {
        return;
    }

    AppSettings appSettings = settings::app();
    appSettings.themeId = ThemeService::toSettings(theme);
    settings::saveApp(appSettings);
}

void MainWindow::refreshStatisticsPages()
{
    for (auto *page : findChildren<StatisticsPage *>()) page->refresh();
}

void MainWindow::queueWorkspaceRefresh(WorkspaceChanges changes, qint64 workId)
{
    if (!m_pendingWorkspaceChanges)
        QTimer::singleShot(0, this, &MainWindow::refreshWorkspace);

    m_pendingWorkspaceChanges |= changes;
    if (changes.testFlag(WorksChanged)) {
        if (workId > 0)
            m_pendingChangedWorkIds.insert(workId);
        else
            m_refreshAllGraphWorks = true;
    }
}

void MainWindow::refreshWorkspace()
{
    const auto changes = std::exchange(m_pendingWorkspaceChanges, {});
    const auto changedWorkIds = std::exchange(m_pendingChangedWorkIds, {});
    const bool allWorks = std::exchange(m_refreshAllGraphWorks, false);
    const bool works = changes.testFlag(WorksChanged);
    const bool actresses = changes.testFlag(ActressesChanged);
    const bool actors = changes.testFlag(ActorsChanged);
    const bool people = actresses || actors;
    const bool tags = changes.testFlag(TagsChanged);
    const bool references = changes.testFlag(ReferencesChanged);
    const bool workFavorites = changes.testFlag(WorkFavoritesChanged);
    const bool actressFavorites = changes.testFlag(ActressFavoritesChanged);
    const bool workDataChanged = works || people || tags || references;
    if (tags || references) m_workFilter->refreshOptions(tags, references);

    // Closed routes are removed immediately, even when their widgets await deleteLater().
    const auto pages = m_routePages.values();
    for (const auto &page : pages) {
        auto *lazyPage = dynamic_cast<LazyWidget *>(page.data());
        if (!lazyPage || !lazyPage->isInitialized())
            continue;

        if (auto *list = qobject_cast<WorkPage *>(lazyPage)) {
            if (workDataChanged || workFavorites)
                list->refreshFromWorkspace(tags, references);
        } else if (auto *shelf = qobject_cast<ShelfPage *>(lazyPage)) {
            if (workDataChanged)
                shelf->refreshFromWorkspace(tags, references);
            else if (workFavorites)
                shelf->refreshCurrentFavorite();
        } else if (auto *list = qobject_cast<PersonPage *>(lazyPage)) {
            if (list->kind() == PersonKind::Actress ? actresses || actressFavorites : actors)
                list->refresh();
        } else if (auto *detail = qobject_cast<PersonDetailPage *>(lazyPage)) {
            if ((actresses || actressFavorites || works || tags || references) && detail->currentPersonId() > 0)
                detail->showPerson(detail->currentPersonId());
        } else if (auto *management = qobject_cast<ManagementPage *>(lazyPage)) {
            if (people || tags)
                management->refreshPersonSelectors();
            if (references)
                management->refreshReferences();
            if (works)
                management->refreshWorkLists();
        } else if (auto *statistics = qobject_cast<StatisticsPage *>(lazyPage)) {
            if (works || people || workFavorites || actressFavorites)
                statistics->refresh();
        }
    }

    if ((workDataChanged && m_timelineSource)
        && (qobject_cast<PersonDetailPage *>(m_timelineSource.data())
            || qobject_cast<PersonEditorPage *>(m_timelineSource.data())))
        updateTimeline();

    if (allWorks || people || tags || references || workFavorites || actressFavorites) {
        if (m_graphManager->isInitialized())
            m_graphManager->refresh();
        else
            m_graphManager->initialize();
    } else {
        for (qint64 workId : changedWorkIds)
            m_graphManager->scheduleRefreshWork(workId);
    }
}

void MainWindow::updateLocalGraphFocus()
{
    const auto *pane = m_workspace->activePane();
    if (!pane) return;
    const QString route = pane->currentContentId();
    const QString base = route.section(u'/', 0, 0);
    auto *page = pane->contentWidget(route);
    if (base == QStringLiteral("actress_detail") || base == QStringLiteral("modify_actress")
        || base == QStringLiteral("work_edit") || base == QStringLiteral("mutiwork")
        || base == QStringLiteral("actress")
        || qobject_cast<ShelfPage *>(page)) {
        m_localGraphSource = page;
        updateLocalGraph();
    }
}

void MainWindow::updateLocalGraph()
{
    if (!m_localGraphPage) return;
    if (!m_localGraphSource) {
        m_localGraphPage->showEmptyGraph();
        return;
    }

    if (auto *works = qobject_cast<WorkPage *>(m_localGraphSource.data())) {
        m_localGraphPage->showFilteredWorks(works->filteredWorkIds());
    } else if (auto *people = qobject_cast<PersonPage *>(m_localGraphSource.data())) {
        m_localGraphPage->showFilteredActresses(people->filteredPersonIds());
    } else if (auto *shelf = qobject_cast<ShelfPage *>(m_localGraphSource.data())) {
        if (shelf->isReturningToShelf()) return;
        const qint64 id = shelf->focusedWorkId();
        if (id > 0) m_localGraphPage->setEgoGraph(QStringLiteral("w%1").arg(id));
        else m_localGraphPage->showFilteredWorks(shelf->filteredWorkIds());
    } else if (auto *detail = qobject_cast<PersonDetailPage *>(m_localGraphSource.data())) {
        const auto id = detail->currentPersonId();
        if (id > 0) m_localGraphPage->setEgoGraph(QStringLiteral("a%1").arg(id));
        else m_localGraphPage->showEmptyGraph();
    } else {
        const QString route = m_localGraphSource->property("workspaceRoute").toString();
        const QChar prefix = route.startsWith(QStringLiteral("work_edit/")) ? u'w' : u'a';
        m_localGraphPage->setEgoGraph(prefix + route.section(u'/', 1));
    }
}

void MainWindow::updateTimelineFocus()
{
    const auto sourceInPane = [](const myads::PaneWidget *pane) -> QWidget * {
        if (!pane) return nullptr;
        const QString route = pane->currentContentId();
        auto *page = pane->contentWidget(route);
        if (route.startsWith(QStringLiteral("actress_detail/"))
            || route.startsWith(QStringLiteral("modify_actress/"))
            || qobject_cast<WorkPage *>(page) || qobject_cast<ShelfPage *>(page))
            return page;
        return nullptr;
    };

    auto *source = sourceInPane(m_workspace->activePane());
    // A restored layout can start with the timeline active beside a source page.
    if (!source && !m_timelineSource && m_timelinePage) {
        for (const auto *pane : m_workspace->panes()) {
            source = sourceInPane(pane);
            if (source) break;
        }
    }
    if (!source || source == m_timelineSource) return;
    m_timelineSource = source;
    updateTimeline();
}

void MainWindow::updateTimeline()
{
    if (!m_timelinePage) return;
    auto *source = m_timelineSource.data();
    if (auto *works = qobject_cast<WorkPage *>(source)) {
        m_timelinePage->setFilteredWorks(works->filteredWorks());
    } else if (auto *shelf = qobject_cast<ShelfPage *>(source)) {
        const qint64 workId = shelf->focusedWorkId();
        if (workId <= 0) {
            m_timelinePage->setFilteredWorks(shelf->filteredWorks());
            return;
        }
        QList<PersonDetails> actresses;
        const auto work = WorkRepository(m_publicDatabase).findDetailsById(workId);
        if (work) {
            const PersonRepository people(m_publicDatabase);
            actresses.reserve(work->actresses.size());
            for (const auto &actress : work->actresses) {
                if (auto details = people.findDetails(PersonKind::Actress, actress.id))
                    actresses.append(std::move(*details));
            }
        }
        m_timelinePage->setActresses(actresses);
    } else {
        auto *detail = qobject_cast<PersonDetailPage *>(source);
        const auto personId = detail ? detail->currentPersonId()
            : source ? source->property("workspaceRoute").toString().section(u'/', 1).toLongLong() : 0;
        const auto details = source
            ? PersonRepository(m_publicDatabase).findDetails(PersonKind::Actress, personId)
            : std::nullopt;
        if (details) m_timelinePage->setActress(*details);
        else m_timelinePage->setWorks({});
    }
}

void MainWindow::connectShelfPage(ShelfPage *shelf)
{
    connect(shelf, &ShelfPage::filteredWorksChanged, this, [this, shelf] {
        if (m_timelineSource == shelf) updateTimeline();
    });
    connect(shelf, &ShelfPage::focusedWorkChanged, this, [this, shelf] {
        if (m_timelineSource == shelf) updateTimeline();

        const auto workId = shelf->focusedWorkId();
        auto *pane = m_workspace->findPaneByContentId(actressSwitchRoute);
        if (workId <= 0 || !pane) return;
        auto *detail = static_cast<PersonDetailPage *>(pane->contentWidget(actressSwitchRoute));
        const auto work = WorkRepository(m_publicDatabase).findDetailsById(workId);
        if (!work) return;
        const auto actressId = work->actresses.isEmpty() ? 0 : work->actresses.first().id;
        if (detail->currentPersonId() == actressId) return;
        if (actressId == 0) detail->clearPerson();
        else if (!detail->showPerson(actressId, false)) return;
        if (m_localGraphSource == detail) updateLocalGraph();
        if (m_timelineSource == detail) updateTimeline();
    }, Qt::QueuedConnection);
    connect(shelf, &ShelfPage::graphContextChanged, this, [this, shelf] {
        if (m_localGraphSource == shelf) updateLocalGraph();
    }, Qt::QueuedConnection);
    connect(shelf, &ShelfPage::editRequested, this, &MainWindow::openWorkEditor);
    connect(shelf, &ShelfPage::workDeleted, this, [this](qint64 workId) {
        queueWorkspaceRefresh(WorksChanged, workId);
    });
    connect(shelf, &ShelfPage::favoriteChanged, this, [this] {
        queueWorkspaceRefresh(WorkFavoritesChanged);
    });
    connect(shelf, &ShelfPage::actressRequested, this, [this](qint64 personId) {
        auto *pane = m_workspace->findPaneByContentId(QStringLiteral("shelf"));
        navigateTo(QStringLiteral("actress_detail/") + QString::number(personId),
                   true, ActressOpenMode::Automatic, pane);
    });
    connect(shelf, &ShelfPage::actorRequested, this, [this](qint64 personId) {
        openPerson(PersonKind::Actor, personId);
    });
    connect(shelf, &ShelfPage::tagRequested, this, [this](qint64 tagId) {
        navigateTo(QStringLiteral("tag/") + QString::number(tagId));
    });
}

void MainWindow::startNextActressSync()
{
    if (m_backgroundActressSync == nullptr || m_backgroundActressSync->isBusy())
        return;
    while (!m_pendingActressSyncs.isEmpty())
    {
        const qint64 actressId = m_pendingActressSyncs.dequeue();
        if (m_backgroundActressSync->start(actressId))
            return;
        // No valid Japanese name or an unavailable endpoint: do not leave the
        // id permanently blocked from future enrichment attempts.
        m_pendingActressSyncIds.remove(actressId);
    }
}

void MainWindow::enqueueActressSyncs(const QList<qint64> &actressIds)
{
    for (const qint64 actressId : actressIds)
    {
        if (actressId > 0 && !m_pendingActressSyncIds.contains(actressId))
        {
            m_pendingActressSyncIds.insert(actressId);
            m_pendingActressSyncs.enqueue(actressId);
        }
    }
    startNextActressSync();
}

void MainWindow::showWorkInShelf(qint64 workId)
{
    if (workId > 0) navigateTo(QStringLiteral("work_detail/") + QString::number(workId));
}

void MainWindow::openPerson(PersonKind kind, qint64 id)
{
    if (id > 0) navigateTo((kind == PersonKind::Actress ? QStringLiteral("actress_detail/")
                         : QStringLiteral("actor_works/")) + QString::number(id));
}

void MainWindow::openWorkEditor(qint64 id)
{
    if (id > 0) navigateTo(QStringLiteral("work_edit/") + QString::number(id));
}

QString MainWindow::workspaceFile() const
{
    return QDir(QFileInfo(m_paths.settingsFile()).absolutePath()).filePath(QStringLiteral("workspace-v2.json"));
}

void MainWindow::closeCurrentTab()
{
    auto *pane = m_workspace->activePane();
    if (pane) pane->removeContent(pane->currentContentId());
}

void MainWindow::registerPage(const QString &menuTitle, const QString &routeName)
{
    // Registration is metadata only; each page is created on demand.
    m_routeTitles.insert(routeName, menuTitle);
    m_routePages.insert(routeName, nullptr);
}

QMap<QString, QString> MainWindow::workspaceCommandPages() const
{
    auto pages = m_routeTitles;
    pages.insert(QStringLiteral("setting"), QStringLiteral("设置"));
    pages.insert(QStringLiteral("open_help"), QStringLiteral("帮助"));
    pages.insert(QStringLiteral("open_github"), QStringLiteral("GitHub 项目仓库"));
    pages.insert(QStringLiteral("open_changelog"), QStringLiteral("版本记录"));
    pages.insert(SettingsDialog::workspaceCommands());
    pages.insert(QStringLiteral("work_filters"), m_workFilter->windowTitle());
    pages.insert(WorkspaceSearch::independentActressCommand, QStringLiteral("女演员详情（独立页）"));
    for (const auto &route : {QStringLiteral("fanart"),
                              QStringLiteral("modify_actress"), QStringLiteral("modify_actor")})
        pages.remove(route);
    return pages;
}

QWidget *MainWindow::ensurePage(const QString &routeName, qint64 switchActressId)
{
    if (routeName == QStringLiteral("work_filters")) return m_workFilter;
    if (m_routePages.value(routeName)) return m_routePages.value(routeName);
    const QString base = routeName.section('/', 0, 0);
    const QString argument = routeName.section('/', 1);
    const qint64 id = argument.toLongLong();
    QWidget *widget = nullptr;
    QString title = m_routeTitles.value(base);
    if (base == QStringLiteral("search") && !argument.isEmpty()) {
        auto *results = new WorkspaceSearch(m_publicDatabase, {});
        results->setFullResults();
        connect(results, &WorkspaceSearch::openRequested, this, [this](const QString &route) { navigateTo(route); });
        const auto query = QUrl::fromPercentEncoding(argument.toLatin1());
        results->search(query);
        widget = results;
        title = QStringLiteral("搜索 · ") + query;
    } else if (base == QStringLiteral("fanart_work") && id > 0) {
        const auto work = WorkRepository(m_publicDatabase).findById(id);
        if (!work) return nullptr;
        auto *fanart = static_cast<FanartBrowserPage *>(createPage(QStringLiteral("剧照"), QStringLiteral("fanart"), nullptr));
        fanart->showWork(id);
        widget = fanart;
        title = work->serialNumber + QStringLiteral(" · 剧照");
    } else if (base == QStringLiteral("actress_detail")) {
        const bool switching = routeName == actressSwitchRoute;
        const auto personId = switching ? switchActressId : argument.section(u'/', 0, 0).toLongLong();
        if (!switching && personId <= 0) return nullptr;
        auto *detail = static_cast<PersonDetailPage *>(createPage(title, base, nullptr));
        const bool loaded = personId > 0 && detail->showPerson(personId, false);
        if (!switching && !loaded) {
            delete detail;
            return nullptr;
        }
        widget = detail;
        if (!switching && !detail->currentPersonName().isEmpty()) title = detail->currentPersonName();
    } else if ((base == QStringLiteral("modify_actress") || base == QStringLiteral("modify_actor")) && id > 0) {
        const auto kind = base == QStringLiteral("modify_actress") ? PersonKind::Actress : PersonKind::Actor;
        const auto person = PersonRepository(m_publicDatabase).findDetails(kind, id);
        if (!person) return nullptr;
        widget = createPage(title, base, nullptr);
        if (kind == PersonKind::Actress) static_cast<ModifyActressPage *>(widget)->loadActress(id);
        else static_cast<ModifyActorPage *>(widget)->loadActor(id);
        if (!person->names.isEmpty()) {
            const auto &name = person->names.first();
            title = QStringLiteral("编辑 · ") + (name.chinese.isEmpty() ? name.japanese : name.chinese);
        }
    } else if (base == QStringLiteral("work_edit") && id > 0) {
        const auto work = WorkRepository(m_publicDatabase).findById(id);
        if (!work) return nullptr;
        auto *editor = static_cast<ManagementPage *>(createPage({}, base, nullptr));
        editor->loadWork(id);
        title = QStringLiteral("编辑 · ") + work->serialNumber;
        widget = editor;
    } else if ((base == QStringLiteral("tag") || shelfReferenceRoutes.contains(base)) && id > 0) {
        auto *shelf = new ShelfPage(m_publicDatabase, m_privateDatabase, m_themeService,
                                    m_paths.workCoverDirectory(), m_paths.fanartDirectory());
        connectShelfPage(shelf);
        if (base == QStringLiteral("tag")) shelf->filterByTag(id);
        else (shelf->*shelfReferenceRoutes.constFind(base)->apply)(id);
        title = QStringLiteral("分类作品 · %1 %2").arg(base).arg(id);
        widget = shelf;
    } else if (m_routeTitles.contains(base) && argument.isEmpty() && !base.contains(QStringLiteral("detail")) && !base.startsWith(QStringLiteral("modify"))) {
        widget = createPage(title, base, nullptr);
    }
    if (!widget) return nullptr;
    widget->setParent(this);
    widget->hide();
    const QMap<QString, QString> names{{QStringLiteral("actress"), QStringLiteral("ActressPage")},
        {QStringLiteral("actor"), QStringLiteral("ActorPage")}, {QStringLiteral("actress_detail"), QStringLiteral("ActressDetailPage")},
        {QStringLiteral("modify_actress"), QStringLiteral("ModifyActressPage")},
        {QStringLiteral("modify_actor"), QStringLiteral("ModifyActorPage")}};
    if (names.contains(base)) widget->setObjectName(names.value(base));
    if (widget->objectName().isEmpty()) widget->setObjectName(QString::fromLatin1(widget->metaObject()->className()).section(QStringLiteral("::"), -1));
    widget->setWindowTitle(title);
    widget->setProperty("workspaceRoute", routeName);
    m_routePages.insert(routeName, widget);
    return widget;
}

QWidget *MainWindow::createPage(const QString &menuTitle, const QString &routeName,
                                QWidget *parent)
{
    if (routeName == QStringLiteral("home"))
    {
        m_homePage = new HomePage(m_publicDatabase, m_paths.workCoverDirectory(), m_themeService, parent);
        connect(m_homePage, &HomePage::detailRequested, this, &MainWindow::showWorkInShelf);
        return m_homePage;
    }
    if (routeName == QStringLiteral("mutiwork"))
    {
        m_workPage = new WorkPage(m_publicDatabase, m_themeService,
                                  m_paths.workCoverDirectory(), m_privateDatabase,
                                  m_paths.fanartDirectory(),
                                  settings::crawler().coverFetchApiUrl, parent, m_workFilter);
        connect(m_workPage, &WorkPage::detailRequested, this, [this](qint64 workId) {
            showWorkInShelf(workId);
        });
        connect(m_workPage, &WorkPage::editRequested, this, [this](qint64 workId) {
            openWorkEditor(workId);
        });
        connect(m_workPage, &WorkPage::worksChanged, this,
                [this] { queueWorkspaceRefresh(WorksChanged); });
        auto *works = m_workPage.data();
        connect(works, &WorkPage::filteredWorksChanged, this, [this, works] {
            if (m_localGraphSource == works) updateLocalGraph();
            if (m_timelineSource == works) updateTimeline();
        });
        return works;
    }
    if (routeName == QStringLiteral("fanart"))
    {
        m_fanartBrowserPage = new FanartBrowserPage(
            m_publicDatabase, m_themeService, m_paths.fanartDirectory(),
            m_paths.workCoverDirectory(), settings::crawler().coverFetchApiUrl, parent);
        auto *fanart = m_fanartBrowserPage.data();
        connect(fanart, &FanartBrowserPage::closeRequested, this, [this, fanart] {
            const auto route = fanart->property("workspaceRoute").toString();
            if (auto *pane = m_workspace->findPaneByContentId(route)) pane->removeContent(route);
        });
        connect(m_fanartBrowserPage, &FanartBrowserPage::fanartChanged, this, [this](qint64 workId) {
            queueWorkspaceRefresh(WorksChanged, workId);
        });
        return m_fanartBrowserPage;
    }
    if (routeName == QStringLiteral("actress"))
    {
        m_actressPage = new ActressPage(m_publicDatabase, m_privateDatabase, m_themeService,
                                         m_paths.actressImageDirectory(), parent);
        connect(m_actressPage, &PersonPage::detailRequested, this,
                [this](PersonKind requestedKind, qint64 personId) {
                    openPerson(requestedKind, personId);
                });
        connect(m_actressPage, &PersonPage::editRequested, this, &MainWindow::openPersonEditor);
        connect(m_actressPage, &PersonPage::filteredPeopleChanged, this, [this] {
            if (m_localGraphSource == m_actressPage) updateLocalGraph();
        });
        return m_actressPage;
    }
    if (routeName == QStringLiteral("actor"))
    {
        m_actorPage = new ActorPage(m_publicDatabase, m_privateDatabase, m_themeService,
                                    m_paths.actorImageDirectory(), parent);
        connect(m_actorPage, &PersonPage::detailRequested, this,
                [this](PersonKind, qint64 personId) {
                    openPerson(PersonKind::Actor, personId);
                });
        connect(m_actorPage, &PersonPage::editRequested, this, &MainWindow::openPersonEditor);
        return m_actorPage;
    }
    if (routeName == QStringLiteral("actress_detail"))
    {
        auto *detail = new ActressDetailPage(
            m_publicDatabase, m_privateDatabase, m_themeService,
            m_paths.actressImageDirectory(), parent);
        connect(detail, &PersonDetailPage::editRequested, this, &MainWindow::openPersonEditor);
        connect(detail, &PersonDetailPage::workRequested, this, [this](qint64 workId) {
            showWorkInShelf(workId);
        });
        connect(detail, &PersonDetailPage::favoriteChanged, this, [this] {
            queueWorkspaceRefresh(ActressFavoritesChanged);
        });
        return detail;
    }
    if (routeName == QStringLiteral("modify_actress") ||
        routeName == QStringLiteral("modify_actor"))
    {
        const PersonKind kind = routeName == QStringLiteral("modify_actress")
            ? PersonKind::Actress : PersonKind::Actor;
        const QString imageDirectory = kind == PersonKind::Actress
            ? m_paths.actressImageDirectory() : m_paths.actorImageDirectory();
        PersonEditorPage *editor = nullptr;
        if (kind == PersonKind::Actress)
        {
            m_actressEditorPage = new ModifyActressPage(
                m_publicDatabase, m_themeService, imageDirectory, parent,
                m_paths.workCoverDirectory(), m_paths.actressNavButtonsFile());
            editor = m_actressEditorPage;
        }
        else
        {
            m_actorEditorPage = new ModifyActorPage(
                m_publicDatabase, m_themeService, imageDirectory, parent,
                m_paths.workCoverDirectory());
            editor = m_actorEditorPage;
        }
        editor->enableLayoutPersistence(QDir(m_paths.dataDirectory()).filePath(
            routeName + QStringLiteral("_workspace_layout.json")));
        connect(editor, &PersonEditorPage::personSaved, this, [this](PersonKind kind, qint64) {
            queueWorkspaceRefresh(kind == PersonKind::Actress ? ActressesChanged : ActorsChanged);
        });
        connect(editor, &PersonEditorPage::workLinkRequested, this, [this](qint64 workId) {
            showWorkInShelf(workId);
        });
        connect(editor, &PersonEditorPage::actressLinkRequested, this, [this](qint64 actressId) {
            openPerson(PersonKind::Actress, actressId);
        });
        connect(editor, &PersonEditorPage::personViewRequested, this,
                [this](PersonKind requestedKind, qint64 personId) {
                    openPerson(requestedKind, personId);
                });
        connect(editor, &PersonEditorPage::personDeleted, this,
                [this](PersonKind deletedKind, qint64 personId) {
                    const bool actress = deletedKind == PersonKind::Actress;
                    const QString listRoute = actress ? QStringLiteral("actress") : QStringLiteral("actor");
                    const QString detailRoute = listRoute + QStringLiteral("_detail/")
                                              + QString::number(personId);
                    if (auto *pane = m_workspace->findPaneByContentId(detailRoute))
                        pane->removeContent(detailRoute);
                    queueWorkspaceRefresh(actress ? ActressesChanged : ActorsChanged);
                    navigateTo(listRoute);
                });
        connect(editor, &PersonEditorPage::closeRequested, this, [this, editor] {
            auto route = editor->property("workspaceRoute").toString();
            if (auto *pane = m_workspace->findPaneByContentId(route)) pane->removeContent(route);
        });
        return editor;
    }
    std::optional<ManagementPage::Section> managementSection;
    if (routeName == QStringLiteral("work_edit"))
        managementSection = ManagementPage::Section::WorkEditor;
    else
        for (const auto &entry : managementRoutes)
            if (entry.route == routeName)
            {
                managementSection = entry.section;
                break;
            }
    if (managementSection)
    {
        auto *management = new ManagementPage(*managementSection, m_publicDatabase, m_themeService,
                                              *m_crawlerScheduler, m_privateDatabase,
                                              m_paths.workCoverDirectory(),
                                              m_paths.fanartDirectory(), settings::crawler().coverFetchApiUrl,
                                              parent,
                                              m_paths.actressImageDirectory(), m_paths.actorImageDirectory());
        connect(management, &ManagementPage::referencesChanged, this,
                [this] { queueWorkspaceRefresh(ReferencesChanged); });
        connect(management, &ManagementPage::tagsChanged, this,
                [this] { queueWorkspaceRefresh(TagsChanged); });
        connect(management, &ManagementPage::worksChanged, this,
                [this] { queueWorkspaceRefresh(WorksChanged); });
        connect(management, &ManagementPage::workChanged, this,
                [this](qint64 workId) { queueWorkspaceRefresh(WorksChanged, workId); });
        connect(management, &ManagementPage::peopleChanged, this, [this](PersonKind kind) {
            queueWorkspaceRefresh(kind == PersonKind::Actress ? ActressesChanged : ActorsChanged);
        });
        connect(management, &ManagementPage::actressesCreated, this,
                &MainWindow::enqueueActressSyncs);
        connect(management, &ManagementPage::workRequested, this, &MainWindow::showWorkInShelf);
        connect(management, &ManagementPage::workEditRequested, this, &MainWindow::openWorkEditor);
        connect(management, &ManagementPage::personEditRequested, this, &MainWindow::openPersonEditor);
        connect(management, &ManagementPage::personDetailRequested, this,
                [this](PersonKind kind, qint64 id) {
                    openPerson(kind, id);
                });
        connect(management, &ManagementPage::actressRequested, this, [this](qint64 actressId) {
            openPerson(PersonKind::Actress, actressId);
        });
        return management;
    }
    if (routeName == QStringLiteral("chart") || routeName == QStringLiteral("information"))
    {
        auto *statistics =
            new StatisticsPage(m_publicDatabase, m_privateDatabase, m_themeService,
                               m_paths.actressImageDirectory(), parent,
                               routeName == QStringLiteral("information") ? StatisticsPage::Panel::Information
                                                                           : StatisticsPage::Panel::Plots);
        if (routeName == QStringLiteral("information")) statistics->setObjectName(QStringLiteral("InformationPage"));
        connect(statistics, &StatisticsPage::actressDetailRequested, this,
                [this](qint64 actressId) {
                    openPerson(PersonKind::Actress, actressId);
                });
        connect(statistics, &StatisticsPage::actressEditRequested, this,
                [this](qint64 actressId) {
                    openPersonEditor(PersonKind::Actress, actressId);
                });
        return statistics;
    }
    if (routeName == QStringLiteral("timeline"))
    {
        auto *timeline = new ActressWorkTimeline(parent, m_paths.workCoverDirectory(),
                                                 m_paths.actressImageDirectory(), &m_themeService);
        connect(timeline, &ActressWorkTimeline::workRequested, this, &MainWindow::showWorkInShelf);
        m_timelinePage = timeline;
        updateTimeline();
        return timeline;
    }
    if (routeName == QStringLiteral("graph") || routeName == QStringLiteral("local_graph"))
    {
        auto *graph = new ForceDirectPage(m_themeService, *m_graphManager, parent,
                                          m_paths.actressImageDirectory(), m_paths.workCoverDirectory());
        connect(graph, &ForceDirectPage::workRequested, this, [this](qint64 workId) {
            navigateTo(QStringLiteral("work_detail/%1").arg(workId), true,
                       ActressOpenMode::Automatic, nullptr, ShelfFilterPolicy::ClearIfExcluded);
        }, Qt::QueuedConnection);
        connect(graph, &ForceDirectPage::actressRequested, this, [this](qint64 actressId) {
            openPerson(PersonKind::Actress, actressId);
        });
        if (routeName == QStringLiteral("local_graph")) {
            graph->setFavoriteFilterToggleVisible(false);
            graph->setObjectName(QStringLiteral("LocalGraphPage"));
            m_localGraphPage = graph;
            updateLocalGraph();
        }
        return graph;
    }
    if (routeName == QStringLiteral("shelf"))
    {
        m_shelfPage = new ShelfPage(m_publicDatabase, m_privateDatabase, m_themeService,
                                    m_paths.workCoverDirectory(),
                                    m_paths.fanartDirectory(), parent, m_workFilter);
        connectShelfPage(m_shelfPage);
        return m_shelfPage;
    }
    if (routeName == QStringLiteral("av"))
    {
        return new AvPage(parent);
    }
    if (routeName == QStringLiteral("inbox"))
    {
        return new InboxPage(*m_crawlerScheduler, *m_crawlerPersistence, parent);
    }
    return new PlaceholderPage(menuTitle, routeName, parent);
}

void MainWindow::navigateTo(const QString &routeName, bool recordRecent, ActressOpenMode actressMode,
                            myads::PaneWidget *preferredPane, ShelfFilterPolicy shelfFilter)
{
    if (routeName == QStringLiteral("setting")) {
        openSettings();
        return;
    }
    if (routeName == QStringLiteral("open_help")) {
        m_shortcutActions->findChild<QAction *>(routeName)->trigger();
        return;
    }
    if (routeName == QStringLiteral("open_github")) {
        QDesktopServices::openUrl(utils::projectRepositoryUrl());
        return;
    }
    if (routeName == QStringLiteral("open_changelog")) {
        QDesktopServices::openUrl(utils::changelogUrl());
        return;
    }
    const auto settingsCommandPrefix = SettingsDialog::workspaceCommandPrefix;
    if (routeName.startsWith(settingsCommandPrefix)) {
        m_globalSearch->hide();
        openSettings();
        m_settingsDialog->runWorkspaceCommand(routeName.sliced(settingsCommandPrefix.size()));
        return;
    }
    const QPointer<myads::PaneWidget> destination = preferredPane ? preferredPane : m_workspace->activePane();
    const auto base = routeName.section(u'/', 0, 0);
    const auto argument = routeName.section(u'/', 1);
    const auto reference = shelfReferenceRoutes.constFind(base);
    const bool filtersReference = reference != shelfReferenceRoutes.cend();
    const bool filtersDirector = base == QStringLiteral("director");
    const bool filtersActor = base == QStringLiteral("actor_works");
    const bool opensWork = base == QStringLiteral("work_detail");
    const bool opensShelf = opensWork || filtersReference || filtersDirector || filtersActor;
    std::optional<Work> work;
    std::optional<PersonSummary> actor;
    QString recentTitle;
    QString director;
    qint64 referenceId = 0;
    if (opensWork) {
        const auto workId = argument.toLongLong();
        if (workId <= 0) return;
        work = WorkRepository(m_publicDatabase).findById(workId);
        if (!work) return;
        recentTitle = work->serialNumber;
    } else if (filtersReference) {
        referenceId = argument.toLongLong();
        if (referenceId <= 0) return;
        const WorkRepository works(m_publicDatabase);
        const auto options = (works.*reference->options)();
        const auto option = std::find_if(options.cbegin(), options.cend(), [referenceId](const NamedIdOption &value) {
            return value.id == referenceId;
        });
        if (option == options.cend()) return;
        recentTitle = reference->title + QStringLiteral(" · ") + option->name;
    } else if (filtersDirector) {
        director = QUrl::fromPercentEncoding(argument.toLatin1());
        if (director.trimmed().isEmpty()) return;
        recentTitle = QStringLiteral("导演 · ") + director;
    } else if (filtersActor) {
        const auto actorId = argument.toLongLong();
        if (actorId <= 0) return;
        PersonSearch query;
        query.kind = PersonKind::Actor;
        query.includedIds = {actorId};
        query.restrictToIncludedIds = true;
        query.limit = 1;
        const auto matches = PersonRepository(m_publicDatabase).search(query);
        if (matches.isEmpty()) return;
        actor = matches.first();
        recentTitle = QStringLiteral("男演员 · ") + actor->name;
    }
    auto pageRoute = opensShelf ? QStringLiteral("shelf") : routeName;
    qint64 switchActressId = 0;
    if (routeName == QStringLiteral("actress_detail")) {
        pageRoute = actressSwitchRoute;
    } else if (recordRecent && base == QStringLiteral("actress_detail")) {
        const auto actressId = argument.toLongLong();
        if (actressId <= 0) return;
        if (actressMode == ActressOpenMode::Switching
            || (actressMode == ActressOpenMode::Automatic && m_workspace->findPaneByContentId(actressSwitchRoute))) {
            pageRoute = actressSwitchRoute;
            switchActressId = actressId;
        } else if (m_workspace->findPaneByContentId(pageRoute)) {
            pageRoute += u'/' + QUuid::createUuid().toString(QUuid::WithoutBraces);
        }
    }
    auto *widget = ensurePage(pageRoute);
    if (!widget) return;
    if (work) {
        auto *shelf = static_cast<ShelfPage *>(widget);
        if (!shelf->containsWork(work->id)) {
            if (shelfFilter == ShelfFilterPolicy::Keep) return;
            shelf->resetWorkFilters();
            if (!shelf->containsWork(work->id)) return;
        }
    }
    auto *pane = m_workspace->findPaneByContentId(pageRoute);
    if (switchActressId > 0) {
        auto *detail = static_cast<PersonDetailPage *>(widget);
        if (detail->currentPersonId() != switchActressId) {
            if (!detail->showPerson(switchActressId)) return;
            if (m_localGraphSource == detail) updateLocalGraph();
            if (m_timelineSource == detail) updateTimeline();
        }
        recentTitle = detail->currentPersonName();
    }
    if (!pane) {
        pane = destination.data();
        if (!pane) pane = m_workspace->rootPane();
        auto config = myads::ContentConfig(pageRoute).setWidget(widget).setWindowTitle(widget->windowTitle());
        if (widget == m_workFilter) config.setClosePolicy(myads::ClosePolicy::KeepAlive);
        if (!m_workspace->fillPane(pane, config)) return;
    }
    pane->setCurrentContentId(pageRoute);
    m_workspace->setActivePane(pane);
    if (m_workspace->isSidebar(pane)) pane->focusTabBar();
    if (opensShelf) {
        auto *shelf = static_cast<ShelfPage *>(widget);
        if (work && !shelf->showWork(work->id))
            return;
        if (filtersReference) (shelf->*reference->apply)(referenceId);
        if (filtersDirector) shelf->filterByDirector(director);
        if (actor) shelf->filterByActor(actor->id, actor->name);
    }
    if (recordRecent && isRecentWorkspaceRoute(routeName)) {
        QSettings settings(m_paths.settingsFile(), QSettings::IniFormat);
        auto recent = settings.value(QStringLiteral("workspace/recentPages")).toStringList();
        recent.removeIf([&routeName](const QString &route) {
            return route == routeName || !isRecentWorkspaceRoute(route);
        });
        recent.prepend(routeName);
        qsizetype entities = 0;
        qsizetype pages = 0;
        recent.removeIf([&](const QString &route) {
            auto &count = route.contains(u'/') ? entities : pages;
            if (++count <= recentWorkspaceLimit) return false;
            settings.remove(QStringLiteral("workspace/titles/") + route);
            return true;
        });
        settings.setValue(QStringLiteral("workspace/recentPages"), recent);
        settings.setValue(QStringLiteral("workspace/titles/") + routeName,
                          recentTitle.isEmpty() ? widget->windowTitle() : recentTitle);
    }
    if (auto *statistics = qobject_cast<StatisticsPage *>(widget)) statistics->refresh();
}

void MainWindow::openSettings()
{
    if (!m_settingsDialog) {
        auto *settings = new SettingsDialog(m_themeService, *m_shortcutActions,
                                            m_publicDatabase, m_privateDatabase, m_paths, this);
        m_settingsDialog = settings;
        connect(settings, &SettingsDialog::navigationBarVisibilityChanged,
                this, [this](bool visible) {
            m_navigationBar->setVisible(visible);
            m_navigationHint->setVisible(visible);
        });
        connect(settings, &SettingsDialog::sidebarAutoCollapseChanged,
                m_workspace, &myads::WorkspaceWidget::setSidebarAutoCollapse);
        connect(settings, &SettingsDialog::worksChanged, this,
                [this] { queueWorkspaceRefresh(WorksChanged); });
        connect(settings, &SettingsDialog::quickWorkRequested, this,
                [this, settings](const QStringList &serials) {
            AddQuickWorkDialog dialog(m_publicDatabase, *m_crawlerScheduler, m_themeService, settings);
            dialog.loadSerials(serials);
            dialog.exec();
        });
        connect(settings, &SettingsDialog::referencesChanged, this,
                [this] { queueWorkspaceRefresh(ReferencesChanged); });
        connect(settings, &SettingsDialog::tagsChanged, this,
                [this] { queueWorkspaceRefresh(TagsChanged); });
        connect(settings, &SettingsDialog::actressesChanged, this,
                [this] { queueWorkspaceRefresh(ActressesChanged); });
        connect(settings, &SettingsDialog::actorsChanged, this,
                [this] { queueWorkspaceRefresh(ActorsChanged); });
        m_themeSelector = settings->themeSelector();
        populateThemeSelector();
        connect(m_themeSelector, &QComboBox::currentIndexChanged, this, &MainWindow::changeTheme);
    }
    if (!m_settingsDialog->isVisible()) m_settingsDialog->open();
    m_settingsDialog->raise();
    m_settingsDialog->activateWindow();
}

void MainWindow::openPersonEditor(PersonKind kind, qint64 personId)
{
    if (personId > 0) navigateTo((kind == PersonKind::Actress ? QStringLiteral("modify_actress/")
        : QStringLiteral("modify_actor/")) + QString::number(personId));
}

void MainWindow::saveWindowState() const
{
    SettingsStore store(m_paths.settingsFile());
    store.setValue(QStringLiteral("window/maximized"), isMaximized());
    if (auto *pane = m_workspace->activePane()) store.setValue(QStringLiteral("workspace/activeRoute"), pane->currentContentId());
    QDir().mkpath(QFileInfo(workspaceFile()).absolutePath());
    QString error;
    if (!m_workspace->saveLayout(workspaceFile(), [](const myads::PaneWidget *pane, const QString &route) {
            QJsonObject descriptor{{QStringLiteral("content_id"), route}};
            if (route == actressSwitchRoute) {
                auto *detail = qobject_cast<PersonDetailPage *>(pane->contentWidget(route));
                if (detail) descriptor.insert(QStringLiteral("actress_id"), QString::number(detail->currentPersonId()));
            }
            return descriptor;
        }, {}, &error)) qWarning() << "Workspace save:" << error;
}

} // namespace darkeye
