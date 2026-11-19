#pragma once

#include "settings/Paths.h"
#include "darkeye_ui/theme/ThemeService.h"
#include "domain/Person.h"
#include "settings/Settings.h"
#include "graph/GraphManager.h"
#include "crawler/CrawlerScheduler.h"
#include "services/CrawlerPersistenceService.h"
#include "services/ActressSyncService.h"

#include <QFlags>
#include <QMainWindow>
#include <QMap>
#include <QQueue>
#include <QSqlDatabase>
#include <QSet>
#include <QPointer>

#include <memory>

class QActionGroup;
class QComboBox;
class QStackedWidget;

namespace darkeye
{

namespace myads { class WorkspaceWidget; }
class WorkspaceSearch;
class Sidebar;
class DashboardPage;
class HomePage;
class ActorPage;
class ActressDetailPage;
class ActressWorkTimeline;
class ActressPage;
class PersonDetailPage;
class ModifyActressPage;
class ModifyActorPage;
class PersonPage;
class StatisticsPage;
class WorkPage;
class ShelfPage;
class ForceDirectPage;
class FanartBrowserPage;
class LocalApiServer;

class MainWindow final : public QMainWindow
{
public:
    explicit MainWindow(ThemeService &themeService,
                        QSqlDatabase publicDatabase = {}, QSqlDatabase privateDatabase = {},
                        settings::Paths paths = {},
                        QWidget *parent = nullptr);

    void showInitial();
    // warmGraphics requires a visible window with its graphics prewarm complete.
    void preloadShelfPage(bool warmGraphics = false);
    /// Connects the HTTP bridge after both the window and local API are constructed.
    void setLocalApiServer(LocalApiServer &api);

protected:
    void changeEvent(QEvent *event) override;
    void closeEvent(QCloseEvent *event) override;

private:
    void buildUi();
    void openPerson(PersonKind kind, qint64 personId);
    void openWorkEditor(qint64 workId);
    void closeCurrentTab();
    QString workspaceFile() const;
    void updateWindowTitle();
    void registerPage(const QString &menuTitle, const QString &routeName);
    QWidget *createPage(const QString &menuTitle, const QString &routeName,
                        QWidget *parent);
    QWidget *ensurePage(const QString &routeName);
    QMap<QString, QString> workspaceCommandPages() const;
    void populateThemeSelector();
    void changeTheme(int index);
    void saveWindowState() const;
    void navigateTo(const QString &routeName, bool recordRecent = true);
    void openPersonEditor(PersonKind kind, qint64 personId);
    enum WorkspaceChange {
        WorksChanged = 0x01,
        ActressesChanged = 0x02,
        ActorsChanged = 0x04,
        TagsChanged = 0x08,
        ReferencesChanged = 0x10,
        WorkFavoritesChanged = 0x20,
        ActressFavoritesChanged = 0x40,
    };
    Q_DECLARE_FLAGS(WorkspaceChanges, WorkspaceChange)
    void queueWorkspaceRefresh(WorkspaceChanges changes, qint64 workId = 0);
    void refreshWorkspace();
    void connectShelfPage(ShelfPage *shelf);
    void updateLocalGraphFocus();
    void updateLocalGraph();
    void updateTimelineFocus();
    void updateTimeline();
    void refreshStatisticsPages();
    void showWorkInShelf(qint64 workId);
    void registerApplicationActions();
    void openWorkspaceSearch(bool commands);
    [[nodiscard]] QWidget *partialCaptureTarget();
    void captureWidget(QWidget *widget);
    void attachLocalApi(LocalApiServer &api);
    void startNextActressSync();
    void enqueueActressSyncs(const QList<qint64> &actressIds);

    ThemeService &m_themeService;
    settings::Paths m_paths;
    QSqlDatabase m_publicDatabase;
    QSqlDatabase m_privateDatabase;
    std::unique_ptr<graph::GraphManager> m_graphManager;
    std::unique_ptr<CrawlerScheduler> m_crawlerScheduler;
    std::unique_ptr<CrawlerPersistenceService> m_crawlerPersistence;
    std::unique_ptr<ActressSyncService> m_backgroundActressSync;
    QQueue<qint64> m_pendingActressSyncs;
    QSet<qint64> m_pendingActressSyncIds;
    myads::WorkspaceWidget *m_workspace = nullptr;
    WorkspaceSearch *m_globalSearch = nullptr;
    QActionGroup *m_shortcutActions = nullptr;
    QPointer<HomePage> m_homePage;
    QPointer<DashboardPage> m_dashboardPage;
    QPointer<WorkPage> m_workPage;
    QPointer<ShelfPage> m_shelfPage;
    QPointer<ForceDirectPage> m_localGraphPage;
    QPointer<QWidget> m_localGraphSource;
    QPointer<ActressWorkTimeline> m_timelinePage;
    QPointer<QWidget> m_timelineSource;
    QPointer<FanartBrowserPage> m_fanartBrowserPage;
    QPointer<ActressPage> m_actressPage;
    QPointer<ActorPage> m_actorPage;
    QPointer<ActressDetailPage> m_actressDetailPage;
    QPointer<ModifyActressPage> m_actressEditorPage;
    QPointer<ModifyActorPage> m_actorEditorPage;
    QPointer<QComboBox> m_themeSelector;
    WorkspaceChanges m_pendingWorkspaceChanges;
    QSet<qint64> m_pendingChangedWorkIds;
    bool m_refreshAllGraphWorks = false;
    QMap<QString, QString> m_routeTitles;
    QMap<QString, QPointer<QWidget>> m_routePages;
};

} // namespace darkeye
