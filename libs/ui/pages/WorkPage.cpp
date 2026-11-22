#include "ui/pages/WorkPage.h"

#include "darkeye_ui/components/IconButton.h"
#include "darkeye_ui/components/LazyScrollArea.h"
#include "darkeye_ui/components/ToastNotification.h"
#include "database/SqliteConnection.h"
#include "database/repositories/PersonRepository.h"
#include "ui/components/WorkCard.h"
#include "ui/components/WorkFilter.h"

#include <QGridLayout>
#include <QHBoxLayout>
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

void WorkPage::lazyLoad()
{
    m_filter->initialize();
    m_largeCoverView = settings::app().workLargeCoverView;
    buildUi();
    connect(m_filter, &WorkFilter::changed, this, &WorkPage::refreshData);
    refreshData();
    m_lazyArea->setViewportItemHandler([](QWidget *widget, bool active) {
        static_cast<WorkCard *>(widget)->setCoverActive(active);
    });
    m_lazyArea->setLoader([this](int pageIndex, int pageSize)
                          { return loadCardPage(pageIndex, pageSize); });
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
    initialize();
    if (!m_filter->flush()) refreshData();
}

void WorkPage::refreshData()
{
    QString errorMessage;
    SqliteConnection connection;
    if (!connection.open(m_database.databaseName(), true, &errorMessage))
    {
        const QString message = errorMessage.isEmpty() ? QStringLiteral("无法打开只读数据库")
                                                       : errorMessage;
        if (message != m_lastCountError)
        {
            ToastNotification::showMessage(
                window(), QStringLiteral("统计作品失败：%1").arg(message),
                ToastNotification::Level::Error, 4000, &m_themeService);
            m_lastCountError = message;
        }
        return;
    }

    WorkRepository repository(connection.database());
    const WorkSearch search = m_filter->search();
    const std::optional<int> total = repository.count(search, &errorMessage);
    if (!total.has_value())
    {
        const QString message = errorMessage.isEmpty() ? QStringLiteral("未知数据库错误")
                                                       : errorMessage;
        if (message != m_lastCountError)
        {
            ToastNotification::showMessage(
                window(), QStringLiteral("统计作品失败：%1").arg(message),
                ToastNotification::Level::Error, 4000, &m_themeService);
            m_lastCountError = message;
        }
        return;
    }
    m_lastCountError.clear();
    m_filter->updateCount(*total);
    qInfo() << "WorkPage query returned" << *total << "rows";
    m_appliedSearch = search;
    m_filteredWorkIds.reset();
    m_lazyArea->reset();
    emit filteredWorksChanged();
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

QList<QWidget *> WorkPage::loadCardPage(int pageIndex, int pageSize)
{
    QString errorMessage;
    SqliteConnection connection;
    if (!connection.open(m_database.databaseName(), true, &errorMessage))
    {
        ToastNotification::showMessage(window(),
                                       QStringLiteral("加载作品失败：%1").arg(errorMessage),
                                       ToastNotification::Level::Error, 4000, &m_themeService);
        return {};
    }
    if (!m_appliedSearch) return {};
    WorkSearch search = *m_appliedSearch;
    search.limit = pageSize;
    search.offset = pageIndex * pageSize;
    WorkRepository repository(connection.database());
    const QList<WorkSummary> works = repository.search(search, &errorMessage);
    if (!errorMessage.isEmpty())
    {
        ToastNotification::showMessage(window(),
                                       QStringLiteral("加载作品失败：%1").arg(errorMessage),
                                       ToastNotification::Level::Error, 4000, &m_themeService);
        return {};
    }

    QList<QWidget *> cards;
    cards.reserve(works.size());
    for (const WorkSummary &work : works)
    {
        auto *card = new WorkCard(work, m_coverDirectory, m_largeCoverView, nullptr, true);
        connect(card, &WorkCard::activated, this, &WorkPage::showWorkDetails);
        connect(card, &WorkCard::editRequested, this, &WorkPage::loadWork);
        cards.append(card);
    }
    return cards;
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
