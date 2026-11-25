#include "ui/pages/WorkPage.h"

#include "darkeye_ui/components/LazyScrollArea.h"
#include "darkeye_ui/components/ToastNotification.h"
#include "database/SqliteConnection.h"
#include "database/repositories/PersonRepository.h"
#include "ui/components/WorkCard.h"
#include "ui/components/WorkFilter.h"

#include <QFutureWatcher>
#include <QPromise>
#include <QThreadPool>
#include <QVBoxLayout>

namespace darkeye
{
WorkPage::WorkPage(QSqlDatabase database, ThemeService &themeService,
                   QString coverDirectory, QSqlDatabase privateDatabase, QString fanartDirectory,
                   QUrl imageFetchEndpoint, QWidget *parent, WorkFilter *filter)
    : LazyWidget(parent), m_themeService(themeService), m_database(database),
      m_repository(std::move(database)),
      m_coverDirectory(std::move(coverDirectory)), m_fanartDirectory(std::move(fanartDirectory)),
      m_imageFetchEndpoint(std::move(imageFetchEndpoint)),
      m_filter(filter ? filter : new WorkFilter(m_database, std::move(privateDatabase), themeService, this))
{
}

WorkPage::~WorkPage()
{
    if (m_pageQuery) m_pageQuery->cancel();
}

void WorkPage::lazyLoad()
{
    m_filter->initialize();
    buildUi();
    connect(m_filter, &WorkFilter::changed, this, [this] { m_lazyArea->reset(); });
    m_lazyArea->setViewportItemHandler([](QWidget *widget, bool active) {
        static_cast<WorkCard *>(widget)->setCoverActive(active);
    });
    m_lazyArea->setAsyncLoader([this](quint64 request, int pageIndex, int pageSize) {
        loadCardPage(request, pageIndex, pageSize);
    });
}

void WorkPage::buildUi()
{
    setObjectName(QStringLiteral("WorkPage"));
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    if (m_filter->parentWidget() == this) m_filter->hide();

    m_lazyArea = new LazyScrollArea(250, this);
    m_lazyArea->setPageSize(70);
    m_lazyArea->setUniformRows(true);
    root->addWidget(m_lazyArea, 1);
}

void WorkPage::refresh()
{
    const bool initialized = isInitialized();
    initialize();
    if (!m_filter->flush() && initialized) refreshData();
}

void WorkPage::refreshData()
{
    m_lazyArea->reset(LazyScrollArea::ScrollPosition::Preserve);
}

QSet<qint64> WorkPage::filteredWorkIds()
{
    if (!m_appliedSearch) return {};
    if (!m_filteredWorkIds) m_filteredWorkIds = m_repository.matchingWorkIds(*m_appliedSearch);
    return *m_filteredWorkIds;
}

QList<WorkSummary> WorkPage::filteredWorks() const
{
    if (!m_appliedSearch) return {};
    WorkSearch search = *m_appliedSearch;
    search.limit = 0;
    search.offset = 0;
    return m_repository.search(search);
}

void WorkPage::focusSearch()
{
    initialize();
    m_filter->focusNotes();
}

QWidget *WorkPage::captureContent()
{
    initialize();
    if (m_lazyArea == nullptr)
        return nullptr;
    return m_lazyArea->widget();
}

void WorkPage::refreshReferences()
{
    refreshFromWorkspace(false, true);
}

void WorkPage::refreshTags()
{
    refreshFromWorkspace(true, false);
}

void WorkPage::refreshFromWorkspace(bool tagsChanged, bool referencesChanged)
{
    initialize();
    if (m_filter->parentWidget() == this)
        m_filter->refreshOptions(tagsChanged, referencesChanged);
    refreshData();
}

void WorkPage::loadCardPage(quint64 request, int pageIndex, int pageSize)
{
    struct PageResult
    {
        QList<WorkSummary> works;
        WorkSearch search;
        std::optional<int> total;
        QString error;
    };

    const auto filters = pageIndex == 0 ? std::optional{m_filter->searchRequest()} : std::nullopt;
    const WorkSearch appliedSearch = m_appliedSearch.value_or(WorkSearch{});
    const QString databasePath = m_database.databaseName();
    if (m_pageQuery) m_pageQuery->cancel();

    auto *watcher = new QFutureWatcher<PageResult>(this);
    m_pageQuery = watcher;
    connect(watcher, &QFutureWatcher<PageResult>::finished, this,
            [this, watcher, request, pageSize] {
        if (m_pageQuery == watcher) m_pageQuery = nullptr;
        watcher->deleteLater();
        if (!m_lazyArea->isCurrentRequest(request)) return;

        PageResult result = watcher->result();
        if (!result.error.isEmpty()) {
            m_lazyArea->failPage(request);
            ToastNotification::showMessage(
                window(), QStringLiteral("加载作品失败：%1").arg(result.error),
                ToastNotification::Level::Error, 4000, &m_themeService);
            return;
        }

        if (result.total) {
            m_filter->updateCount(*result.total);
            m_appliedSearch = std::move(result.search);
            m_filteredWorkIds.reset();
        }
        const int count = static_cast<int>(result.works.size());
        m_lazyArea->completePage(request, count, count == pageSize,
                                 [this, works = std::move(result.works)](int index) {
            auto *card = new WorkCard(works.at(index), m_coverDirectory,
                                      /*largeCoverView=*/true, nullptr, /*deferCoverLoad=*/true);
            connect(card, &WorkCard::activated, this, &WorkPage::showWorkDetails);
            connect(card, &WorkCard::editRequested, this, &WorkPage::loadWork);
            return card;
        });
        if (result.total) emit filteredWorksChanged();
    });

    QPromise<PageResult> promise;
    promise.start();
    watcher->setFuture(promise.future());
    QThreadPool::globalInstance()->start(
        [promise = std::move(promise), databasePath, filters, appliedSearch, pageIndex, pageSize]() mutable {
        if (promise.isCanceled()) return;

        PageResult result;
        SqliteConnection connection;
        if (connection.open(databasePath, true, &result.error)) {
            const WorkRepository repository(connection.database());
            result.search = filters ? filters->resolve(&result.error) : appliedSearch;
            if (promise.isCanceled()) return;
            if (filters && result.error.isEmpty()) {
                result.total = repository.count(result.search, &result.error);
                if (!result.total && result.error.isEmpty())
                    result.error = QStringLiteral("无法统计作品总数");
            }
            if (promise.isCanceled()) return;

            if (result.error.isEmpty()) {
                WorkSearch page = result.search;
                page.limit = pageSize;
                page.offset = pageIndex * pageSize;
                result.works = repository.search(page, &result.error);
            }
        } else if (result.error.isEmpty()) {
            result.error = QStringLiteral("无法打开只读数据库");
        }
        promise.addResult(std::move(result));
        promise.finish();
    });
}

void WorkPage::showWorkDetails(qint64 workId)
{
    emit detailRequested(workId);
}

void WorkPage::loadWork(qint64 workId)
{
    emit editRequested(workId);
}

void WorkPage::filterByActor(qint64 actorId)
{
    if (actorId <= 0) return;
    initialize();
    QString error;
    SqliteConnection connection;
    if (!connection.open(m_database.databaseName(), true, &error)) return;
    const auto details =
        PersonRepository(connection.database()).findDetails(PersonKind::Actor, actorId, &error);
    if (!details.has_value()) return;
    for (const PersonName &name : details->names)
    {
        const QString displayName = !name.chinese.trimmed().isEmpty() ? name.chinese
                                    : !name.japanese.trimmed().isEmpty() ? name.japanese
                                                                         : name.english;
        if (!displayName.trimmed().isEmpty())
        {
            m_filter->filterByActor(actorId, displayName);
            return;
        }
    }
}

void WorkPage::filterByTag(qint64 tagId)
{
    if (tagId <= 0) return;
    initialize();
    m_filter->filterByTag(tagId);
}

} // namespace darkeye
