#include "ui/pages/ShelfPage.h"

#include "darkeye_ui/components/CompleterLineEdit.h"
#include "darkeye_ui/components/DesignComboBox.h"
#include "darkeye_ui/components/DesignInput.h"
#include "darkeye_ui/components/DesignLabel.h"
#include "darkeye_ui/components/IconButton.h"
#include "darkeye_ui/components/InteractionEffects.h"
#include "darkeye_ui/components/MakerSelector.h"
#include "darkeye_ui/components/ToastNotification.h"
#include "database/SqliteConnection.h"
#include "database/repositories/PrivateRepository.h"
#include "database/repositories/WorkRepository.h"
#include "settings/Settings.h"
#include "ui/components/DvdShelfView.h"
#include "ui/components/WorkTagSelector.h"

#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QRandomGenerator>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QTimer>
#include <QVBoxLayout>

namespace darkeye
{

ShelfPage::ShelfPage(QSqlDatabase database, QSqlDatabase privateDatabase,
                     ThemeService &themeService, graph::GraphManager *graphManager,
                     QString coverDirectory, QString fanartDirectory, QWidget *parent)
    : LazyWidget(parent), m_database(std::move(database)),
      m_privateDatabase(std::move(privateDatabase)), m_themeService(themeService),
      m_graphManager(graphManager), m_coverDirectory(std::move(coverDirectory)),
      m_fanartDirectory(std::move(fanartDirectory))
{
    setObjectName(QStringLiteral("ShelfPage"));
}

void ShelfPage::lazyLoad()
{
    m_randomSeed = QRandomGenerator::global()->bounded(1, 1'000'001);
    m_randomSeed2 = QRandomGenerator::global()->bounded(1, 1'000'001);
    m_tagPanelVisible = settings::app().shelfTagSelectorVisible;
    buildUi();
    refreshData();
}

void ShelfPage::buildUi()
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);
    auto *filterBar = new QWidget(this);
    filterBar->setFixedHeight(44);
    auto *bar = new QHBoxLayout(filterBar);
    bar->setContentsMargins(10, 0, 10, 0);
    bar->setSpacing(6);

    m_tagPanelButton = new IconButton(m_tagPanelVisible ? QStringLiteral("panel_left_close")
                                                         : QStringLiteral("panel_left_open"),
                                      &m_themeService, filterBar);
    m_tagPanelButton->setObjectName(QStringLiteral("ShelfTagPanelButton"));
    bar->addWidget(m_tagPanelButton);
    auto *filterScroll = new QScrollArea(filterBar);
    filterScroll->setWidgetResizable(true);
    filterScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    filterScroll->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    filterScroll->setFrameShape(QFrame::NoFrame);
    filterScroll->setFixedHeight(44);
    auto *filterContent = new QWidget(filterScroll);
    auto *filters = new QHBoxLayout(filterContent);
    filters->setContentsMargins(0, 0, 0, 0);
    filters->setSpacing(6);
    filters->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);

    QList<NamedIdOption> makers, labels, series;
    QList<TagOption> tags;
    QString error;
    SqliteConnection connection;
    if (connection.open(m_database.databaseName(), true, &error))
    {
        WorkRepository repository(connection.database());
        makers = repository.makerOptions();
        labels = repository.labelOptions();
        series = repository.seriesOptions();
        tags = repository.tagOptions();
    }
    const QString databasePath = m_database.databaseName();
    m_serialFilter = new CompleterLineEdit([databasePath] {
        QString loadError;
        SqliteConnection asyncConnection;
        if (!asyncConnection.open(databasePath, true, &loadError)) return QStringList{};
        return WorkRepository(asyncConnection.database()).serialSuggestions();
    }, filterContent);

    m_actressFilter = new CompleterLineEdit([databasePath] {
        QString loadError;
        SqliteConnection asyncConnection;
        if (!asyncConnection.open(databasePath, true, &loadError)) return QStringList{};
        return WorkRepository(asyncConnection.database()).actressSuggestions();
    }, filterContent);
    m_titleFilter = new DesignLineEdit(filterContent);
    m_notesFilter = new DesignLineEdit(filterContent);
    m_directorFilter = new CompleterLineEdit([databasePath] {
        QString loadError;
        SqliteConnection asyncConnection;
        if (!asyncConnection.open(databasePath, true, &loadError)) return QStringList{};
        return WorkRepository(asyncConnection.database()).directorSuggestions();
    }, filterContent);
    m_actorFilter = new CompleterLineEdit([databasePath] {
        QString loadError;
        SqliteConnection asyncConnection;
        if (!asyncConnection.open(databasePath, true, &loadError)) return QStringList{};
        return WorkRepository(asyncConnection.database()).actorSuggestions();
    }, filterContent);
    const auto addText = [filters, filterContent](const QString &label, QLineEdit *input, int width)
    {
        filters->addWidget(new DesignLabel(label, filterContent));
        input->setFixedWidth(width);
        filters->addWidget(input);
    };
    addText(QStringLiteral("番号："), m_serialFilter, 150);
    addText(QStringLiteral("女优"), m_actressFilter, 120);
    addText(QStringLiteral("标题包含："), m_titleFilter, 100);
    addText(QStringLiteral("简单笔记包含："), m_notesFilter, 100);
    addText(QStringLiteral("导演"), m_directorFilter, 150);
    addText(QStringLiteral("男优"), m_actorFilter, 120);
    const auto selectorOptions = [](const QList<NamedIdOption> &options) {
        QList<MakerOption> result;
        result.reserve(options.size());
        for (const NamedIdOption &option : options)
            result.append({option.id, option.chineseName, option.japaneseName, option.aliases});
        return result;
    };
    const auto addCombo = [filters, filterContent](const QString &label, MakerSelector *combo)
    {
        filters->addWidget(new DesignLabel(label, filterContent));
        combo->setFixedWidth(160);
        filters->addWidget(combo);
    };
    m_makerFilter = new MakerSelector(selectorOptions(makers), filterContent);
    m_labelFilter = new MakerSelector(selectorOptions(labels), filterContent);
    m_seriesFilter = new MakerSelector(selectorOptions(series), filterContent);
    addCombo(QStringLiteral("片商"), m_makerFilter);
    addCombo(QStringLiteral("厂牌"), m_labelFilter);
    addCombo(QStringLiteral("系列"), m_seriesFilter);
    filterScroll->setWidget(filterContent);
    bar->addWidget(filterScroll, 1);

    auto *reload = new RotateButton(QStringLiteral("refresh_cw"), &m_themeService, filterBar);
    auto *clear = new ShakeButton(QStringLiteral("eraser"), &m_themeService, filterBar);
    m_countLabel = new DesignLabel({}, filterBar);
    m_countLabel->setObjectName(QStringLiteral("ShelfCountLabel"));
    m_countLabel->setFixedWidth(100);
    m_scopeSelector = new DesignComboBox(filterBar);
    m_scopeSelector->setObjectName(QStringLiteral("ShelfScopeSelector"));
    m_scopeSelector->addItems({QStringLiteral("公共库范围"), QStringLiteral("收藏库范围"),
                               QStringLiteral("收藏未观看"), QStringLiteral("已撸过"),
                               QStringLiteral("本地有视频")});
    m_scopeSelector->setToolTip(QStringLiteral("选择公共库、收藏、观看记录或本地视频范围"));
    m_sortSelector = new DesignComboBox(filterBar);
    m_sortSelector->setObjectName(QStringLiteral("ShelfSortSelector"));
    const QList<QPair<QString, WorkSortOrder>> sorts{
        {QStringLiteral("随机顺序"), WorkSortOrder::Random},
        {QStringLiteral("添加逆序"), WorkSortOrder::CreatedDescending},
        {QStringLiteral("添加顺序"), WorkSortOrder::CreatedAscending},
        {QStringLiteral("番号顺序"), WorkSortOrder::SerialAscending},
        {QStringLiteral("番号逆序"), WorkSortOrder::SerialDescending},
        {QStringLiteral("制作商顺序"), WorkSortOrder::MakerAscending},
        {QStringLiteral("制作商逆序"), WorkSortOrder::MakerDescending},
        {QStringLiteral("更新时间顺序"), WorkSortOrder::UpdatedAscending},
        {QStringLiteral("更新时间逆序"), WorkSortOrder::UpdatedDescending},
        {QStringLiteral("发布时间逆序"), WorkSortOrder::ReleaseDateDescending},
        {QStringLiteral("发布时间顺序"), WorkSortOrder::ReleaseDateAscending},
        {QStringLiteral("拍摄年龄顺序"), WorkSortOrder::ActressAgeAscending},
        {QStringLiteral("拍摄年龄逆序"), WorkSortOrder::ActressAgeDescending}};
    for (const auto &[title, order] : sorts) m_sortSelector->addItem(title, static_cast<int>(order));
    m_sortSelector->setCurrentIndex(1);
    bar->addWidget(reload);
    bar->addWidget(clear);
    bar->addWidget(m_countLabel);
    bar->addWidget(m_scopeSelector);
    bar->addWidget(m_sortSelector);
    root->addWidget(filterBar);

    auto *body = new QHBoxLayout;
    body->setContentsMargins(0, 0, 0, 0);
    body->setSpacing(0);
    m_tagPanel = new QWidget(this);
    m_tagPanel->setObjectName(QStringLiteral("ShelfTagPanel"));
    auto *tagLayout = new QHBoxLayout(m_tagPanel);
    tagLayout->setContentsMargins(0, 0, 0, 0);
    m_tagSelector = new WorkTagSelector(tags, &m_themeService, m_tagPanel);
    m_tagSelector->setSelectedColumnWidth(84);
    for (const TagOption &tag : tags)
    {
        if (tag.name == QStringLiteral("绿色封面"))
        {
            m_tagSelector->setSelectedIds({tag.id});
            break;
        }
    }
    m_tagSelector->setLoader([this] {
        QString loadError;
        SqliteConnection tagConnection;
        if (!tagConnection.open(m_database.databaseName(), true, &loadError)) return QList<TagOption>{};
        return WorkRepository(tagConnection.database()).tagOptions();
    });
    tagLayout->addWidget(m_tagSelector, 0, Qt::AlignLeft);
    m_tagPanel->setVisible(m_tagPanelVisible);
    body->addWidget(m_tagPanel);
    m_shelfView = new DvdShelfView(m_database, m_privateDatabase, m_graphManager,
                                   m_coverDirectory, m_fanartDirectory, this);
    connect(m_shelfView, &DvdShelfView::workSelected, this, &ShelfPage::detailRequested);
    connect(m_shelfView, &DvdShelfView::editRequested, this, &ShelfPage::editRequested);
    connect(m_shelfView, &DvdShelfView::favoriteChanged, this, &ShelfPage::favoriteChanged);
    connect(m_shelfView, &DvdShelfView::workDeleted, this, [this](qint64 workId) {
        refreshData();
        emit workDeleted(workId);
    });
    connect(m_shelfView, &DvdShelfView::actressRequested, this, &ShelfPage::actressRequested);
    connect(m_shelfView, &DvdShelfView::actorRequested, this, &ShelfPage::actorRequested);
    connect(m_shelfView, &DvdShelfView::tagRequested, this, &ShelfPage::tagRequested);
    connect(m_shelfView, &DvdShelfView::directorRequested, this, &ShelfPage::filterByDirector);
    connect(m_shelfView, &DvdShelfView::makerRequested, this, &ShelfPage::filterByMaker);
    connect(m_shelfView, &DvdShelfView::labelRequested, this, &ShelfPage::filterByLabel);
    connect(m_shelfView, &DvdShelfView::seriesRequested, this, &ShelfPage::filterBySeries);
    body->addWidget(m_shelfView, 1);
    root->addLayout(body, 1);

    connect(reload, &QPushButton::clicked, this, [this] {
        m_randomSeed = QRandomGenerator::global()->generate();
        m_randomSeed2 = QRandomGenerator::global()->generate();
        applyFilters();
    });
    connect(clear, &QPushButton::clicked, this, &ShelfPage::clearFilters);
    connect(m_tagPanelButton, &QPushButton::clicked, this, &ShelfPage::toggleTagPanel);
    m_filterTimer = new QTimer(this);
    m_filterTimer->setSingleShot(true);
    m_filterTimer->setInterval(50);
    connect(m_filterTimer, &QTimer::timeout, this, &ShelfPage::applyFilters);
    const QList<QLineEdit *> inputs{m_serialFilter, m_actressFilter, m_titleFilter, m_notesFilter,
                                     m_directorFilter, m_actorFilter, m_makerFilter->lineEdit(),
                                     m_labelFilter->lineEdit(), m_seriesFilter->lineEdit()};
    for (QLineEdit *input : inputs)
        connect(input, &QLineEdit::textChanged, m_filterTimer, qOverload<>(&QTimer::start));
    connect(m_tagSelector, &WorkTagSelector::selectionChanged, m_filterTimer,
            qOverload<>(&QTimer::start));
    connect(m_scopeSelector, &QComboBox::currentIndexChanged, this, [this](int) { applyFilters(); });
    connect(m_sortSelector, &QComboBox::currentIndexChanged, this, [this](int) { applyFilters(); });
}

