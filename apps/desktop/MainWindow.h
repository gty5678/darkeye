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
#include <optional>

class QActionGroup;
class QComboBox;
class QLabel;
class QJsonObject;
class QPushButton;
class QTimer;

namespace darkeye
{

namespace myads { struct ContentConfig; class PaneWidget; class WorkspaceWidget; }
class WorkspaceSearch;
class SettingsDialog;
class ActressWorkTimeline;
class PersonDetailPage;
class ModifyActressPage;
class ModifyActorPage;
class ShelfPage;
class WorkFilter;
class ForceDirectPage;
class LocalApiServer;

class MainWindow final : public QMainWindow
{
public:
    explicit MainWindow(ThemeService &themeService,
                        QSqlDatabase publicDatabase = {}, QSqlDatabase privateDatabase = {},
                        settings::Paths paths = {},
                        QWidget *parent = nullptr);

    void showInitial();
    /// Connects the HTTP bridge after both the window and local API are constructed.
    void setLocalApiServer(LocalApiServer &api);

protected:
#ifdef Q_OS_WIN
    bool event(QEvent *event) override;
#endif
#ifdef Q_OS_MACOS
    void showEvent(QShowEvent *event) override;
#endif
    void changeEvent(QEvent *event) override;
    void closeEvent(QCloseEvent *event) override;

private:
    void buildUi();
    QWidget *buildNavigationBar(QWidget *parent);
    void updateNavigationBar(const AppSettings &settings);
    void updateSidebarSettings(const AppSettings &settings);
    void openPerson(PersonKind kind, qint64 personId);
    void openWorkEditor(qint64 workId);
    void closeCurrentTab();
    void restoreWorkspace();
    bool restoreWorkspacePreset(const QString &resource, bool showFailureDialog = true);
    QString workspaceFile() const;
    std::optional<myads::ContentConfig> workspaceContent(const QJsonObject &descriptor);
    void scheduleWorkspaceSave();
    void saveWorkspace() const;
#ifdef Q_OS_MACOS
    void updateMacTitleBarBackground();
#endif
#ifdef Q_OS_WIN
    void updateNativeTitleBarColors();
#endif
    void registerPage(const QString &menuTitle, const QString &routeName, const QString &icon = {});
    QWidget *createPage(const QString &routeName, QWidget *parent);
    QWidget *ensurePage(const QString &routeName, qint64 switchActressId = 0);
    QMap<QString, QString> workspaceCommandPages() const;
    void ensureSettingsDialog();
    void openSettings();
    void populateThemeSelector();
    void changeTheme(int index);
    enum class ActressOpenMode { Automatic, Switching, Independent };
    enum class ShelfFilterPolicy { Keep, ClearIfExcluded, ResetToRandomIfExcluded };
    void navigateTo(const QString &routeName, bool recordRecent = true,
                    ActressOpenMode actressMode = ActressOpenMode::Automatic,
                    myads::PaneWidget *preferredPane = nullptr,
                    ShelfFilterPolicy shelfFilter = ShelfFilterPolicy::Keep);
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
    void refreshPersonalRecordPanels();
    void showWorkInShelf(qint64 workId);
    void showWorkInShelfWithFilterFallback(qint64 workId);
    void registerApplicationActions();
    void openWorkspaceSearch(bool commands);
    void openWorkspaceSearchResult(const QString &route);
    [[nodiscard]] QWidget *partialCaptureTarget();
    void captureWidget(QWidget *widget);
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
    QWidget *m_navigationBar = nullptr;
    QLabel *m_navigationHint = nullptr;
    QList<QPushButton *> m_navigationButtons;
    myads::WorkspaceWidget *m_workspace = nullptr;
    QTimer *m_workspaceSaveTimer = nullptr;
    WorkFilter *m_workFilter = nullptr;
    WorkspaceSearch *m_globalSearch = nullptr;
    QActionGroup *m_shortcutActions = nullptr;
    QPointer<ShelfPage> m_shelfPage;
    QPointer<ForceDirectPage> m_localGraphPage;
    QPointer<QWidget> m_localGraphSource;
    QPointer<ActressWorkTimeline> m_timelinePage;
    QPointer<QWidget> m_timelineSource;
    QPointer<ModifyActressPage> m_actressEditorPage;
    QPointer<ModifyActorPage> m_actorEditorPage;
    QPointer<SettingsDialog> m_settingsDialog;
    QPointer<QComboBox> m_themeSelector;
    WorkspaceChanges m_pendingWorkspaceChanges;
    QSet<qint64> m_pendingChangedWorkIds;
    bool m_refreshAllGraphWorks = false;
    QList<QPair<QString, QString>> m_navigationPages;
    QMap<QString, QString> m_routeTitles;
    QMap<QString, QPointer<QWidget>> m_routePages;
};

} // namespace darkeye
