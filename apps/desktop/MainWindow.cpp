#include "MainWindow.h"
#include "settings/SettingsStore.h"
#include "utils/Documentation.h"

#include "ui/pages/DashboardPage.h"
#include "ui/pages/ActorDetailPage.h"
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
#include "dvd_scene/DvdSceneWidget.h"
#include "ui/pages/PlaceholderPage.h"
#include "ui/pages/WorkPage.h"
#include "crawler/CrawlerScheduler.h"
#include "http/LocalApiServer.h"
#include "services/ActressSyncService.h"
#include "darkeye_ui/base/LazyWidget.h"
#include "graph/GraphManager.h"
#include "darkeye_ui/components/Sidebar.h"
#include "ui/pages/ModifyActorPage.h"
#include "ui/pages/ModifyActressPage.h"
#include "ui/pages/PersonEditorPage.h"
#include "ui/dialogs/AddMakeLoveDialog.h"
#include "ui/dialogs/AddMasturbationDialog.h"
#include "ui/dialogs/AddSexualArousalDialog.h"
#include "ui/dialogs/AddQuickWorkDialog.h"
#include "darkeye_ui/components/ToastNotification.h"

#include <QAction>
#include <QCloseEvent>
#include <QComboBox>
#include <QDesktopServices>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeySequence>
#include <QLineEdit>
#include <QPixmap>
#include <QStackedWidget>
#include <QTimer>
#include <QUrl>

#ifdef Q_OS_WIN
#define NOMINMAX
#include <Windows.h>
#include <psapi.h>
#elif defined(Q_OS_MACOS)
#include <mach/mach.h>
#include <sys/resource.h>
#include <QThread>
#endif

