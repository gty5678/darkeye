#include "ui/pages/ShelfPage.h"
#include "ui/pages/PageScrollArea.h"
#include "ui/components/DvdShelfView.h"
#include "ui/components/WorkFilter.h"
#include "database/SqliteConnection.h"
#include "database/repositories/WorkRepository.h"

#include <QVBoxLayout>

namespace darkeye
{

ShelfPage::ShelfPage(QSqlDatabase database, QSqlDatabase privateDatabase,
                     ThemeService &themeService, QString coverDirectory, QString fanartDirectory,
                     QWidget *parent, WorkFilter *filter)
    : LazyWidget(parent), m_database(std::move(database)),
      m_privateDatabase(std::move(privateDatabase)), m_themes(themeService),
      m_coverDirectory(std::move(coverDirectory)), m_fanartDirectory(std::move(fanartDirectory)),
      m_filter(filter ? filter : new WorkFilter(m_database, m_privateDatabase, themeService, this))
{
    setObjectName(QStringLiteral("ShelfPage"));
}

void ShelfPage::lazyLoad()
{
    m_filter->initialize();
    auto *content = addPageScrollArea(this);
    auto *root = new QVBoxLayout(content);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);
    if (m_filter->parentWidget() == this) m_filter->hide();
    m_shelfView = new DvdShelfView(m_database, m_privateDatabase, m_coverDirectory,
                                   m_fanartDirectory, content);
    m_shelfView->bindTheme(m_themes);
    root->addWidget(m_shelfView, 1);
    connect(m_shelfView, &DvdShelfView::graphContextChanged, this, &ShelfPage::graphContextChanged);
    connect(m_shelfView, &DvdShelfView::focusedWorkChanged, this, &ShelfPage::focusedWorkChanged);
    connect(m_shelfView, &DvdShelfView::expandedWorkChanged, this, &ShelfPage::expandedWorkChanged);
    connect(m_shelfView, &DvdShelfView::editRequested, this, &ShelfPage::editRequested);
    connect(m_shelfView, &DvdShelfView::favoriteChanged, this, &ShelfPage::favoriteChanged);
    connect(m_shelfView, &DvdShelfView::workDeleted, this, [this](qint64 workId) {
        refreshData();
        emit workDeleted(workId);
    });
    connect(m_shelfView, &DvdShelfView::actressRequested, this, &ShelfPage::actressRequested);
    connect(m_shelfView, &DvdShelfView::actorRequested, this, &ShelfPage::actorRequested);
    connect(m_shelfView, &DvdShelfView::tagRequested, this, &ShelfPage::filterByTag, Qt::QueuedConnection);
    connect(m_shelfView, &DvdShelfView::directorRequested, this, &ShelfPage::filterByDirector);
    connect(m_shelfView, &DvdShelfView::makerRequested, this, &ShelfPage::filterByMaker);
    connect(m_shelfView, &DvdShelfView::labelRequested, this, &ShelfPage::filterByLabel);
    connect(m_shelfView, &DvdShelfView::seriesRequested, this, &ShelfPage::filterBySeries);
    connect(m_filter, &WorkFilter::changed, this, &ShelfPage::refreshData);
    refreshData();
}

void ShelfPage::refresh()
{
    initialize();
    if (!m_filter->flush()) refreshData();
}

void ShelfPage::refreshFromWorkspace(bool tagsChanged, bool referencesChanged)
{
    initialize();
    const qint64 workId = currentWorkId();
    if (isAncestorOf(m_filter))
        m_filter->refreshOptions(tagsChanged, referencesChanged);
    refreshData();
    if (workId > 0) m_shelfView->openWork(workId);
}

void ShelfPage::refreshCurrentFavorite()
{
    if (!m_shelfView) return;
    if (m_filter->search().restrictToIncludedWorkIds)
        refreshFromWorkspace(false, false);
    else
        m_shelfView->refreshCurrentFavorite();
}

void ShelfPage::filterByDirector(const QString &director)
{
    initialize();
    m_filter->filterByDirector(director);
}

void ShelfPage::filterByActor(qint64 actorId, const QString &name)
{
    initialize();
    m_filter->filterByActor(actorId, name);
}

void ShelfPage::filterByMaker(qint64 makerId)
{
    initialize();
    m_filter->filterByReference(WorkFilter::Reference::Maker, makerId);
}

void ShelfPage::filterByLabel(qint64 labelId)
{
    initialize();
    m_filter->filterByReference(WorkFilter::Reference::Label, labelId);
}

void ShelfPage::filterBySeries(qint64 seriesId)
{
    initialize();
    m_filter->filterByReference(WorkFilter::Reference::Series, seriesId);
}

void ShelfPage::filterByTag(qint64 tagId)
{
    initialize();
    m_filter->filterByTag(tagId);
}

void ShelfPage::resetWorkFilters(std::optional<WorkSortOrder> order)
{
    initialize();
    m_filter->resetToPublicLibrary(order);
}

bool ShelfPage::containsWork(qint64 workId)
{
    initialize();
    m_filter->flush();
    return filteredWorkIds().contains(workId);
}

bool ShelfPage::showWork(qint64 workId)
{
    initialize();
    m_filter->flush();
    return workId > 0 && m_shelfView->openWork(workId);
}

qint64 ShelfPage::currentWorkId() const
{
    return m_shelfView ? m_shelfView->currentWorkId() : 0;
}

qint64 ShelfPage::focusedWorkId() const
{
    return m_shelfView ? m_shelfView->focusedWorkId() : 0;
}

bool ShelfPage::isReturningToShelf() const
{
    return m_shelfView && m_shelfView->isReturningToShelf();
}

qint64 ShelfPage::expandedWorkId() const
{
    return m_shelfView ? m_shelfView->expandedWorkId() : 0;
}

QSet<qint64> ShelfPage::filteredWorkIds() const
{
    return m_shelfView ? m_shelfView->displayedWorkIds() : QSet<qint64>{};
}

QList<WorkSummary> ShelfPage::filteredWorks() const
{
    return m_shelfView ? m_shelfView->displayedWorks() : QList<WorkSummary>{};
}

void ShelfPage::refreshData()
{
    QString error;
    SqliteConnection connection;
    if (!connection.open(m_database.databaseName(), true, &error)) return;
    WorkSearch search = m_filter->search();
    // The 3D scene virtualizes the submitted items while keeping the full result set.
    search.limit = 0;
    search.offset = 0;
    const auto works = WorkRepository(connection.database()).search(search, &error);
    if (!error.isEmpty()) return;
    m_shelfView->setWorks(works);
    m_filter->updateCount(works.size());
    emit graphContextChanged();
    emit filteredWorksChanged();
}

} // namespace darkeye