void ShelfPage::refresh()
{
    initialize();
    refreshData();
}

void ShelfPage::refreshAfterCapture()
{
    initialize();
    const qint64 workId = m_shelfView->currentWorkId();
    refreshData();
    if (workId > 0)
        m_shelfView->openWork(workId);
}

void ShelfPage::refreshReferences()
{
    initialize();
    QString error;
    SqliteConnection connection;
    if (!connection.open(m_database.databaseName(), true, &error)) return;
    const auto resetOptions = [](MakerSelector *combo, const QList<NamedIdOption> &options) {
        const auto selected = combo->maker();
        QList<MakerOption> replacement;
        replacement.reserve(options.size());
        for (const NamedIdOption &option : options)
            replacement.append({option.id, option.chineseName, option.japaneseName, option.aliases});
        const QSignalBlocker blocker(combo);
        combo->setMakers(replacement);
        combo->setMaker(selected);
    };
    WorkRepository repository(connection.database());
    resetOptions(m_makerFilter, repository.makerOptions());
    resetOptions(m_labelFilter, repository.labelOptions());
    resetOptions(m_seriesFilter, repository.seriesOptions());
    m_serialFilter->reloadItems();
    m_directorFilter->reloadItems();
    refreshData();
}

void ShelfPage::refreshTags()
{
    initialize();
    m_tagSelector->reloadTags();
    refreshData();
}

