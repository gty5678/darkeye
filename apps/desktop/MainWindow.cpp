#include "MainWindow.h"
#include "app/Resources.h"
#include "utils/Documentation.h"

#include "ui/pages/AvPage.h"
#include "ui/pages/ForceDirectPage.h"
#include "ui/pages/FanartBrowserPage.h"
#include "ui/pages/InboxPage.h"
#include "ui/pages/HomePage.h"
#include "ui/pages/PersonDetailPage.h"
#include "ui/pages/PersonPage.h"
#include "ui/pages/PersonalDataPanels.h"
#include "ui/pages/StatisticsChartPage.h"
#include "ui/pages/SettingsPage.h"
#include "ui/pages/ShelfPage.h"
#include "ui/pages/WorkPage.h"
#include "ui/pages/AddWorkTabPage3.h"
#include "ui/pages/LabelManagementWidget.h"
#include "ui/pages/MakerManagementWidget.h"
#include "ui/pages/PersonalRecordManagementWidget.h"
#include "ui/pages/SummaryQueryWidget.h"
#include "ui/pages/TagManagementWidget.h"
#include "ui/pages/WorkBatchStateWidget.h"
#include "ui/components/WorkFilter.h"
#include "crawler/CrawlerScheduler.h"
#include "http/LocalApiServer.h"
#include "services/ActressSyncService.h"
#include "darkeye_ui/base/LazyWidget.h"
#include "darkeye_ui/components/ColorWheel.h"
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
#include <QFile>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeySequence>
#include <QImage>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QPainter>
#include <QPixmap>
#include <QScreen>
#include <QScrollArea>
#include <QStackedLayout>
#include <QVBoxLayout>
#include <QTimer>
#include <QUrl>
#include <QUuid>
#include <QVariantAnimation>
#include <algorithm>

#include <utility>

#ifdef Q_OS_WIN
#define NOMINMAX
#include <Windows.h>
#include <dwmapi.h>
#elif defined(Q_OS_MACOS)
#include "MacWindowAppearance.h"
#include "MacWorkspaceShortcuts.h"
#include <QShowEvent>
#endif

