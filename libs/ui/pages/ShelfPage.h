#pragma once

#include "darkeye_ui/base/LazyWidget.h"
#include "darkeye_ui/theme/ThemeService.h"
#include "domain/Work.h"

#include <QSet>
#include <QSqlDatabase>

class QComboBox;
class QLabel;
class QTimer;

namespace darkeye
{

class CompleterLineEdit;
class DesignLineEdit;
class IconButton;
class DvdShelfView;
class WorkTagSelector;
class MakerSelector;

// Uses the same filtering contract as Python's DVD shelf.  The native 3D
// renderer is intentionally isolated from this data and interaction layer.
class ShelfPage final : public LazyWidget
{
    Q_OBJECT

public:
    static constexpr auto DefaultFilterBarShortcut = Qt::Key_F;

    explicit ShelfPage(QSqlDatabase database, QSqlDatabase privateDatabase,
                       ThemeService &themeService,
                       QString coverDirectory = {}, QString fanartDirectory = {},
                       QWidget *parent = nullptr);

    void toggleFilterBar();
    void refresh();
    void refreshAfterCapture();
    void refreshReferences();
    void refreshTags();
    void refreshFromWorkspace(bool tagsChanged, bool referencesChanged);
    void refreshCurrentFavorite();
    void filterByDirector(const QString &director);
    void filterByActor(qint64 actorId, const QString &name);
    void filterByMaker(qint64 makerId);
    void filterByLabel(qint64 labelId);
    void filterBySeries(qint64 seriesId);
    void filterByTag(qint64 tagId);
    bool showWork(qint64 workId);
    [[nodiscard]] qint64 currentWorkId() const;
    [[nodiscard]] QSet<qint64> filteredWorkIds() const;
    [[nodiscard]] QList<WorkSummary> filteredWorks() const;

signals:
    void graphContextChanged();
    void filteredWorksChanged();
    void editRequested(qint64 workId);
    void workDeleted(qint64 workId);
    void favoriteChanged(qint64 workId, bool favorite);
    void actressRequested(qint64 actressId);
    void actorRequested(qint64 actorId);
    void tagRequested(qint64 tagId);

private:
    void lazyLoad() override;
    void buildUi();
    void applyFilters();
    void clearFilters();
    void clearFilterInputs();
    void toggleTagPanel();
    void reloadReferenceOptions();
    void selectCategoryTag();
    void refreshData();
    int reloadDvdScene();
    void resetForRoute();
    [[nodiscard]] WorkSearch currentSearch() const;
    [[nodiscard]] WorkSortOrder selectedSortOrder() const;

    qint64 m_categoryTagId = 0;
    std::optional<qint64> m_actorId;
    QSqlDatabase m_database;
    QSqlDatabase m_privateDatabase;
    ThemeService &m_themeService;
    QString m_coverDirectory;
    QString m_fanartDirectory;
    CompleterLineEdit *m_serialFilter = nullptr;
    CompleterLineEdit *m_actressFilter = nullptr;
    DesignLineEdit *m_titleFilter = nullptr;
    DesignLineEdit *m_notesFilter = nullptr;
    CompleterLineEdit *m_directorFilter = nullptr;
    CompleterLineEdit *m_actorFilter = nullptr;
    MakerSelector *m_makerFilter = nullptr;
    MakerSelector *m_labelFilter = nullptr;
    MakerSelector *m_seriesFilter = nullptr;
    QWidget *m_filterBar = nullptr;
    QComboBox *m_scopeSelector = nullptr;
    QComboBox *m_sortSelector = nullptr;
    WorkTagSelector *m_tagSelector = nullptr;
    QWidget *m_tagPanel = nullptr;
    IconButton *m_tagPanelButton = nullptr;
    DvdShelfView *m_shelfView = nullptr;
    QLabel *m_countLabel = nullptr;
    QTimer *m_filterTimer = nullptr;
    quint32 m_randomSeed = 1;
    quint32 m_randomSeed2 = 1;
    bool m_tagPanelVisible = true;
};

} // namespace darkeye