void ShelfPage::resetForRoute()
{
    // Route handlers reload once, after all destination filters are in place.
    const QSignalBlocker scopeBlocker(m_scopeSelector);
    const QSignalBlocker sortBlocker(m_sortSelector);
    clearFilterInputs();
    m_scopeSelector->setCurrentIndex(0);
    m_sortSelector->setCurrentIndex(0);
}

void ShelfPage::filterByDirector(const QString &director)
{
    initialize();
    resetForRoute();
    m_directorFilter->setText(director);
    refreshData();
}

void ShelfPage::filterByMaker(qint64 makerId)
{
    initialize();
    resetForRoute();
    m_makerFilter->setMaker(makerId);
    refreshData();
}

void ShelfPage::filterByLabel(qint64 labelId)
{
    initialize();
    resetForRoute();
    m_labelFilter->setMaker(labelId);
    refreshData();
}

void ShelfPage::filterBySeries(qint64 seriesId)
{
    initialize();
    resetForRoute();
    m_seriesFilter->setMaker(seriesId);
    refreshData();
}

void ShelfPage::filterByTag(qint64 tagId)
{
    initialize();
    resetForRoute();
    m_tagSelector->setSelectedIds({tagId});
    refreshData();
}

bool ShelfPage::showWork(qint64 workId)
{
    if (workId <= 0) return false;
    initialize();
    resetForRoute();
    refreshData();
    return m_shelfView != nullptr && m_shelfView->openWork(workId);
}