namespace darkeye
{

namespace
{

constexpr qsizetype recentWorkspaceLimit = 9;

class PageNavigationBar final : public QWidget
{
public:
    PageNavigationBar(ThemeService &themeService, QWidget *parent) : QWidget(parent)
    {
        const auto applyTheme = [this, &themeService] {
            const auto theme = myads::dockThemeFromTokens(
                ThemeService::tokens(themeService.current(), themeService.customPrimary()));
            m_dividerBackground = theme.background;
            m_dividerLine = theme.splitterLine();
            update();
        };
        applyTheme();
        connect(&themeService, &ThemeService::themeChanged, this, applyTheme);
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        const QRect divider(rect().right(), 0, 1, height());
        painter.fillRect(divider, m_dividerBackground);
        painter.fillRect(divider, m_dividerLine);
    }

private:
    QColor m_dividerBackground;
    QColor m_dividerLine;
};

const auto actressSwitchRoute = QStringLiteral("actress_detail/switch");
struct WorkspacePreset {
    QString command;
    QString title;
    QString resource;
};

const WorkspacePreset defaultWorkspacePreset{
    QStringLiteral("restore_workspace_preset"),
    QStringLiteral("恢复预设工作区布局 · 默认"),
    QStringLiteral(":/workspace/browsing.json"),
};
const WorkspacePreset workspacePresets[]{
    defaultWorkspacePreset,
    {QStringLiteral("restore_pure_browsing_workspace_preset"),
     QStringLiteral("恢复预设工作区布局 · 纯浏览"),
     QStringLiteral(":/workspace/pure-browsing.json")},
};

const QList<QPair<QString, QString>> dialogNavigationActions{
    {QStringLiteral("add_quick_work"), QStringLiteral("plus")},
    {QStringLiteral("add_sexual_rousal_record"), QStringLiteral("sprout")},
    {QStringLiteral("add_makelove_record"), QStringLiteral("love_off")},
    {QStringLiteral("add_masturbation_record"), QStringLiteral("note_pen")},
};

const auto libraryOverviewRoute = QStringLiteral("library_overview");
const auto topActressesRoute = QStringLiteral("top_actresses");
const auto recordHeatmapRoute = QStringLiteral("record_heatmap");

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

bool isRecentEntityRoute(const QString &route)
{
    const auto view = QStringView(route);
    const auto slash = view.indexOf(u'/');
    if (slash <= 0) return false;
    const auto base = view.first(slash);
    return base != u"new"
        && base != u"setting" && base != u"search" && base != u"database" && base != u"person_graph"
        && base != u"manage_work" && base != u"actor_detail";
}

} // namespace

MainWindow::MainWindow(ThemeService &themeService,
                       QSqlDatabase publicDatabase, QSqlDatabase privateDatabase,
                       settings::Paths paths, QWidget *parent)
    : QMainWindow(parent), m_themeService(themeService),
      m_paths(std::move(paths)), m_publicDatabase(std::move(publicDatabase)),
      m_privateDatabase(std::move(privateDatabase))
{
#ifdef Q_OS_MACOS
    setWindowFlag(Qt::NoTitleBarBackgroundHint);
    connect(&m_themeService, &ThemeService::themeChanged, this, &MainWindow::updateMacTitleBarBackground);
#endif
    setWindowTitle(QStringLiteral("暗之眼 V%1").arg(QStringLiteral(DARKEYE_VERSION)));
#ifdef Q_OS_WIN
    connect(&m_themeService, &ThemeService::themeChanged, this, &MainWindow::updateNativeTitleBarColors);
#endif
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
    buildUi();
    resize(1200, 600);
}

#ifdef Q_OS_WIN
bool MainWindow::event(QEvent *event)
{
    if (event->type() == QEvent::WinIdChange)
        updateNativeTitleBarColors();
    return QMainWindow::event(event);
}

void MainWindow::updateNativeTitleBarColors()
{
    const auto window = reinterpret_cast<HWND>(internalWinId());
    if (!window) return;

    const ThemeTokens tokens = m_themeService.currentTokens();
    const QColor background(tokens.pageBackground);
    const COLORREF captionColor = RGB(background.red(), background.green(), background.blue());
    if (FAILED(DwmSetWindowAttribute(window, DWMWA_CAPTION_COLOR, &captionColor, sizeof(captionColor))))
        return;

    const QColor text(tokens.text);
    const COLORREF textColor = RGB(text.red(), text.green(), text.blue());
    DwmSetWindowAttribute(window, DWMWA_TEXT_COLOR, &textColor, sizeof(textColor));
}
#endif

#ifdef Q_OS_MACOS
void MainWindow::showEvent(QShowEvent *event)
{
    QMainWindow::showEvent(event);
    updateMacTitleBarBackground();
}

void MainWindow::updateMacTitleBarBackground()
{
    if (!internalWinId()) return;
    const ThemeTokens tokens = m_themeService.currentTokens();
    setMacTitleBarBackground(*this, QColor(tokens.pageBackground), tokens.colorSpace,
                             m_themeService.current() == ThemeId::Dark);
}
#endif

void MainWindow::showInitial()
{
    // Apply the initial geometry after Qt creates the native window.
    winId();
    if (const auto *initialScreen = screen()) {
        const QSize availableSize = initialScreen->availableGeometry().size();
        if (!availableSize.isEmpty()) resize(availableSize * 0.85);
    }
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
    connect(&api, &LocalApiServer::captureOneReceived, this,
            [this](const QString &serial) { m_crawlerScheduler->enqueue({serial}); });
    connect(&api, &LocalApiServer::crawlerBacklogWarning, this,
            [this](int count, const QString &browser)
            {
                Toast::showWarning(this, QStringLiteral("%1 爬虫标签已积压 %2 个，请处理验证页。").arg(browser).arg(count),
                                   &m_themeService, 6000);
            });
    connect(&api, &LocalApiServer::cloudflareChallengeReceived, this,
            [this](const QString &site, const QString &serial)
            {
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
    if (!m_workspace->requestWindowClose()) {
        event->ignore();
        return;
    }
    if (m_workspaceSaveTimer) m_workspaceSaveTimer->stop();
    saveWorkspace();
    QSettings settings(m_paths.settingsFile(), QSettings::IniFormat);
    settings.setValue(QStringLiteral("window/maximized"), isMaximized());
    settings.sync();
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
    m_workFilter->setWindowTitle(QStringLiteral("作品过滤器"));
    m_workFilter->hide();
    m_workspace = new myads::WorkspaceWidget(workspaceContainer);
    m_workspace->setObjectName(QStringLiteral("mainWorkspace"));
    const auto appSettings = settings::app(m_paths.settingsFile());
    updateSidebarSettings(appSettings);
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
        if (route == QStringLiteral("work_filters") && !m_workFilter->parentWidget())
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

    registerPage(QStringLiteral("作品展厅"), QStringLiteral("home"), QStringLiteral("house"));
    registerPage(QStringLiteral("平铺浏览"), QStringLiteral("mutiwork"), QStringLiteral("film"));
    registerPage(QStringLiteral("DVD 浏览"), QStringLiteral("shelf"), QStringLiteral("library_big"));
    registerPage(QStringLiteral("女演员"), QStringLiteral("actress"), QStringLiteral("venus"));
    registerPage(QStringLiteral("男演员"), QStringLiteral("actor"), QStringLiteral("mars"));
    registerPage(QStringLiteral("统计"), QStringLiteral("chart"), QStringLiteral("chart_line"));
    registerPage(QStringLiteral("全局关系图"), QStringLiteral("graph"), QStringLiteral("share_2"));
    registerPage(QStringLiteral("暗黑界知识库"), QStringLiteral("av"), QStringLiteral("tv"));
    registerPage(QStringLiteral("采集任务"), QStringLiteral("inbox"), QStringLiteral("arrow_down_to_line"));
    registerPage(QStringLiteral("剧照浏览"), QStringLiteral("fanart"));
    registerPage(QStringLiteral("女演员详情（跟随切换）"), QStringLiteral("actress_detail"), QStringLiteral("eye"));
    registerPage(QStringLiteral("修改女演员"), QStringLiteral("modify_actress"), QStringLiteral("square_pen"));
    registerPage(QStringLiteral("修改男演员"), QStringLiteral("modify_actor"), QStringLiteral("square_pen"));
    registerPage(QStringLiteral("库藏概览"), libraryOverviewRoute, QStringLiteral("database"));
    registerPage(QStringLiteral("最喜欢的女优"), topActressesRoute, QStringLiteral("love_on"));
    registerPage(QStringLiteral("个人记录热力图"), recordHeatmapRoute, QStringLiteral("layout_grid"));
    registerPage(QStringLiteral("局部关系图"), QStringLiteral("local_graph"), QStringLiteral("link"));
    registerPage(QStringLiteral("作品时间轴"), QStringLiteral("timeline"), QStringLiteral("list_x"));
    registerPage(QStringLiteral("标签管理"), QStringLiteral("manage_tags"), QStringLiteral("link"));
    registerPage(QStringLiteral("片商管理"), QStringLiteral("manage_makers"), QStringLiteral("layout_grid"));
    registerPage(QStringLiteral("厂牌管理"), QStringLiteral("manage_labels"), QStringLiteral("note_pen"));
    registerPage(QStringLiteral("系列管理"), QStringLiteral("manage_series"), QStringLiteral("library_big"));
    registerPage(QStringLiteral("资料汇总"), QStringLiteral("manage_summary"), QStringLiteral("scroll_text"));
    registerPage(QStringLiteral("个人记录"), QStringLiteral("manage_records"), QStringLiteral("note_pen"));
    registerPage(QStringLiteral("批量移入回收站"), QStringLiteral("manage_deleted"), QStringLiteral("list_x"));
    registerPage(QStringLiteral("回收站"), QStringLiteral("manage_trash"), QStringLiteral("trash_2"));
    registerPage(m_workFilter->windowTitle(), QStringLiteral("work_filters"), QStringLiteral("funnel"));
    m_globalSearch = new WorkspaceSearch(m_publicDatabase, workspaceCommandPages(), centralWidget);
    auto commands = SettingsDialog::workspaceCommands();
    commands.insert(QStringLiteral("open_help"), QStringLiteral("帮助"));
    commands.insert(QStringLiteral("open_github"), QStringLiteral("GitHub 项目仓库"));
    commands.insert(QStringLiteral("open_changelog"), QStringLiteral("版本记录"));
    for (const auto &preset : workspacePresets)
        commands.insert(preset.command, preset.title);
    m_globalSearch->addCommands(commands);
    m_globalSearch->hide();
    m_globalSearch->setAutoFillBackground(true);
    connect(m_globalSearch, &WorkspaceSearch::openRequested, this, &MainWindow::openWorkspaceSearchResult);
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
    QMap<QString, QString> dialogCommands;
    for (const auto &entry : dialogNavigationActions) {
        const auto *action = m_shortcutActions->findChild<QAction *>(entry.first);
        dialogCommands.insert(action->objectName(), action->text());
    }
    m_globalSearch->addCommands(dialogCommands);
    m_navigationBar = buildNavigationBar(centralWidget);
    mainLayout->addWidget(m_navigationBar);
    mainLayout->addWidget(workspaceContainer, 1);

    auto *emptyState = new QWidget(workspaceContainer);
    auto *hintLayout = new QVBoxLayout(emptyState);
    hintLayout->setAlignment(Qt::AlignCenter);
    m_navigationHint = new QLabel(QStringLiteral("从左侧导航打开页面"), emptyState);
    m_navigationHint->setAlignment(Qt::AlignCenter);
    hintLayout->addWidget(m_navigationHint);
    updateNavigationBar(appSettings);
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
    restoreWorkspace();

    m_workspaceSaveTimer = new QTimer(this);
    m_workspaceSaveTimer->setSingleShot(true);
    m_workspaceSaveTimer->setInterval(250);
    connect(m_workspaceSaveTimer, &QTimer::timeout, this, &MainWindow::saveWorkspace);
    connect(m_workspace, &myads::WorkspaceWidget::layoutChanged, this, &MainWindow::scheduleWorkspaceSave);
    connect(m_workspace, &myads::WorkspaceWidget::layoutPreferencesChanged, this, &MainWindow::scheduleWorkspaceSave);
    connect(m_workspace, &myads::WorkspaceWidget::contentClosed, this, &MainWindow::scheduleWorkspaceSave);
    connect(m_workspace, &myads::WorkspaceWidget::activePaneChanged, this, &MainWindow::scheduleWorkspaceSave);
}

void MainWindow::restoreWorkspace()
{
    const auto savedLayout = workspaceFile();
    const bool hasSavedLayout = QFile::exists(savedLayout);
    QString error;
    const bool restored = hasSavedLayout &&
        m_workspace->loadLayout(savedLayout,
            [this](const QJsonObject &descriptor) { return workspaceContent(descriptor); }, &error);
    if (!restored) {
        if (hasSavedLayout)
            qWarning() << "Workspace restore:" << (error.isEmpty() ? QStringLiteral("工作区布局为空") : error);
        if (!restoreWorkspacePreset(defaultWorkspacePreset.resource, /*showFailureDialog=*/false))
            m_workspace->resetToSingleEmptyPane();
        return;
    }

    const auto *pane = m_workspace->activePane();
    if (pane && !pane->currentContentId().isEmpty())
        navigateTo(pane->currentContentId(), false);
}

QWidget *MainWindow::buildNavigationBar(QWidget *parent)
{
    constexpr int expandedWidth = 208;
    constexpr int collapsedWidth = 56;
    constexpr int buttonSize = 36;
    constexpr int horizontalInset = (collapsedWidth - buttonSize) / 2;
    auto *bar = new PageNavigationBar(m_themeService, parent);
    bar->setObjectName(QStringLiteral("pageNavigationBar"));
    bar->setAttribute(Qt::WA_StyledBackground);
    bar->setAccessibleName(QStringLiteral("页面导航"));
    auto *layout = new QVBoxLayout(bar);
    layout->setContentsMargins(horizontalInset, 6, horizontalInset, 6);
    layout->setSpacing(4);

    auto *scroll = new QScrollArea(bar);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidgetResizable(true);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    auto *pages = new QWidget;
    pages->setObjectName(QStringLiteral("pageNavigationPages"));
    auto *pageLayout = new QVBoxLayout(pages);
    pageLayout->setContentsMargins(0, 0, 0, 0);
    pageLayout->setSpacing(4);
    pageLayout->setAlignment(Qt::AlignTop);
    scroll->setWidget(pages);

    const auto addButton = [this](QVBoxLayout *column, const QString &id,
                                  const QString &title, const QString &icon) {
        auto *button = new IconButton(icon, title, &m_themeService, column->parentWidget());
        button->setProperty("navigationId", id);
        button->setButtonPixelSize(buttonSize);
        button->setMaximumWidth(QWIDGETSIZE_MAX);
        button->setIconPixelSize(20);
        button->setFocusPolicy(Qt::TabFocus);
        auto *row = new QHBoxLayout(button);
        row->setContentsMargins(41, 0, 7, 0);
        auto *label = new QLabel(title, button);
        label->setObjectName(QStringLiteral("pageNavigationLabel"));
        label->setAttribute(Qt::WA_TransparentForMouseEvents);
        label->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
        row->addWidget(label);
        column->addWidget(button);
        if (id != QStringLiteral("toggle_navigation"))
            m_navigationButtons.append(button);
        return button;
    };
    const auto addActionButton = [this, &addButton](QVBoxLayout *column, const QString &id,
                                                   const QString &title, const QString &icon) {
        auto *button = addButton(column, id, title, icon);
        auto *action = m_shortcutActions->findChild<QAction *>(id);
        connect(button, &QPushButton::clicked, action, &QAction::trigger);
    };
    auto *toggle = addButton(layout, QStringLiteral("toggle_navigation"),
                             QStringLiteral("收起页面导航"), QStringLiteral("panel_left_close"));
    addActionButton(layout, QStringLiteral("workspace_search"), QStringLiteral("搜索实体"), QStringLiteral("search"));
    layout->addWidget(scroll, 1);
    for (const auto &[id, icon] : dialogNavigationActions) {
        const auto *action = m_shortcutActions->findChild<QAction *>(id);
        addActionButton(pageLayout, id, action->text(), icon);
    }

    using SearchMode = WorkspaceSearch::Mode;
    const auto addPicker = [this, &addButton, pageLayout](const QString &id, const QString &title,
                                                        const QString &icon, SearchMode mode) {
        auto *button = addButton(pageLayout, id, title, icon);
        connect(button, &QPushButton::clicked, this, [this, mode] { m_globalSearch->open(mode, {}); });
    };
    for (const auto &[route, icon] : m_navigationPages) {
        const auto title = m_routeTitles.value(route);
        if (route == QStringLiteral("modify_actress")) {
            addPicker(route, title, icon, SearchMode::EditActress);
        } else if (route == QStringLiteral("modify_actor")) {
            addPicker(route, title, icon, SearchMode::EditActor);
        } else {
            auto *button = addButton(pageLayout, route, title, icon);
            connect(button, &QPushButton::clicked, this, [this, route] { navigateTo(route); });
            if (route == QStringLiteral("actress_detail")) {
                addPicker(WorkspaceSearch::independentActressCommand, QStringLiteral("女演员详情（独立页）"),
                          QStringLiteral("eye"), SearchMode::IndependentActress);
            }
        }
    }
    addPicker(QStringLiteral("work_edit"), QStringLiteral("编辑作品"),
              QStringLiteral("square_pen"), SearchMode::EditWork);
    addActionButton(layout, QStringLiteral("workspace_commands"), QStringLiteral("更多页面与命令"), QStringLiteral("terminal"));
    addActionButton(layout, QStringLiteral("open_help"), QStringLiteral("帮助"), QStringLiteral("circle_question"));
    addActionButton(layout, QStringLiteral("open_settings"), QStringLiteral("设置"), QStringLiteral("settings"));

    const auto updateToggle = [bar, toggle](bool expanded) {
        bar->setProperty("expanded", expanded);
        const auto title = expanded ? QStringLiteral("收起页面导航") : QStringLiteral("展开页面导航");
        toggle->setIconName(expanded ? QStringLiteral("panel_left_close") : QStringLiteral("panel_left_open"));
        toggle->setAccessibleName(title);
        toggle->setToolTip(title);
        toggle->findChild<QLabel *>(QStringLiteral("pageNavigationLabel"))->setText(title);
    };
    const auto labels = bar->findChildren<QLabel *>(QStringLiteral("pageNavigationLabel"));
    const auto setLabelsVisible = [labels](bool visible) {
        for (auto *label : labels)
            label->setVisible(visible);
    };
    auto *animation = new QVariantAnimation(bar);
    animation->setDuration(220);
    animation->setEasingCurve(QEasingCurve::InOutCubic);
    connect(animation, &QVariantAnimation::valueChanged, bar, [bar, animation](const QVariant &width) {
        if (animation->state() == QAbstractAnimation::Running)
            bar->setFixedWidth(width.toInt());
    });
    connect(animation, &QVariantAnimation::finished, bar, [bar, setLabelsVisible] {
        setLabelsVisible(bar->property("expanded").toBool());
    });

    const auto expandedKey = QStringLiteral("Workspace/NavigationBarExpanded");
    QSettings settings(m_paths.settingsFile(), QSettings::IniFormat);
    const bool initiallyExpanded = settings.value(expandedKey, true).toBool();
    updateToggle(initiallyExpanded);
    setLabelsVisible(initiallyExpanded);
    bar->setFixedWidth(initiallyExpanded ? expandedWidth : collapsedWidth);
    connect(toggle, &QPushButton::clicked, this,
            [this, bar, animation, expandedKey, updateToggle, setLabelsVisible] {
        const bool expanded = !bar->property("expanded").toBool();
        const int currentWidth = bar->width();
        animation->stop();
        updateToggle(expanded);
        if (expanded)
            setLabelsVisible(true);
        animation->setStartValue(currentWidth);
        animation->setEndValue(expanded ? expandedWidth : collapsedWidth);
        animation->start();
        QSettings settings(m_paths.settingsFile(), QSettings::IniFormat);
        settings.setValue(expandedKey, expanded);
    });
    return bar;
}

void MainWindow::updateNavigationBar(const AppSettings &settings)
{
    bool hasVisibleItem = false;
    for (auto *button : m_navigationButtons) {
        const bool visible = !settings.hiddenNavigationItems.contains(
            button->property("navigationId").toString());
        button->setVisible(visible);
        hasVisibleItem |= visible;
    }
    m_navigationBar->setVisible(settings.navigationBarVisible);
    m_navigationHint->setVisible(settings.navigationBarVisible && hasVisibleItem);
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
                  connect(&dialog, &AddMasturbationDialog::recordAdded, this,
                          &MainWindow::refreshPersonalRecordPanels);
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
                  connect(&dialog, &AddMakeLoveDialog::recordAdded, this,
                          &MainWindow::refreshPersonalRecordPanels);
                  dialog.exec();
              });
    addAction(QStringLiteral("add_sexual_rousal_record"), QStringLiteral("添加晨勃记录"),
              QStringLiteral("A"), [this]
              {
                  AddSexualArousalDialog dialog(m_privateDatabase, this);
                  connect(&dialog, &AddSexualArousalDialog::recordAdded, this,
                          &MainWindow::refreshPersonalRecordPanels);
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
    if (commands) {
        m_globalSearch->open(WorkspaceSearch::Mode::Commands, {});
        return;
    }

    const QSettings settings(m_paths.settingsFile(), QSettings::IniFormat);
    const auto routes = settings.value(QStringLiteral("workspace/recentPages")).toStringList();
    QList<QPair<QString, QString>> recent;
    for (const auto &route : routes) {
        if (!isRecentEntityRoute(route)) continue;
        const auto title = settings.value(QStringLiteral("workspace/titles/") + route,
                                          m_routeTitles.value(route)).toString();
        if (title.isEmpty()) continue;
        recent.append({route, title});
        if (recent.size() == recentWorkspaceLimit) break;
    }
    m_globalSearch->open(WorkspaceSearch::Mode::Entities, std::move(recent));
}

void MainWindow::openWorkspaceSearchResult(const QString &route)
{
    navigateTo(route, true, ActressOpenMode::Automatic, nullptr, ShelfFilterPolicy::ResetToRandomIfExcluded);
}

QWidget *MainWindow::partialCaptureTarget()
{
    auto *pane = m_workspace->activePane();
    auto *page = pane ? pane->contentWidget(pane->currentContentId()) : nullptr;
    if (auto *detail = qobject_cast<PersonDetailPage *>(page))
        return detail->captureContent();
    if (auto *chart = qobject_cast<StatisticsChartPage *>(page))
        return chart->captureContent();
    return page;
}

void MainWindow::captureWidget(QWidget *widget)
{
    if (widget == nullptr || widget->size().isEmpty())
        return;

    QImage screenshot = widget->grab().toImage();
    screenshot.setColorSpace(ThemeService::outputColorSpace());
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
    const QString settingsFile = m_paths.settingsFile();
    AppSettings appSettings = settings::app(settingsFile);
    auto primary = OklchColor::fromString(appSettings.customPrimaryOklch);
    if (!primary)
        primary = OklchColor::fromHex(appSettings.customPrimary, ThemeTokens::defaultColorSpace);
    if (!m_themeService.setTheme(theme, primary ? primary->hex(ThemeService::colorMode()) : QString()))
    {
        return;
    }

    appSettings.themeId = ThemeService::toSettings(theme);
    settings::saveApp(appSettings, settingsFile);
}

void MainWindow::refreshPersonalRecordPanels()
{
    if (auto *page = qobject_cast<TopActressesPage *>(m_routePages.value(topActressesRoute).data()))
        page->refresh();
    if (auto *page = qobject_cast<RecordHeatmapPage *>(m_routePages.value(recordHeatmapRoute).data()))
        page->refresh();
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

        if (auto *workState = qobject_cast<WorkBatchStateWidget *>(lazyPage)) {
            if (works)
                workState->refresh();
        } else if (auto *workPage = qobject_cast<WorkPage *>(lazyPage)) {
            if (workDataChanged || workFavorites)
                workPage->refreshFromWorkspace(tags, references);
        } else if (auto *shelf = qobject_cast<ShelfPage *>(lazyPage)) {
            if (workDataChanged)
                shelf->refreshFromWorkspace(tags, references);
            else if (workFavorites)
                shelf->refreshCurrentFavorite();
        } else if (auto *personPage = qobject_cast<PersonPage *>(lazyPage)) {
            if (personPage->kind() == PersonKind::Actress ? actresses || actressFavorites : actors)
                personPage->refresh();
        } else if (auto *detail = qobject_cast<PersonDetailPage *>(lazyPage)) {
            if ((actresses || works || tags || references) && detail->currentPersonId() > 0)
                detail->showPerson(detail->currentPersonId());
            else if (actressFavorites)
                detail->refreshFavorite();
        } else if (auto *editor = qobject_cast<AddWorkTabPage3 *>(lazyPage)) {
            if (people || tags)
                editor->refreshAssociations();
            if (references)
                editor->refreshReferences(true);
            if (works)
                editor->refreshCompletions();
        } else if (auto *overview = qobject_cast<LibraryOverviewPage *>(lazyPage)) {
            if (works || people || tags || workFavorites || actressFavorites)
                overview->refresh();
        } else if (auto *topActresses = qobject_cast<TopActressesPage *>(lazyPage)) {
            if (works || actresses)
                topActresses->refresh();
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
    if (!m_timelinePage || !m_timelinePage->isInitialized()) return;
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
        scheduleWorkspaceSave();
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

void MainWindow::showWorkInShelfWithFilterFallback(qint64 workId)
{
    if (workId > 0)
        navigateTo(QStringLiteral("work_detail/") + QString::number(workId), true,
                   ActressOpenMode::Automatic, nullptr, ShelfFilterPolicy::ResetToRandomIfExcluded);
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

std::optional<myads::ContentConfig> MainWindow::workspaceContent(const QJsonObject &descriptor)
{
    const auto route = descriptor.value(QStringLiteral("content_id")).toString();
    const bool existingPage = !m_routePages.value(route).isNull();
    auto *page = ensurePage(route, descriptor.value(QStringLiteral("actress_id")).toString().toLongLong());
    if (!page) return std::nullopt;
    if (!existingPage && page != m_workFilter) page->setParent(nullptr);
    myads::ContentConfig config{.contentId = route, .widget = page, .title = page->windowTitle()};
    if (page == m_workFilter) config.closePolicy = myads::ClosePolicy::KeepAlive;
    return config;
}

bool MainWindow::restoreWorkspacePreset(const QString &resource, bool showFailureDialog)
{
    initializeResources();
    QString error;
    if (m_workspace->loadLayout(resource, [this](const QJsonObject &descriptor) {
            return workspaceContent(descriptor);
        }, &error)) {
        m_workspace->setSidebarOpen(myads::SidebarSide::Left, true);
        m_workspace->setSidebarOpen(myads::SidebarSide::Right, true);
        return true;
    }
    if (error.isEmpty()) return false;
    if (showFailureDialog)
        QMessageBox::warning(this, QStringLiteral("恢复预设工作区布局失败"), error);
    else
        qWarning() << "Workspace preset restore:" << error;
    return false;
}

void MainWindow::closeCurrentTab()
{
    auto *pane = m_workspace->activePane();
    if (pane) pane->removeContent(pane->currentContentId());
}

void MainWindow::registerPage(const QString &menuTitle, const QString &routeName, const QString &icon)
{
    // Registration is metadata only; each page is created on demand.
    m_routeTitles.insert(routeName, menuTitle);
    if (!icon.isEmpty()) m_navigationPages.append({routeName, icon});
}

QMap<QString, QString> MainWindow::workspaceCommandPages() const
{
    auto pages = m_routeTitles;
    pages.insert(QStringLiteral("setting"), QStringLiteral("设置"));
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
        auto *results = new WorkspaceSearch(m_publicDatabase, {}, nullptr,
                                            WorkspaceSearch::Presentation::ResultsPage);
        connect(results, &WorkspaceSearch::openRequested, this, &MainWindow::openWorkspaceSearchResult);
        const auto query = QUrl::fromPercentEncoding(argument.toLatin1());
        results->search(query);
        widget = results;
        title = QStringLiteral("搜索 · ") + query;
    } else if (base == QStringLiteral("fanart_work") && id > 0) {
        const auto work = WorkRepository(m_publicDatabase).findById(id);
        if (!work) return nullptr;
        auto *fanart = static_cast<FanartBrowserPage *>(createPage(QStringLiteral("fanart"), nullptr));
        fanart->showWork(id);
        widget = fanart;
        title = work->serialNumber + QStringLiteral(" · 剧照");
    } else if (base == QStringLiteral("actress_detail")) {
        const bool switching = routeName == actressSwitchRoute;
        const auto personId = switching ? switchActressId : argument.section(u'/', 0, 0).toLongLong();
        if (!switching && personId <= 0) return nullptr;
        auto *detail = static_cast<PersonDetailPage *>(createPage(base, nullptr));
        const bool loaded = personId > 0 && detail->showPerson(personId, false);
        if (!switching && !loaded) {
            delete detail;
            return nullptr;
        }
        widget = detail;
        if (!switching && !detail->currentPersonName().isEmpty()) title = detail->currentPersonName();
    } else if ((base == QStringLiteral("modify_actress") || base == QStringLiteral("modify_actor")) && id > 0) {
        const auto kind = base == QStringLiteral("modify_actress") ? PersonKind::Actress : PersonKind::Actor;
        PersonSearch query;
        query.kind = kind;
        query.includedIds = {id};
        query.restrictToIncludedIds = true;
        query.limit = 1;
        const auto people = PersonRepository(m_publicDatabase).search(query);
        if (people.isEmpty()) return nullptr;
        auto *editor = static_cast<PersonEditorPage *>(createPage(base, nullptr));
        editor->setInitialPerson(kind, id);
        widget = editor;
        title = QStringLiteral("编辑 · ") + people.first().name;
    } else if (base == QStringLiteral("work_edit") && id > 0) {
        const auto work = WorkRepository(m_publicDatabase).findById(id);
        if (!work) return nullptr;
        auto *editor = static_cast<AddWorkTabPage3 *>(createPage(base, nullptr));
        editor->loadWorkAsync(id);
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
        widget = createPage(base, nullptr);
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

QWidget *MainWindow::createPage(const QString &routeName, QWidget *parent)
{
    if (routeName == QStringLiteral("home"))
    {
        auto *home = new HomePage(m_publicDatabase, m_paths.workCoverDirectory(), m_themeService, parent);
        connect(home, &HomePage::detailRequested, this, &MainWindow::showWorkInShelfWithFilterFallback);
        return home;
    }
    if (routeName == QStringLiteral("mutiwork"))
    {
        auto *works = new WorkPage(m_publicDatabase, m_themeService,
                                   m_paths.workCoverDirectory(), m_privateDatabase, parent, m_workFilter);
        connect(works, &WorkPage::detailRequested, this, &MainWindow::showWorkInShelf);
        connect(works, &WorkPage::editRequested, this, &MainWindow::openWorkEditor);
        connect(works, &WorkPage::filteredWorksChanged, this, [this, works] {
            if (m_localGraphSource == works) updateLocalGraph();
            if (m_timelineSource == works) updateTimeline();
        });
        return works;
    }
    if (routeName == QStringLiteral("fanart"))
    {
        auto *fanart = new FanartBrowserPage(
            m_publicDatabase, m_themeService, m_paths.fanartDirectory(),
            m_paths.workCoverDirectory(), settings::crawler().coverFetchApiUrl, parent);
        connect(fanart, &FanartBrowserPage::closeRequested, this, [this, fanart] {
            const auto route = fanart->property("workspaceRoute").toString();
            if (auto *pane = m_workspace->findPaneByContentId(route)) pane->removeContent(route);
        });
        connect(fanart, &FanartBrowserPage::fanartChanged, this, [this](qint64 workId) {
            queueWorkspaceRefresh(WorksChanged, workId);
        });
        return fanart;
    }
    if (routeName == QStringLiteral("actress") || routeName == QStringLiteral("actor"))
    {
        const bool isActress = routeName == QStringLiteral("actress");
        auto *people = new PersonPage(
            isActress ? PersonKind::Actress : PersonKind::Actor, m_publicDatabase, m_privateDatabase,
            m_themeService, isActress ? m_paths.actressImageDirectory() : m_paths.actorImageDirectory(),
            parent);
        connect(people, &PersonPage::detailRequested, this, &MainWindow::openPerson);
        connect(people, &PersonPage::editRequested, this, &MainWindow::openPersonEditor);
        if (isActress) {
            connect(people, &PersonPage::filteredPeopleChanged, this, [this, people] {
                if (m_localGraphSource == people) updateLocalGraph();
            });
        }
        return people;
    }
    if (routeName == QStringLiteral("actress_detail"))
    {
        auto *detail = new PersonDetailPage(
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
    if (routeName == QStringLiteral("work_edit"))
    {
        auto *editor = new AddWorkTabPage3(m_publicDatabase, m_themeService, *m_crawlerScheduler,
                                          m_paths.workCoverDirectory(), m_paths.fanartDirectory(),
                                          settings::crawler().coverFetchApiUrl, parent,
                                          m_paths.actressImageDirectory(), m_paths.actorImageDirectory());
        editor->setWorkCreationEnabled(false);
        connect(editor, &AddWorkTabPage3::workSaved, this,
                [this](qint64 workId, bool) { queueWorkspaceRefresh(WorksChanged, workId); });
        connect(editor, &AddWorkTabPage3::referencesChanged, this,
                [this] { queueWorkspaceRefresh(ReferencesChanged); });
        connect(editor, &AddWorkTabPage3::tagsChanged, this,
                [this] { queueWorkspaceRefresh(TagsChanged); });
        connect(editor, &AddWorkTabPage3::peopleChanged, this, [this](PersonKind kind) {
            queueWorkspaceRefresh(kind == PersonKind::Actress ? ActressesChanged : ActorsChanged);
        });
        connect(editor, &AddWorkTabPage3::actressesCreated, this, &MainWindow::enqueueActressSyncs);
        connect(editor, &AddWorkTabPage3::workLinkRequested, this, &MainWindow::showWorkInShelf);
        connect(editor, &AddWorkTabPage3::personEditRequested, this, &MainWindow::openPersonEditor);
        connect(editor, &AddWorkTabPage3::personDetailRequested, this,
                [this](PersonKind kind, qint64 id) { openPerson(kind, id); });
        connect(editor, &AddWorkTabPage3::actressLinkRequested, this, [this](qint64 actressId) {
            openPerson(PersonKind::Actress, actressId);
        });
        return editor;
    }
    if (routeName == QStringLiteral("manage_tags"))
    {
        auto *page = new TagManagementWidget(m_publicDatabase, m_themeService, parent);
        const auto refreshTags = [this] { queueWorkspaceRefresh(TagsChanged); };
        connect(page, &TagManagementWidget::tagsChanged, this, refreshTags);
        connect(page, &TagManagementWidget::tagTypesChanged, this, refreshTags);
        return page;
    }
    if (routeName == QStringLiteral("manage_makers"))
    {
        auto *page = new MakerManagementWidget(m_publicDatabase, m_themeService, parent);
        const auto refreshReferences = [this] { queueWorkspaceRefresh(ReferencesChanged); };
        connect(page, &MakerManagementWidget::referencesChanged, this, refreshReferences);
        connect(page, &MakerManagementWidget::prefixesChanged, this, refreshReferences);
        return page;
    }
    if (routeName == QStringLiteral("manage_labels") || routeName == QStringLiteral("manage_series"))
    {
        const auto kind = routeName == QStringLiteral("manage_labels") ? ReferenceKind::Label : ReferenceKind::Series;
        auto *page = new LabelManagementWidget(kind, m_publicDatabase, m_themeService, parent);
        connect(page, &LabelManagementWidget::referencesChanged, this,
                [this] { queueWorkspaceRefresh(ReferencesChanged); });
        return page;
    }
    if (routeName == QStringLiteral("manage_summary"))
    {
        auto *page = new SummaryQueryWidget(m_publicDatabase, m_themeService, parent);
        connect(page, &SummaryQueryWidget::workRequested, this, &MainWindow::openWorkEditor);
        return page;
    }
    if (routeName == QStringLiteral("manage_records"))
        return new PersonalRecordManagementWidget(m_privateDatabase, m_themeService, parent);
    if (routeName == QStringLiteral("manage_deleted") || routeName == QStringLiteral("manage_trash"))
    {
        const bool recycleBin = routeName == QStringLiteral("manage_trash");
        auto *page = new WorkBatchStateWidget(recycleBin ? WorkStateMode::RecycleBin : WorkStateMode::Active,
                                              m_publicDatabase, m_themeService,
                                              recycleBin ? m_paths.workCoverDirectory() : QString(),
                                              recycleBin ? m_paths.fanartDirectory() : QString(), parent);
        connect(page, &WorkBatchStateWidget::worksChanged, this,
                [this] { queueWorkspaceRefresh(WorksChanged); });
        return page;
    }
    if (routeName == QStringLiteral("chart"))
        return new StatisticsChartPage(m_publicDatabase, m_privateDatabase, m_themeService, parent);
    if (routeName == libraryOverviewRoute)
        return new LibraryOverviewPage(m_publicDatabase, m_privateDatabase, m_themeService, parent);
    if (routeName == recordHeatmapRoute)
        return new RecordHeatmapPage(m_publicDatabase, m_privateDatabase, m_themeService, parent);
    if (routeName == topActressesRoute)
    {
        auto *page = new TopActressesPage(m_publicDatabase, m_privateDatabase, m_themeService,
                                          m_paths.actressImageDirectory(), parent);
        connect(page, &TopActressesPage::actressDetailRequested, this,
                [this](qint64 actressId) { openPerson(PersonKind::Actress, actressId); });
        connect(page, &TopActressesPage::actressEditRequested, this,
                [this](qint64 actressId) { openPersonEditor(PersonKind::Actress, actressId); });
        return page;
    }
    if (routeName == QStringLiteral("timeline"))
    {
        auto *timeline = new ActressWorkTimeline(parent, m_paths.workCoverDirectory(),
                                                 m_paths.actressImageDirectory(), &m_themeService);
        connect(timeline, &ActressWorkTimeline::workRequested, this, &MainWindow::showWorkInShelfWithFilterFallback);
        m_timelinePage = timeline;
        connect(timeline, &ActressWorkTimeline::initialized, this, &MainWindow::updateTimeline);
        return timeline;
    }
    if (routeName == QStringLiteral("graph") || routeName == QStringLiteral("local_graph"))
    {
        const bool localGraph = routeName == QStringLiteral("local_graph");
        auto *graph = new ForceDirectPage(m_themeService, *m_graphManager, parent, &m_paths, localGraph);
        connect(graph, &ForceDirectPage::workRequested, this, [this](qint64 workId) {
            navigateTo(QStringLiteral("work_detail/%1").arg(workId), true,
                       ActressOpenMode::Automatic, nullptr, ShelfFilterPolicy::ClearIfExcluded);
        }, Qt::QueuedConnection);
        connect(graph, &ForceDirectPage::actressRequested, this, [this](qint64 actressId) {
            openPerson(PersonKind::Actress, actressId);
        });
        if (localGraph) {
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
        return new AvPage(m_paths.wikiDirectory(), m_themeService, parent);
    }
    if (routeName == QStringLiteral("inbox"))
    {
        return new InboxPage(*m_crawlerScheduler, *m_crawlerPersistence, parent);
    }
    return nullptr;
}

void MainWindow::navigateTo(const QString &routeName, bool recordRecent, ActressOpenMode actressMode,
                            myads::PaneWidget *preferredPane, ShelfFilterPolicy shelfFilter)
{
    for (const auto &preset : workspacePresets) {
        if (routeName == preset.command) {
            m_globalSearch->hide();
            restoreWorkspacePreset(preset.resource);
            return;
        }
    }
    if (routeName == QStringLiteral("setting")) {
        openSettings();
        return;
    }
    if (std::ranges::any_of(dialogNavigationActions, [&routeName](const auto &entry) {
            return entry.first == routeName;
        })) {
        m_globalSearch->hide();
        m_shortcutActions->findChild<QAction *>(routeName)->trigger();
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
        ensureSettingsDialog();
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
            if (shelfFilter == ShelfFilterPolicy::ResetToRandomIfExcluded)
                shelf->resetWorkFilters(WorkSortOrder::Random);
            else
                shelf->resetWorkFilters();
            if (!shelf->containsWork(work->id)) return;
        }
    }
    auto *pane = m_workspace->findPaneByContentId(pageRoute);
    if (switchActressId > 0) {
        auto *detail = static_cast<PersonDetailPage *>(widget);
        if (detail->currentPersonId() != switchActressId) {
            if (!detail->showPerson(switchActressId)) return;
            scheduleWorkspaceSave();
            if (m_localGraphSource == detail) updateLocalGraph();
            if (m_timelineSource == detail) updateTimeline();
        }
        recentTitle = detail->currentPersonName();
    }
    if (!pane) {
        pane = destination.data();
        if (!preferredPane && pageRoute == QStringLiteral("inbox"))
            pane = m_workspace->sidebarPane(myads::SidebarSide::Right);
        if (!pane) pane = m_workspace->rootPane();
        myads::ContentConfig config{.contentId = pageRoute, .widget = widget, .title = widget->windowTitle()};
        if (widget == m_workFilter) config.closePolicy = myads::ClosePolicy::KeepAlive;
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
    if (recordRecent && isRecentEntityRoute(routeName)) {
        QSettings settings(m_paths.settingsFile(), QSettings::IniFormat);
        auto recent = settings.value(QStringLiteral("workspace/recentPages")).toStringList();
        recent.removeAll(routeName);
        recent.prepend(routeName);
        qsizetype entities = 0;
        recent.removeIf([&](const QString &route) {
            if (isRecentEntityRoute(route) && ++entities <= recentWorkspaceLimit) return false;
            settings.remove(QStringLiteral("workspace/titles/") + route);
            return true;
        });
        settings.setValue(QStringLiteral("workspace/recentPages"), recent);
        settings.setValue(QStringLiteral("workspace/titles/") + routeName,
                          recentTitle.isEmpty() ? widget->windowTitle() : recentTitle);
    }
    if (auto *chart = qobject_cast<StatisticsChartPage *>(widget))
        chart->initialize();
}

void MainWindow::updateSidebarSettings(const AppSettings &settings)
{
    using myads::SidebarSide;
    m_workspace->setSidebarAutoCollapse(SidebarSide::Left, settings.leftSidebarAutoCollapse);
    m_workspace->setSidebarAutoCollapse(SidebarSide::Right, settings.rightSidebarAutoCollapse);
    m_workspace->setSidebarToggleVisible(SidebarSide::Left, settings.leftSidebarToggleVisible);
    m_workspace->setSidebarToggleVisible(SidebarSide::Right, settings.rightSidebarToggleVisible);
}

void MainWindow::ensureSettingsDialog()
{
    if (!m_settingsDialog) {
        QList<QPair<QString, QString>> navigationItems;
        for (const auto *button : m_navigationButtons)
            navigationItems.append({button->property("navigationId").toString(), button->accessibleName()});
        auto *settings = new SettingsDialog(m_themeService, *m_shortcutActions,
                                            m_publicDatabase, m_privateDatabase, m_paths, this,
                                            navigationItems);
        m_settingsDialog = settings;
        connect(settings, &SettingsDialog::navigationBarSettingsChanged, this, [this] {
            updateNavigationBar(settings::app(m_paths.settingsFile()));
        });
        connect(settings, &SettingsDialog::sidebarSettingsChanged, this, [this] {
            updateSidebarSettings(settings::app(m_paths.settingsFile()));
        });
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
}

void MainWindow::openSettings()
{
    ensureSettingsDialog();
    if (!m_settingsDialog->isVisible()) {
#ifdef Q_OS_MACOS
        // QDialog::open() presents a native sheet on Cocoa, which draws its own rounded frame.
        m_settingsDialog->setWindowModality(Qt::ApplicationModal);
        m_settingsDialog->show();
#else
        m_settingsDialog->open();
#endif
    }
    m_settingsDialog->raise();
    m_settingsDialog->activateWindow();
}

void MainWindow::openPersonEditor(PersonKind kind, qint64 personId)
{
    if (personId > 0) navigateTo((kind == PersonKind::Actress ? QStringLiteral("modify_actress/")
        : QStringLiteral("modify_actor/")) + QString::number(personId));
}

void MainWindow::scheduleWorkspaceSave()
{
    if (m_workspaceSaveTimer) m_workspaceSaveTimer->start();
}

void MainWindow::saveWorkspace() const
{
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
