#include "ui/pages/WorkPage.h"

#include "darkeye_ui/components/IconButton.h"
#include "darkeye_ui/components/LazyScrollArea.h"
#include "darkeye_ui/components/ToastNotification.h"
#include "database/SqliteConnection.h"
#include "ui/components/WorkCard.h"
#include "ui/components/WorkFilter.h"

#include <QFutureWatcher>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QPromise>
#include <QThreadPool>
#include <QVBoxLayout>

namespace darkeye
{
WorkPage::WorkPage(QSqlDatabase database, ThemeService &themeService,
                   QString coverDirectory, QSqlDatabase privateDatabase,
                   QWidget *parent, WorkFilter *filter)
    : LazyWidget(parent), m_themeService(themeService), m_database(database),
      m_repository(std::move(database)),
      m_coverDirectory(std::move(coverDirectory)),
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
    m_largeCoverView = settings::app().workLargeCoverView;
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

    auto *body = new QWidget(this);
    auto *overlay = new QGridLayout(body);
    overlay->setContentsMargins(0, 0, 0, 0);
    m_lazyArea = new LazyScrollArea(m_largeCoverView ? 250 : 220, body);
    m_lazyArea->setPageSize(70);
    m_lazyArea->setUniformRows(m_largeCoverView);
    overlay->addWidget(m_lazyArea, 0, 0);

    m_viewButton = new IconButton(m_largeCoverView ? QStringLiteral("layout_waterfall")
                                                  : QStringLiteral("layout_grid"),
                                  &m_themeService, body);
    m_viewButton->setProperty("workControlId", QStringLiteral("WorkCoverViewButton"));
    m_viewButton->setObjectName(QStringLiteral("WorkCoverViewToggle"));
    m_viewButton->setIconPixelSize(22);
    m_viewButton->setButtonPixelSize(34);
    m_viewButton->setToolTip(m_largeCoverView ? QStringLiteral("切换为瀑布流")
                                              : QStringLiteral("切换为普通布局"));
    m_viewButton->setAccessibleName(m_viewButton->toolTip());
    auto *buttonLayout = new QHBoxLayout;
    buttonLayout->setContentsMargins(0, 8, 20, 0);
    buttonLayout->addStretch();
    buttonLayout->addWidget(m_viewButton, 0, Qt::AlignTop);
    overlay->addLayout(buttonLayout, 0, 0, Qt::AlignTop);
    root->addWidget(body, 1);
    connect(m_viewButton, &QPushButton::clicked, this, &WorkPage::toggleCoverSize);
}

void WorkPage::toggleCoverSize()
{
    m_largeCoverView = !m_largeCoverView;
    m_lazyArea->setColumnWidth(m_largeCoverView ? 250 : 220);
    m_lazyArea->setUniformRows(m_largeCoverView);
    m_viewButton->setIconName(m_largeCoverView ? QStringLiteral("layout_waterfall")
                                               : QStringLiteral("layout_grid"));
    m_viewButton->setToolTip(m_largeCoverView ? QStringLiteral("切换为瀑布流")
                                              : QStringLiteral("切换为普通布局"));
    m_viewButton->setAccessibleName(m_viewButton->toolTip());
    AppSettings appSettings = settings::app();
    appSettings.workLargeCoverView = m_largeCoverView;
    settings::saveApp(appSettings);
    refresh();
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
            auto *card = new WorkCard(works.at(index), m_coverDirectory, m_largeCoverView,
                                      nullptr, true);
            connect(card, &WorkCard::activated, this, &WorkPage::detailRequested);
            connect(card, &WorkCard::editRequested, this, &WorkPage::editRequested);
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

} // namespace darkeye