namespace darkeye
{

namespace
{

QString formatMemory(qint64 bytes)
{
    return QStringLiteral("%1 MiB").arg(bytes / 1024.0 / 1024.0, 0, 'f', 1);
}

struct ProcessMemoryUsage
{
    QString shared;
    QString privateWorkingSet;
};

ProcessMemoryUsage processMemoryUsage()
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
        return {formatMemory(sharedWorkingSet), formatMemory(privateWorkingSet)};
    }
#elif defined(Q_OS_MACOS)
    task_vm_info_data_t counters{};
    mach_msg_type_number_t count = TASK_VM_INFO_COUNT;
    if (task_info(mach_task_self(), TASK_VM_INFO,
                  reinterpret_cast<task_info_t>(&counters), &count) == KERN_SUCCESS)
    {
        // Darwin splits resident pages into anonymous (internal) and file-backed
        // (external) memory. Use these for the private/shared display; external
        // pages can be shared but are not necessarily mapped by another process.
        // Compressed and swapped-out pages are excluded, as with a working set.
        return {formatMemory(static_cast<qint64>(counters.external)),
                formatMemory(static_cast<qint64>(counters.internal))};
    }
#endif
    return {QStringLiteral("N/A"), QStringLiteral("N/A")};
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
    connect(m_crawlerPersistence.get(), &CrawlerPersistenceService::worksChanged,
            this, &MainWindow::refreshWorksAfterCapture);
    connect(m_crawlerPersistence.get(), &CrawlerPersistenceService::actressesChanged,
            this, [this] { refreshPeopleAfterCapture(PersonKind::Actress); });
    connect(m_crawlerPersistence.get(), &CrawlerPersistenceService::actorsChanged,
            this, [this] { refreshPeopleAfterCapture(PersonKind::Actor); });
    connect(m_backgroundActressSync.get(), &ActressSyncService::actressesChanged,
            this, [this] { refreshPeopleAfterCapture(PersonKind::Actress); });
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
    const ProcessMemoryUsage memoryUsage = processMemoryUsage();
    setWindowTitle(QStringLiteral("暗之眼 V%1 | CPU %2 | 公共内存 %3 | 私有内存 %4")
                       .arg(QStringLiteral(DARKEYE_VERSION), processCpuUsage(),
                            memoryUsage.shared, memoryUsage.privateWorkingSet));
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

void MainWindow::preloadShelfPage(bool warmGraphics)
{
    auto *shelf = static_cast<ShelfPage *>(ensurePage(QStringLiteral("shelf")));
    if (shelf == nullptr || (!warmGraphics && shelf->isInitialized()))
        return;
    QElapsedTimer timer;
    timer.start();
    shelf->initialize();
    if (warmGraphics && isVisible()) {
        // A hidden page has no GPU resources yet. Render the actual shelf with
        // the visible window's QRhi, below the current page, before dismissing
        // the startup splash. A separate offscreen QRhi would be discarded on
        // the first navigation and would leave the same first-frame stall.
        auto *stack = qobject_cast<QStackedWidget *>(shelf->parentWidget());
        auto *current = stack ? stack->currentWidget() : nullptr;
        auto *scene = shelf->findChild<dvd_scene::DvdSceneWidget *>();
        if (current && current != shelf && scene) {
            shelf->setGeometry(stack->contentsRect());
            shelf->show();
            shelf->lower();
            shelf->layout()->activate();
            scene->layout()->activate();
            // Let Qt process the new widget's layout and backing-store updates
            // before requesting a readback on the prewarmed window.
            QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
            if (scene->renderWidget()->grabFramebuffer().isNull())
                qWarning() << "Shelf graphics prewarm did not render a frame";
            shelf->hide();
        }
    }
    qInfo() << "Shelf page preloaded in" << timer.elapsed() << "ms";
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

void MainWindow::closeEvent(QCloseEvent *event)
{
    saveWindowState();
    QMainWindow::closeEvent(event);
}

void MainWindow::buildUi()
{
    auto *centralWidget = new QWidget(this);
    auto *layout = new QHBoxLayout(centralWidget);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    const QList<SidebarMenuDefinition> menus = {
        {QStringLiteral("home"), QStringLiteral("首页"), QStringLiteral("house")},
        {QStringLiteral("work"), QStringLiteral("作品"), QStringLiteral("film")},
        {QStringLiteral("actress"), QStringLiteral("女演员"), QStringLiteral("venus")},
        {QStringLiteral("actor"), QStringLiteral("男演员"), QStringLiteral("mars")},
        {QStringLiteral("database"), QStringLiteral("管理"), QStringLiteral("database")},
        {QStringLiteral("chart"), QStringLiteral("统计"), QStringLiteral("chart_line")},
        {QStringLiteral("graph"), QStringLiteral("关系图"), QStringLiteral("share_2")},
        {QStringLiteral("shelf"), QStringLiteral("书架"), QStringLiteral("library_big")},
        {QStringLiteral("av"), QStringLiteral("暗黑界"), QStringLiteral("scroll_text")},
        {QStringLiteral("bell"), QStringLiteral("通知"), QStringLiteral("bell")},
    };
    m_sidebar = new Sidebar(menus, &m_themeService, centralWidget);

    m_pages = new QStackedWidget(centralWidget);
    m_pages->setObjectName(QStringLiteral("mainPages"));

    layout->addWidget(m_sidebar);
    layout->addWidget(m_pages, 1);
    setCentralWidget(centralWidget);

    registerPage(QStringLiteral("首页"), QStringLiteral("home"));
    registerPage(QStringLiteral("仪表盘"), QStringLiteral("test_page"));
    registerPage(QStringLiteral("作品"), QStringLiteral("mutiwork"));
    registerPage(QStringLiteral("剧照浏览"), QStringLiteral("fanart"));
    registerPage(QStringLiteral("女演员"), QStringLiteral("actress"));
    registerPage(QStringLiteral("男演员"), QStringLiteral("actor"));
    registerPage(QStringLiteral("女演员详情"), QStringLiteral("actress_detail"));
    registerPage(QStringLiteral("男演员详情"), QStringLiteral("actor_detail"));
    registerPage(QStringLiteral("修改女演员"), QStringLiteral("modify_actress"));
    registerPage(QStringLiteral("修改男演员"), QStringLiteral("modify_actor"));
    registerPage(QStringLiteral("管理"), QStringLiteral("database"));
    registerPage(QStringLiteral("统计"), QStringLiteral("chart"));
    registerPage(QStringLiteral("关系图"), QStringLiteral("graph"));
    registerPage(QStringLiteral("书架"), QStringLiteral("shelf"));
    registerPage(QStringLiteral("暗黑界"), QStringLiteral("av"));
    registerPage(QStringLiteral("通知"), QStringLiteral("inbox"));
    registerPage(QStringLiteral("设置"), QStringLiteral("setting"));

    const QMap<QString, QString> menuRoutes = {
        {QStringLiteral("home"), QStringLiteral("home")},
        {QStringLiteral("work"), QStringLiteral("mutiwork")},
        {QStringLiteral("actress"), QStringLiteral("actress")},
        {QStringLiteral("actor"), QStringLiteral("actor")},
        {QStringLiteral("database"), QStringLiteral("database")},
        {QStringLiteral("chart"), QStringLiteral("chart")},
        {QStringLiteral("graph"), QStringLiteral("graph")},
        {QStringLiteral("shelf"), QStringLiteral("shelf")},
        {QStringLiteral("av"), QStringLiteral("av")},
        {QStringLiteral("bell"), QStringLiteral("inbox")},
        {QStringLiteral("setting"), QStringLiteral("setting")},
    };
    connect(m_sidebar, &Sidebar::itemClicked, this,
            [this, menuRoutes](const QString &menuId)
            {
                if (menuId == QStringLiteral("help"))
                {
                    QDesktopServices::openUrl(utils::documentationUrl());
                    return;
                }
                const QString route = menuRoutes.value(menuId);
                if (m_routeIndexes.contains(route))
                    navigateTo(route);
            });
    connect(m_sidebar, &Sidebar::backwardClicked, this, &MainWindow::navigateBackward);
    connect(m_sidebar, &Sidebar::forwardClicked, this, &MainWindow::navigateForward);
    registerApplicationActions();
    // The standalone C++ build opens directly into the filtered work library.
    navigateTo(QStringLiteral("mutiwork"));
}

void MainWindow::registerApplicationActions()
{
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
    const auto shortcut = [&userShortcuts](const QString &id, const QString &fallback)
    {
        const QString configured = userShortcuts.value(id).toString().trimmed();
        return QKeySequence(configured.isEmpty() ? fallback : configured);
    };
    const auto addAction = [this, &shortcut](const QString &id, const QString &title,
                                             const QString &fallback, auto callback)
    {
        auto *action = new QAction(title, this);
        action->setObjectName(id);
        action->setShortcut(shortcut(id, fallback));
        connect(action, &QAction::triggered, this, callback);
        this->addAction(action);
    };

    addAction(QStringLiteral("add_masturbation_record"), QStringLiteral("添加自慰记录"),
              QStringLiteral("M"), [this]
              {
                  AddMasturbationDialog dialog(m_publicDatabase, m_privateDatabase, this);
                  connect(&dialog, &AddMasturbationDialog::recordAdded, this, [this] {
                      if (m_statisticsPage)
                          m_statisticsPage->refresh();
                  });
                  dialog.exec();
              });
    addAction(QStringLiteral("add_quick_work"), QStringLiteral("快速添加番号"),
              QStringLiteral("W"), [this] {
                  AddQuickWorkDialog dialog(m_publicDatabase, *m_crawlerScheduler, m_themeService, this);
                  dialog.exec();
              });
    addAction(QStringLiteral("add_makelove_record"), QStringLiteral("添加做爱记录"),
              QStringLiteral("L"), [this]
              {
                  AddMakeLoveDialog dialog(m_privateDatabase, this);
                  connect(&dialog, &AddMakeLoveDialog::recordAdded, this, [this] {
                      if (m_statisticsPage)
                          m_statisticsPage->refresh();
                  });
                  dialog.exec();
              });
    addAction(QStringLiteral("add_sexual_rousal_record"), QStringLiteral("添加晨勃记录"),
              QStringLiteral("A"), [this]
              {
                  AddSexualArousalDialog dialog(m_privateDatabase, this);
                  connect(&dialog, &AddSexualArousalDialog::recordAdded, this, [this] {
                      if (m_statisticsPage)
                          m_statisticsPage->refresh();
                  });
                  dialog.exec();
              });
    addAction(QStringLiteral("open_help"), QStringLiteral("打开文档"), QStringLiteral("H"),
              [] { QDesktopServices::openUrl(utils::documentationUrl()); });
    addAction(QStringLiteral("search"), QStringLiteral("搜索"), QStringLiteral("Ctrl+F"), [this] {
        focusWorkSearch();
    });
    addAction(QStringLiteral("allcapture"), QStringLiteral("全软件截图"),
              QStringLiteral("Shift+C"), [this] { captureWidget(this); });
    addAction(QStringLiteral("capture"), QStringLiteral("部分截图"), QStringLiteral("C"),
              [this] { captureWidget(partialCaptureTarget()); });
}

void MainWindow::focusWorkSearch()
{
    auto *workPage = static_cast<WorkPage *>(ensurePage(QStringLiteral("mutiwork")));
    if (workPage == nullptr)
        return;
    navigateTo(QStringLiteral("mutiwork"));
    workPage->focusSearch();
}

QWidget *MainWindow::partialCaptureTarget()
{
    const QString route = m_history.value(m_historyIndex);
    if (route == QStringLiteral("mutiwork") && m_workPage != nullptr)
        return m_workPage->captureContent();
    if (route == QStringLiteral("actress") && m_actressPage != nullptr)
        return m_actressPage->captureContent();
    if (route == QStringLiteral("actress_detail") && m_actressDetailPage != nullptr)
        return m_actressDetailPage->captureContent();
    if (route == QStringLiteral("chart") && m_statisticsPage != nullptr)
        return m_statisticsPage->captureContent();
    if (route == QStringLiteral("home"))
        return m_pages->currentWidget();
    return nullptr;
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

void MainWindow::refreshWorksAfterCapture()
{
    if (m_workPage) m_workPage->refresh();
    if (m_shelfPage) m_shelfPage->refreshAfterCapture();
    // Work relations also appear in the currently displayed person's timeline.
    for (PersonDetailPage *detail : {static_cast<PersonDetailPage *>(m_actressDetailPage),
                                   static_cast<PersonDetailPage *>(m_actorDetailPage)})
        if (detail && detail->currentPersonId() > 0)
            detail->showPerson(detail->currentPersonId());
    refreshRelationshipGraph();
}

void MainWindow::refreshPeopleAfterCapture(PersonKind kind)
{
    PersonPage *list = kind == PersonKind::Actress
        ? static_cast<PersonPage *>(m_actressPage) : static_cast<PersonPage *>(m_actorPage);
    if (list) list->refresh();
    PersonDetailPage *detail = kind == PersonKind::Actress
        ? static_cast<PersonDetailPage *>(m_actressDetailPage)
        : static_cast<PersonDetailPage *>(m_actorDetailPage);
    if (detail && detail->currentPersonId() > 0)
        detail->showPerson(detail->currentPersonId());
    if (m_managementPage) m_managementPage->refreshPersonSelectors();
    refreshRelationshipGraph();
}

void MainWindow::refreshRelationshipGraph()
{
    if (m_graphManager == nullptr) return;
    QString error;
    (void)m_graphManager->refresh(&error);
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

void MainWindow::refreshRelationshipGraphWork(qint64 workId)
{
    if (m_graphManager == nullptr) return;
    m_graphManager->scheduleRefreshWork(workId);
}

void MainWindow::showWorkInShelf(qint64 workId)
{
    if (workId <= 0) return;
    auto *shelf = static_cast<ShelfPage *>(ensurePage(QStringLiteral("shelf")));
    if (shelf == nullptr) return;
    navigateTo(QStringLiteral("shelf"));
    if (!shelf->showWork(workId)) {
        QTimer::singleShot(0, shelf, [shelf, workId] { shelf->showWork(workId); });
    }
}

void MainWindow::registerPage(const QString &menuTitle, const QString &routeName)
{
    // Match Python Router.register(factory): registration stores metadata only.
    // The page is added to the stack on its first navigation.
    m_routeIndexes.insert(routeName, -1);
    m_routeTitles.insert(routeName, menuTitle);
    m_routePages.insert(routeName, nullptr);
}

QWidget *MainWindow::ensurePage(const QString &routeName)
{
    if (!m_routePages.contains(routeName))
    {
        return nullptr;
    }
    QWidget *widget = m_routePages.value(routeName);
    if (widget == nullptr)
    {
        const QString menuTitle = m_routeTitles.value(routeName);
        widget = createPage(menuTitle, routeName, m_pages);
        if (widget == nullptr)
            return nullptr;
        static const QMap<QString, QString> routeObjectNames = {
            {QStringLiteral("actress"), QStringLiteral("ActressPage")},
            {QStringLiteral("actor"), QStringLiteral("ActorPage")},
            {QStringLiteral("actress_detail"), QStringLiteral("ActressDetailPage")},
            {QStringLiteral("actor_detail"), QStringLiteral("ActorDetailPage")},
        };
        if (routeObjectNames.contains(routeName))
        {
            widget->setObjectName(routeObjectNames.value(routeName));
        }
        else if (widget->objectName().isEmpty())
        {
            const QString className = QString::fromLatin1(widget->metaObject()->className());
            widget->setObjectName(className.section(QStringLiteral("::"), -1));
        }

        const int index = m_pages->addWidget(widget);
        m_routeIndexes[routeName] = index;
        m_routePages[routeName] = widget;
    }
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
    if (routeName == QStringLiteral("test_page"))
    {
        m_dashboardPage = new DashboardPage(m_publicDatabase, m_privateDatabase, parent);
        return m_dashboardPage;
    }
    if (routeName == QStringLiteral("mutiwork"))
    {
        m_workPage = new WorkPage(m_publicDatabase, m_themeService,
                                  m_paths.workCoverDirectory(), m_privateDatabase,
                                  m_paths.fanartDirectory(),
                                  settings::crawler().coverFetchApiUrl, parent);
        connect(m_workPage, &WorkPage::detailRequested, this, [this](qint64 workId) {
            showWorkInShelf(workId);
        });
        connect(m_workPage, &WorkPage::editRequested, this, [this](qint64 workId) {
            auto *management =
                static_cast<ManagementPage *>(ensurePage(QStringLiteral("database")));
            if (management->loadWork(workId))
                navigateTo(QStringLiteral("database"));
        });
        connect(m_workPage, &WorkPage::worksChanged, this,
                [this] { refreshRelationshipGraph(); });
        return m_workPage;
    }
    if (routeName == QStringLiteral("fanart"))
    {
        m_fanartBrowserPage = new FanartBrowserPage(
            m_publicDatabase, m_themeService, m_paths.fanartDirectory(),
            m_paths.workCoverDirectory(), settings::crawler().coverFetchApiUrl, parent);
        connect(m_fanartBrowserPage, &FanartBrowserPage::closeRequested, this,
                &MainWindow::navigateBackward);
        connect(m_fanartBrowserPage, &FanartBrowserPage::fanartChanged, this, [this](qint64) {
            if (m_shelfPage) m_shelfPage->refresh();
        });
        return m_fanartBrowserPage;
    }
    if (routeName == QStringLiteral("actress"))
    {
        m_actressPage = new ActressPage(m_publicDatabase, m_privateDatabase, m_themeService,
                                         m_paths.actressImageDirectory(), parent);
        connect(m_actressPage, &PersonPage::detailRequested, this,
                [this](PersonKind requestedKind, qint64 personId) {
                    const QString detailRoute = requestedKind == PersonKind::Actress
                        ? QStringLiteral("actress_detail")
                        : QStringLiteral("actor_detail");
                    auto *detail = static_cast<PersonDetailPage *>(ensurePage(detailRoute));
                    if (detail->showPerson(personId))
                        navigateTo(detailRoute);
                });
        connect(m_actressPage, &PersonPage::editRequested, this, &MainWindow::openPersonEditor);
        return m_actressPage;
    }
    if (routeName == QStringLiteral("actor"))
    {
        m_actorPage = new ActorPage(m_publicDatabase, m_privateDatabase, m_themeService,
                                    m_paths.actorImageDirectory(), parent);
        connect(m_actorPage, &PersonPage::detailRequested, this,
                [this](PersonKind, qint64 personId) {
                    auto *detail = static_cast<ActorDetailPage *>(ensurePage(QStringLiteral("actor_detail")));
                    if (detail->showPerson(personId))
                        navigateTo(QStringLiteral("actor_detail"));
                });
        connect(m_actorPage, &PersonPage::editRequested, this, &MainWindow::openPersonEditor);
        return m_actorPage;
    }
    if (routeName == QStringLiteral("actress_detail"))
    {
        m_actressDetailPage = new ActressDetailPage(
            m_publicDatabase, m_privateDatabase, m_themeService,
            m_paths.actressImageDirectory(), parent, m_paths.workCoverDirectory(), m_graphManager.get());
        connect(m_actressDetailPage, &PersonDetailPage::editRequested, this, &MainWindow::openPersonEditor);
        connect(m_actressDetailPage, &PersonDetailPage::workRequested, this, [this](qint64 workId) {
            showWorkInShelf(workId);
        });
        connect(m_actressDetailPage, &PersonDetailPage::favoriteChanged, this, [this] {
            if (m_actressPage)
                m_actressPage->refresh();
        });
        return m_actressDetailPage;
    }
    if (routeName == QStringLiteral("actor_detail"))
    {
        m_actorDetailPage = new ActorDetailPage(
            m_publicDatabase, m_privateDatabase, m_themeService,
            m_paths.actorImageDirectory(), parent, m_paths.workCoverDirectory());
        connect(m_actorDetailPage, &PersonDetailPage::editRequested, this, &MainWindow::openPersonEditor);
        connect(m_actorDetailPage, &PersonDetailPage::workRequested, this, [this](qint64 workId) {
            showWorkInShelf(workId);
        });
        connect(m_actorDetailPage, &PersonDetailPage::favoriteChanged, this, [this] {
            if (m_actorPage)
                m_actorPage->refresh();
        });
        return m_actorDetailPage;
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
        connect(editor, &PersonEditorPage::personSaved, this,
                [this](PersonKind savedKind, qint64 savedId)
                {
                    PersonPage *list = savedKind == PersonKind::Actress
                        ? static_cast<PersonPage *>(m_actressPage)
                        : static_cast<PersonPage *>(m_actorPage);
                    PersonDetailPage *detail = savedKind == PersonKind::Actress
                        ? static_cast<PersonDetailPage *>(m_actressDetailPage)
                        : static_cast<PersonDetailPage *>(m_actorDetailPage);
                    if (list)
                        list->refresh();
                    if (detail && detail->currentPersonId() == savedId)
                        detail->showPerson(savedId);
                    if (m_managementPage != nullptr)
                        m_managementPage->refreshPersonSelectors();
                    refreshRelationshipGraph();
                });
        connect(editor, &PersonEditorPage::workLinkRequested, this, [this](qint64 workId) {
            showWorkInShelf(workId);
        });
        connect(editor, &PersonEditorPage::actressLinkRequested, this, [this](qint64 actressId) {
            auto *detail = static_cast<PersonDetailPage *>(ensurePage(QStringLiteral("actress_detail")));
            if (detail->showPerson(actressId))
                navigateTo(QStringLiteral("actress_detail"));
        });
        connect(editor, &PersonEditorPage::personViewRequested, this,
                [this](PersonKind requestedKind, qint64 personId) {
                    const QString route = requestedKind == PersonKind::Actress
                        ? QStringLiteral("actress_detail") : QStringLiteral("actor_detail");
                    auto *detail = static_cast<PersonDetailPage *>(ensurePage(route));
                    if (detail->showPerson(personId)) navigateTo(route);
                });
        connect(editor, &PersonEditorPage::personDeleted, this,
                [this](PersonKind deletedKind, qint64) {
                    PersonPage *list = deletedKind == PersonKind::Actress
                        ? static_cast<PersonPage *>(m_actressPage)
                        : static_cast<PersonPage *>(m_actorPage);
                    if (list) list->refresh();
                    if (m_managementPage != nullptr)
                        m_managementPage->refreshPersonSelectors();
                    refreshRelationshipGraph();
                    navigateTo(deletedKind == PersonKind::Actress ? QStringLiteral("actress")
                                                                  : QStringLiteral("actor"));
                });
        connect(editor, &PersonEditorPage::closeRequested, this, &MainWindow::navigateBackward);
        return editor;
    }
    if (routeName == QStringLiteral("database"))
    {
        m_managementPage =
            new ManagementPage(m_publicDatabase, m_themeService, *m_crawlerScheduler, m_privateDatabase,
                               m_paths.workCoverDirectory(), m_paths.actressImageDirectory(),
                               m_paths.fanartDirectory(),
                               settings::crawler().coverFetchApiUrl,
                               settings::crawler().topActressesApiUrl, m_graphManager.get(), parent);
        auto *management = m_managementPage;
        connect(management, &ManagementPage::referencesChanged, this,
                [this](ReferenceKind) {
                    if (m_workPage)
                        m_workPage->refreshReferences();
                    if (m_shelfPage)
                        m_shelfPage->refreshReferences();
                    refreshRelationshipGraph();
                });
        connect(management, &ManagementPage::tagsChanged, this, [this] {
            if (m_workPage)
                m_workPage->refreshTags();
            if (m_shelfPage)
                m_shelfPage->refreshTags();
        });
        connect(management, &ManagementPage::worksChanged, this, [this] {
            if (m_workPage)
                m_workPage->refresh();
            if (m_shelfPage)
                m_shelfPage->refresh();
            refreshRelationshipGraph();
        });
        connect(management, &ManagementPage::workChanged, this, [this](qint64 workId) {
            if (m_workPage)
                m_workPage->refresh();
            if (m_shelfPage)
                m_shelfPage->refresh();
            refreshRelationshipGraphWork(workId);
        });
        connect(management, &ManagementPage::actressesChanged, this, [this] {
            if (m_actressPage)
                m_actressPage->refresh();
            refreshRelationshipGraph();
        });
        connect(management, &ManagementPage::actressesCreated, this,
                &MainWindow::enqueueActressSyncs);
        connect(management, &ManagementPage::workRequested, this, [this](qint64 workId) {
            showWorkInShelf(workId);
        });
        connect(management, &ManagementPage::actressRequested, this, [this](qint64 actressId) {
            auto *detail =
                static_cast<PersonDetailPage *>(ensurePage(QStringLiteral("actress_detail")));
            if (detail->showPerson(actressId))
                navigateTo(QStringLiteral("actress_detail"));
        });
        return management;
    }
    if (routeName == QStringLiteral("chart"))
    {
        m_statisticsPage =
            new StatisticsPage(m_publicDatabase, m_privateDatabase, m_themeService,
                               m_paths.actressImageDirectory(), parent);
        connect(m_statisticsPage, &StatisticsPage::actressDetailRequested, this,
                [this](qint64 actressId) {
                    auto *detail = static_cast<PersonDetailPage *>(
                        ensurePage(QStringLiteral("actress_detail")));
                    if (detail->showPerson(actressId))
                        navigateTo(QStringLiteral("actress_detail"));
                });
        connect(m_statisticsPage, &StatisticsPage::actressEditRequested, this,
                [this](qint64 actressId) {
                    openPersonEditor(PersonKind::Actress, actressId);
                });
        return m_statisticsPage;
    }
    if (routeName == QStringLiteral("graph"))
    {
        m_forceDirectPage =
            new ForceDirectPage(m_themeService, *m_graphManager, parent,
                                m_paths.actressImageDirectory(), m_paths.workCoverDirectory());
        connect(m_forceDirectPage, &ForceDirectPage::workRequested, this, [this](qint64 workId) {
            showWorkInShelf(workId);
        });
        connect(m_forceDirectPage, &ForceDirectPage::actressRequested, this, [this](qint64 actressId) {
            auto *detail = static_cast<PersonDetailPage *>(ensurePage(QStringLiteral("actress_detail")));
            if (detail->showPerson(actressId))
                navigateTo(QStringLiteral("actress_detail"));
        });
        return m_forceDirectPage;
    }
    if (routeName == QStringLiteral("shelf"))
    {
        m_shelfPage = new ShelfPage(m_publicDatabase, m_privateDatabase, m_themeService,
                                    m_graphManager.get(), m_paths.workCoverDirectory(),
                                    m_paths.fanartDirectory(), parent);
        connect(m_shelfPage, &ShelfPage::detailRequested, this, [this](qint64 workId) {
            showWorkInShelf(workId);
        });
        connect(m_shelfPage, &ShelfPage::editRequested, this, [this](qint64 workId) {
            auto *management = static_cast<ManagementPage *>(ensurePage(QStringLiteral("database")));
            if (management->loadWork(workId)) navigateTo(QStringLiteral("database"));
        });
        connect(m_shelfPage, &ShelfPage::workDeleted, this, [this](qint64) {
            if (m_workPage) m_workPage->refresh();
            refreshRelationshipGraph();
        });
        connect(m_shelfPage, &ShelfPage::favoriteChanged, this, [this] {
            if (m_workPage) m_workPage->refresh();
            if (m_statisticsPage) m_statisticsPage->refresh();
        });
        connect(m_shelfPage, &ShelfPage::actressRequested, this, [this](qint64 actressId) {
            auto *detail = static_cast<PersonDetailPage *>(ensurePage(QStringLiteral("actress_detail")));
            if (detail->showPerson(actressId)) navigateTo(QStringLiteral("actress_detail"));
        });
        connect(m_shelfPage, &ShelfPage::actorRequested, this, [this](qint64 actorId) {
            auto *work = static_cast<WorkPage *>(ensurePage(QStringLiteral("mutiwork")));
            work->filterByActor(actorId);
            navigateTo(QStringLiteral("mutiwork"));
        });
        connect(m_shelfPage, &ShelfPage::tagRequested, this, [this](qint64 tagId) {
            auto *work = static_cast<WorkPage *>(ensurePage(QStringLiteral("mutiwork")));
            work->filterByTag(tagId);
            navigateTo(QStringLiteral("mutiwork"));
        });
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
    if (routeName == QStringLiteral("setting"))
    {
        auto *settings = new SettingsPage(m_themeService, m_paths.shortcutsFile(),
                                          m_publicDatabase, m_privateDatabase, m_paths, parent);
        connect(settings, &SettingsPage::worksChanged, this, [this] {
            if (m_workPage)
                m_workPage->refresh();
            if (m_shelfPage)
                m_shelfPage->refresh();
            refreshRelationshipGraph();
        });
        connect(settings, &SettingsPage::quickWorkRequested, this,
                [this](const QStringList &serials) {
            AddQuickWorkDialog dialog(m_publicDatabase, *m_crawlerScheduler, m_themeService, this);
            dialog.loadSerials(serials);
            dialog.exec();
        });
        connect(settings, &SettingsPage::referencesChanged, this, [this](ReferenceKind) {
            if (m_workPage) m_workPage->refreshReferences();
            if (m_shelfPage) m_shelfPage->refreshReferences();
        });
        connect(settings, &SettingsPage::tagsChanged, this, [this] {
            if (m_workPage) m_workPage->refreshTags();
            if (m_shelfPage) m_shelfPage->refreshTags();
        });
        connect(settings, &SettingsPage::actressesChanged, this, [this] {
            if (m_actressPage) m_actressPage->refresh();
            if (m_managementPage) m_managementPage->refreshPersonSelectors();
        });
        connect(settings, &SettingsPage::actorsChanged, this, [this] {
            if (m_actorPage) m_actorPage->refresh();
            if (m_managementPage) m_managementPage->refreshPersonSelectors();
        });
        return settings;
    }
    return new PlaceholderPage(menuTitle, routeName, parent);
}

void MainWindow::navigateTo(const QString &routeName, bool recordHistory)
{
    if (!m_routeIndexes.contains(routeName))
        return;
    auto *widget = ensurePage(routeName);
    if (!widget)
        return;
    if (recordHistory)
    {
        if (m_historyIndex >= 0 && m_history.value(m_historyIndex) == routeName)
        {
            m_pages->setCurrentWidget(widget);
            return;
        }
        while (m_history.size() > m_historyIndex + 1)
            m_history.removeLast();
        m_history.append(routeName);
        m_historyIndex = m_history.size() - 1;
    }
    m_pages->setCurrentWidget(widget);
    if (routeName == QStringLiteral("chart"))
    {
        m_statisticsPage->refresh();
    }
    if (routeName == QStringLiteral("test_page"))
    {
        m_dashboardPage->refresh();
    }
    if (routeName == QStringLiteral("setting"))
    {
        if (!m_themeSelector)
        {
            auto *settings = static_cast<SettingsPage *>(widget);
            m_themeSelector = settings->themeSelector();
            populateThemeSelector();
            connect(m_themeSelector, &QComboBox::currentIndexChanged, this,
                    &MainWindow::changeTheme);
        }
        m_sidebar->clearSelection();
    }
    else if (routeName == QStringLiteral("mutiwork"))
    {
        m_sidebar->select(QStringLiteral("work"));
    }
    else if (routeName == QStringLiteral("actress_detail"))
    {
        m_sidebar->select(QStringLiteral("actress"));
    }
    else if (routeName == QStringLiteral("actor_detail"))
    {
        m_sidebar->select(QStringLiteral("actor"));
    }
    else if (routeName == QStringLiteral("modify_actress"))
    {
        m_sidebar->select(QStringLiteral("actress"));
    }
    else if (routeName == QStringLiteral("modify_actor"))
    {
        m_sidebar->select(QStringLiteral("actor"));
    }
    else
    {
        const QMap<QString, QString> routeMenus{
            {QStringLiteral("home"), QStringLiteral("home")},
            {QStringLiteral("actress"), QStringLiteral("actress")},
            {QStringLiteral("actor"), QStringLiteral("actor")},
            {QStringLiteral("database"), QStringLiteral("database")},
            {QStringLiteral("chart"), QStringLiteral("chart")},
            {QStringLiteral("graph"), QStringLiteral("graph")},
            {QStringLiteral("shelf"), QStringLiteral("shelf")},
            {QStringLiteral("av"), QStringLiteral("av")},
            {QStringLiteral("inbox"), QStringLiteral("bell")}};
        if (routeMenus.contains(routeName))
            m_sidebar->select(routeMenus.value(routeName));
    }
}

void MainWindow::openPersonEditor(PersonKind kind, qint64 personId)
{
    const QString route = kind == PersonKind::Actress
        ? QStringLiteral("modify_actress") : QStringLiteral("modify_actor");
    const bool loaded = kind == PersonKind::Actress
        ? static_cast<ModifyActressPage *>(ensurePage(route))->loadActress(personId)
        : static_cast<ModifyActorPage *>(ensurePage(route))->loadActor(personId);
    if (!loaded)
        return;
    navigateTo(route);
}

void MainWindow::navigateBackward()
{
    if (m_historyIndex <= 0)
        return;
    --m_historyIndex;
    navigateTo(m_history.at(m_historyIndex), false);
}

void MainWindow::navigateForward()
{
    if (m_historyIndex + 1 >= m_history.size())
        return;
    ++m_historyIndex;
    navigateTo(m_history.at(m_historyIndex), false);
}

void MainWindow::saveWindowState() const
{
    SettingsStore store(settings::Paths{}.settingsFile());
    store.setValue(QStringLiteral("window/maximized"), isMaximized());
}

} // namespace darkeye