void ShelfPage::applyFilters() { refreshData(); }

void ShelfPage::clearFilters()
{
    clearFilterInputs();
    applyFilters();
}

void ShelfPage::clearFilterInputs()
{
    m_filterTimer->stop();
    const QList<QLineEdit *> inputs{m_serialFilter, m_actressFilter, m_titleFilter, m_notesFilter,
                                     m_directorFilter, m_actorFilter, m_makerFilter->lineEdit(),
                                     m_labelFilter->lineEdit(), m_seriesFilter->lineEdit()};
    for (QLineEdit *input : inputs) input->clear();
    m_makerFilter->setMaker(std::nullopt);
    m_labelFilter->setMaker(std::nullopt);
    m_seriesFilter->setMaker(std::nullopt);
    m_tagSelector->clearSelection();
}

void ShelfPage::toggleTagPanel()
{
    m_tagPanelVisible = !m_tagPanelVisible;
    m_tagPanel->setVisible(m_tagPanelVisible);
    m_tagPanelButton->setIconName(m_tagPanelVisible ? QStringLiteral("panel_left_close")
                                                     : QStringLiteral("panel_left_open"));
    AppSettings appSettings = settings::app();
    appSettings.shelfTagSelectorVisible = m_tagPanelVisible;
    settings::saveApp(appSettings);
}

