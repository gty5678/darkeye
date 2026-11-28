#pragma once

#include "darkeye_ui/base/LazyWidget.h"
#include "darkeye_ui/theme/ThemeService.h"
#include "domain/Work.h"

#include <QSet>
#include <QSqlDatabase>

namespace darkeye
{

class DvdShelfView;
class WorkFilter;

// Uses the same filtering contract as Python's DVD shelf.  The native 3D
// renderer is intentionally isolated from this data and interaction layer.
class ShelfPage final : public LazyWidget
{
    Q_OBJECT

public:
    explicit ShelfPage(QSqlDatabase database, QSqlDatabase privateDatabase,
                       ThemeService &themeService,
                       QString coverDirectory = {}, QString fanartDirectory = {},
                       QWidget *parent = nullptr, WorkFilter *filter = nullptr);

    void refresh();
    void refreshFromWorkspace(bool tagsChanged, bool referencesChanged);
    void refreshCurrentFavorite();
    void filterByDirector(const QString &director);
    void filterByActor(qint64 actorId, const QString &name);
    void filterByMaker(qint64 makerId);
    void filterByLabel(qint64 labelId);
    void filterBySeries(qint64 seriesId);
    void filterByTag(qint64 tagId);
    void resetWorkFilters(std::optional<WorkSortOrder> order = std::nullopt);
    [[nodiscard]] bool containsWork(qint64 workId);
    bool showWork(qint64 workId);
    [[nodiscard]] qint64 currentWorkId() const;
    [[nodiscard]] qint64 focusedWorkId() const;
    [[nodiscard]] bool isReturningToShelf() const;
    [[nodiscard]] qint64 expandedWorkId() const;
    [[nodiscard]] QSet<qint64> filteredWorkIds() const;
    [[nodiscard]] QList<WorkSummary> filteredWorks() const;

signals:
    void graphContextChanged();
    void focusedWorkChanged();
    void expandedWorkChanged();
    void filteredWorksChanged();
    void editRequested(qint64 workId);
    void workDeleted(qint64 workId);
    void favoriteChanged(qint64 workId, bool favorite);
    void actressRequested(qint64 actressId);
    void actorRequested(qint64 actorId);

private:
    void lazyLoad() override;
    void refreshData();

    QSqlDatabase m_database;
    QSqlDatabase m_privateDatabase;
    ThemeService &m_themes;
    QString m_coverDirectory;
    QString m_fanartDirectory;
    WorkFilter *m_filter;
    DvdShelfView *m_shelfView = nullptr;
};

} // namespace darkeye