void ShelfPage::refreshData()
{
    // Synchronous refreshes supersede any text/tag debounce, including signals
    // emitted while resetting route filters. A late reload would close the
    // newly opened DVD and invalidate its delayed expansion callback.
    m_filterTimer->stop();
    const int total = reloadDvdScene();
    if (total < 0) return;
    m_countLabel->setText(total == 0 ? QStringLiteral("没有查询到数据")
                                     : QStringLiteral("过滤总数:%1").arg(total));
}

int ShelfPage::reloadDvdScene()
{
    QString error;
    SqliteConnection connection;
    if (!connection.open(m_database.databaseName(), true, &error)) return -1;
    WorkSearch search = currentSearch();
    // DvdShelfView receives the complete result set but submits only a moving
    // 60-item window to the native QRhi DVD scene.
    search.limit = 0;
    search.offset = 0;
    const QList<WorkSummary> works = WorkRepository(connection.database()).search(search, &error);
    if (!error.isEmpty()) return -1;
    m_shelfView->setWorks(works);
    return works.size();
}

WorkSortOrder ShelfPage::selectedSortOrder() const
{
    return static_cast<WorkSortOrder>(m_sortSelector->currentData().toInt());
}

WorkSearch ShelfPage::currentSearch() const
{
    WorkSearch search;
    search.serialNumber = m_serialFilter->text();
    search.title = m_titleFilter->text();
    search.notes = m_notesFilter->text();
    search.director = m_directorFilter->text();
    search.actressName = m_actressFilter->text();
    search.actorName = m_actorFilter->text();
    search.makerId = m_makerFilter->maker();
    search.labelId = m_labelFilter->maker();
    search.seriesId = m_seriesFilter->maker();
    search.tagIds = m_tagSelector->selectedIds();
    search.randomSeed = m_randomSeed;
    search.randomSeed2 = m_randomSeed2;
    search.order = selectedSortOrder();
    if (m_scopeSelector->currentIndex() == 4)
    {
        search.requireLocalVideo = true;
        return search;
    }
    if (!m_privateDatabase.isValid() || m_scopeSelector->currentIndex() == 0) return search;
    QString privateError;
    SqliteConnection connection;
    if (!connection.open(m_privateDatabase.databaseName(), true, &privateError)) return search;
    PrivateRepository repository(connection.database());
    search.restrictToIncludedWorkIds = true;
    switch (m_scopeSelector->currentIndex())
    {
    case 1: search.includedWorkIds = repository.favoriteWorkIds(&privateError); break;
    case 2: search.includedWorkIds = repository.favoriteUnwatchedWorkIds(&privateError); break;
    case 3: search.includedWorkIds = repository.masturbationWorkIds(&privateError); break;
    default: break;
    }
    return search;
}

} // namespace darkeye
