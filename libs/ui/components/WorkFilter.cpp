#include "ui/components/WorkFilter.h"

#include "darkeye_ui/components/CompleterLineEdit.h"
#include "darkeye_ui/components/DesignComboBox.h"
#include "darkeye_ui/components/DesignInput.h"
#include "darkeye_ui/components/DesignLabel.h"
#include "darkeye_ui/components/InteractionEffects.h"
#include "darkeye_ui/components/MakerSelector.h"
#include "darkeye_ui/components/ToastNotification.h"
#include "database/SqliteConnection.h"
#include "database/repositories/ReferenceRepository.h"
#include "database/repositories/PrivateRepository.h"
#include "database/repositories/WorkRepository.h"
#include "ui/components/WorkTagSelector.h"

#include <QComboBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMetaObject>
#include <QPointer>
#include <QRandomGenerator>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QThreadPool>
#include <QTimer>
#include <QVBoxLayout>
#include <algorithm>
#include <functional>

namespace darkeye
{
namespace
{
const QString initialDefaultTagName = QStringLiteral("绿色封面");

QList<MakerOption> selectorOptions(const QList<NamedIdOption> &options)
{
    QList<MakerOption> result;
    result.reserve(options.size());
    for (const NamedIdOption &option : options)
        result.append({option.id, option.chineseName, option.japaneseName, option.aliases});
    return result;
}

void populateSelector(MakerSelector *selector, const QList<NamedIdOption> &options)
{
    const auto selected = selector->maker();
    const QString text = selector->currentText();
    const QSignalBlocker selectorBlocker(selector);
    const QSignalBlocker editBlocker(selector->lineEdit());
    selector->setMakers(selectorOptions(options));
    selector->setMaker(selected);
    if (!selected.has_value())
        selector->setEditText(text);
}

}

WorkFilter::WorkFilter(QSqlDatabase database, QSqlDatabase privateDatabase,
                       ThemeService &themeService, QWidget *parent)
    : LazyWidget(parent), m_themeService(themeService), m_database(std::move(database)),
      m_privateDatabase(std::move(privateDatabase))
{
}

void WorkFilter::lazyLoad()
{
    m_randomSeed = QRandomGenerator::global()->bounded(1, 1'000'001);
    m_randomSeed2 = QRandomGenerator::global()->bounded(1, 1'000'001);
    buildUi();
}

void WorkFilter::focusNotes()
{
    initialize();
    m_searchInput->setFocus(Qt::ShortcutFocusReason);
    m_searchInput->selectAll();
}

bool WorkFilter::flush()
{
    if (!m_filterTimer->isActive()) return false;
    applyFilters();
    return true;
}

void WorkFilter::resizeEvent(QResizeEvent *event)
{
    LazyWidget::resizeEvent(event);
    if (m_tagSelector)
        m_tagSelector->setAvailablePanelWidth(qBound(235, width() - 122, 700));
}

void WorkFilter::updateCount(int total)
{
    m_countLabel->setText(QStringLiteral("过滤总数:%1").arg(total));
}

void WorkFilter::applyFilters()
{
    m_filterTimer->stop();
    emit changed();
}

void WorkFilter::clearFilters()
{
    clearInputs();
    applyFilters();
}

void WorkFilter::refreshOptions(bool tagsChanged, bool referencesChanged)
{
    if (!isInitialized()) return;
    if (referencesChanged) reloadReferenceOptions();
    if (tagsChanged) {
        const QSignalBlocker blocker(m_tagSelector);
        m_tagSelector->reloadTags();
        if (m_routeTagId > 0)
            selectRouteTag();
    }
    m_filterTimer->stop();
}

void WorkFilter::filterByReference(Reference reference, qint64 id)
{
    initialize();
    clearInputs();
    MakerSelector *selector = m_makerFilter;
    switch (reference) {
    case Reference::Maker: break;
    case Reference::Label: selector = m_labelFilter; break;
    case Reference::Series: selector = m_seriesFilter; break;
    }
    selector->setMaker(id);
    if (!selector->maker()) {
        reloadReferenceOptions();
        selector->setMaker(id);
    }
    applyFilters();
}

void WorkFilter::filterByDirector(const QString &director)
{
    initialize();
    clearInputs();
    m_directorFilter->setText(director);
    applyFilters();
}

void WorkFilter::filterByActor(qint64 id, const QString &name)
{
    initialize();
    clearInputs();
    m_actorFilter->setText(name);
    m_actorId = id;
    applyFilters();
}

void WorkFilter::filterByTag(qint64 id)
{
    initialize();
    clearInputs();
    m_routeTagId = id;
    selectRouteTag();
    applyFilters();
}

void WorkFilter::selectRouteTag()
{
    const QSignalBlocker blocker(m_tagSelector);
    m_routeTagId = ReferenceRepository(m_database).resolveTagId(m_routeTagId)
                       .value_or(m_routeTagId);
    m_tagSelector->setSelectedIds({m_routeTagId});
}

void WorkFilter::buildUi()
{
    setObjectName(QStringLiteral("WorkFilter"));
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);
    auto *filterBar = new QWidget(this);
    filterBar->setObjectName(QStringLiteral("WorkFilterBar"));
    auto *filterBarLayout = new QVBoxLayout(filterBar);
    filterBarLayout->setContentsMargins(10, 0, 10, 0);
    filterBarLayout->setSpacing(6);

    auto *filterContent = new QWidget(filterBar);
    filterContent->setMaximumWidth(580);
    auto *filters = new QFormLayout(filterContent);
    filters->setContentsMargins(0, 0, 0, 0);
    filters->setSpacing(6);
    filters->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    const auto addFilter = [filters, filterContent](const QString &label, QWidget *input)
    {
        auto *caption = new DesignLabel(label, filterContent);
        caption->setBuddy(input);
        filters->addRow(caption, input);
    };
    const QString databasePath = m_database.databaseName();
    const auto loadSuggestions = [databasePath](auto query)
    {
        SqliteConnection connection;
        QString errorMessage;
        if (!connection.open(databasePath, true, &errorMessage))
        {
            qWarning() << "WorkFilter completion data load failed:" << errorMessage;
            return QStringList{};
        }
        WorkRepository repository(connection.database());
        return std::invoke(query, repository);
    };
    QList<TagOption> tagOptions;
    QString initialLoadError;
    {
        SqliteConnection connection;
        if (connection.open(m_database.databaseName(), true, &initialLoadError))
        {
            WorkRepository repository(connection.database());
            tagOptions = repository.tagOptions();
        }
    }
    if (!initialLoadError.isEmpty())
        qWarning() << "WorkFilter initial filter data load failed:" << initialLoadError;
    m_serialFilter = new CompleterLineEdit(
        [loadSuggestions]
        { return loadSuggestions(&WorkRepository::serialSuggestions); }, filterContent);
    m_serialFilter->setProperty("workControlId", QStringLiteral("WorkSerialFilter"));
    m_actressFilter = new CompleterLineEdit(
        [loadSuggestions]
        { return loadSuggestions(&WorkRepository::actressSuggestions); }, filterContent);
    m_actressFilter->setProperty("workControlId", QStringLiteral("WorkActressFilter"));
    m_titleFilter = new DesignLineEdit(filterContent);
    m_titleFilter->setProperty("workControlId", QStringLiteral("WorkTitleFilter"));
    m_searchInput = new DesignLineEdit(filterContent);
    m_searchInput->setProperty("workControlId", QStringLiteral("WorkNotesFilter"));
    m_storyFilter = new DesignLineEdit(filterContent);
    m_storyFilter->setProperty("workControlId", QStringLiteral("WorkStoryFilter"));
    m_directorFilter = new CompleterLineEdit(
        [loadSuggestions]
        { return loadSuggestions(&WorkRepository::directorSuggestions); }, filterContent);
    m_directorFilter->setProperty("workControlId", QStringLiteral("WorkDirectorFilter"));
    m_actorFilter = new CompleterLineEdit(
        [loadSuggestions]
        { return loadSuggestions(&WorkRepository::actorSuggestions); }, filterContent);
    m_actorFilter->setProperty("workControlId", QStringLiteral("WorkActorFilter"));
    addFilter(QStringLiteral("番号"), m_serialFilter);
    addFilter(QStringLiteral("女优"), m_actressFilter);
    addFilter(QStringLiteral("标题包含"), m_titleFilter);
    addFilter(QStringLiteral("故事包含"), m_storyFilter);
    addFilter(QStringLiteral("笔记包含"), m_searchInput);
    addFilter(QStringLiteral("导演"), m_directorFilter);
    addFilter(QStringLiteral("男优"), m_actorFilter);
    m_makerFilter = new MakerSelector({}, filterContent);
    m_makerFilter->setProperty("workControlId", QStringLiteral("WorkMakerFilter"));
    m_labelFilter = new MakerSelector({}, filterContent);
    m_labelFilter->setProperty("workControlId", QStringLiteral("WorkLabelFilter"));
    m_seriesFilter = new MakerSelector({}, filterContent);
    m_seriesFilter->setProperty("workControlId", QStringLiteral("WorkSeriesFilter"));
    addFilter(QStringLiteral("片商"), m_makerFilter);
    addFilter(QStringLiteral("厂牌"), m_labelFilter);
    addFilter(QStringLiteral("系列"), m_seriesFilter);

    auto *refreshButton = new RotateButton(QStringLiteral("refresh_cw"), &m_themeService,
                                           filterBar);
    refreshButton->setProperty("workControlId", QStringLiteral("WorkRefreshButton"));
    refreshButton->setIconPixelSize(24);
    refreshButton->setButtonPixelSize(24);
    refreshButton->setToolTip(QStringLiteral("刷新作品"));

    auto *clearButton = new ShakeButton(QStringLiteral("eraser"), &m_themeService, filterBar);
    clearButton->setProperty("workControlId", QStringLiteral("WorkClearButton"));
    clearButton->setIconPixelSize(24);
    clearButton->setButtonPixelSize(24);
    clearButton->setToolTip(QStringLiteral("清空筛选"));

    m_countLabel = new DesignLabel({}, filterBar);
    m_countLabel->setProperty("workControlId", QStringLiteral("WorkCountLabel"));
    m_countLabel->setFixedWidth(100);

    m_sortSelector = new DesignComboBox(filterBar);
    m_sortSelector->setProperty("compactFilter", true);
    m_sortSelector->setProperty("workControlId", QStringLiteral("WorkSortSelector"));
    m_sortSelector->addItem(QStringLiteral("随机顺序"), static_cast<int>(WorkSortOrder::Random));
    m_sortSelector->addItem(QStringLiteral("添加逆序"),
                            static_cast<int>(WorkSortOrder::CreatedDescending));
    m_sortSelector->addItem(QStringLiteral("添加顺序"),
                            static_cast<int>(WorkSortOrder::CreatedAscending));
    m_sortSelector->addItem(QStringLiteral("番号顺序"),
                            static_cast<int>(WorkSortOrder::SerialAscending));
    m_sortSelector->addItem(QStringLiteral("番号逆序"),
                            static_cast<int>(WorkSortOrder::SerialDescending));
    m_sortSelector->addItem(QStringLiteral("制作商顺序"),
                            static_cast<int>(WorkSortOrder::MakerAscending));
    m_sortSelector->addItem(QStringLiteral("制作商逆序"),
                            static_cast<int>(WorkSortOrder::MakerDescending));
    m_sortSelector->addItem(QStringLiteral("更新时间顺序"),
                            static_cast<int>(WorkSortOrder::UpdatedAscending));
    m_sortSelector->addItem(QStringLiteral("更新时间逆序"),
                            static_cast<int>(WorkSortOrder::UpdatedDescending));
    m_sortSelector->addItem(QStringLiteral("发布时间逆序"),
                            static_cast<int>(WorkSortOrder::ReleaseDateDescending));
    m_sortSelector->addItem(QStringLiteral("发布时间顺序"),
                            static_cast<int>(WorkSortOrder::ReleaseDateAscending));
    m_sortSelector->addItem(QStringLiteral("拍摄年龄顺序"),
                            static_cast<int>(WorkSortOrder::ActressAgeAscending));
    m_sortSelector->addItem(QStringLiteral("拍摄年龄逆序"),
                            static_cast<int>(WorkSortOrder::ActressAgeDescending));
    m_sortSelector->setCurrentIndex(1);

    m_scopeSelector = new DesignComboBox(filterBar);
    m_scopeSelector->setProperty("compactFilter", true);
    m_scopeSelector->setProperty("workControlId", QStringLiteral("WorkScopeSelector"));
    m_scopeSelector->addItems({QStringLiteral("公共库范围"), QStringLiteral("收藏库范围"),
                               QStringLiteral("收藏未观看"), QStringLiteral("已撸过"),
                               QStringLiteral("本地有视频")});
    m_scopeSelector->setCurrentIndex(0);
    m_scopeSelector->setToolTip(QStringLiteral("选择公共库、收藏、观看记录或本地视频范围"));
    addFilter(QStringLiteral("范围"), m_scopeSelector);
    addFilter(QStringLiteral("排序"), m_sortSelector);
    auto *toolbar = new QHBoxLayout;
    toolbar->addWidget(refreshButton);
    toolbar->addWidget(clearButton);
    toolbar->addWidget(m_countLabel);
    toolbar->addStretch();
    filterBarLayout->addLayout(toolbar);
    auto *filterScroll = new QScrollArea(this);
    filterScroll->setObjectName(QStringLiteral("WorkFilterScrollArea"));
    filterScroll->setWidgetResizable(true);
    filterScroll->setFrameShape(QFrame::NoFrame);
    filterScroll->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    filterScroll->setWidget(filterContent);
    filterScroll->setMinimumHeight(100);
    filterScroll->setMaximumHeight(filterContent->sizeHint().height() + 8);
    filterBarLayout->addWidget(filterScroll, 1);
    root->addWidget(filterBar);

    auto *tagPanel = new QWidget(this);
    tagPanel->setObjectName(QStringLiteral("WorkTagPanel"));
    auto *tagLayout = new QHBoxLayout(tagPanel);
    tagLayout->setContentsMargins(0, 0, 0, 0);
    auto *tagScroll = new QScrollArea(tagPanel);
    tagScroll->setWidgetResizable(true);
    tagScroll->setFrameShape(QFrame::NoFrame);
    tagScroll->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_tagSelector = new WorkTagSelector(tagOptions, &m_themeService);
    tagScroll->setWidget(m_tagSelector);
    m_tagSelector->setSelectedColumnWidth(84);
    m_tagSelector->setAvailablePanelWidth(qBound(235, width() - 122, 700));
    m_tagSelector->setAvailablePanelExpanded(true);
    applyInitialDefaultTagFilter(tagOptions);
    m_tagSelector->setLoader(
        [this]
        {
            SqliteConnection connection;
            QString errorMessage;
            if (!connection.open(m_database.databaseName(), true, &errorMessage))
            {
                qWarning() << "WorkFilter tag load failed:" << errorMessage;
                return QList<TagOption>{};
            }
            WorkRepository repository(connection.database());
            return repository.tagOptions();
        });
    tagLayout->addWidget(tagScroll);
    tagPanel->setMinimumHeight(140);
    root->addWidget(tagPanel, 1);

    connect(refreshButton, &QPushButton::clicked, this,
            [this]
            {
                // Python only re-seeds when the active order is random.  A
                // normal refresh must keep the current deterministic order.
                if (selectedSortOrder() == WorkSortOrder::Random)
                {
                    m_randomSeed = QRandomGenerator::global()->bounded(1, 1'000'001);
                    m_randomSeed2 = QRandomGenerator::global()->bounded(1, 1'000'001);
                }
                applyFilters();
            });
    connect(clearButton, &QPushButton::clicked, this, &WorkFilter::clearFilters);

    m_filterTimer = new QTimer(this);
    m_filterTimer->setSingleShot(true);
    m_filterTimer->setInterval(50);
    connect(m_filterTimer, &QTimer::timeout, this, &WorkFilter::applyFilters);
    const QList<QLineEdit *> filterInputs{m_searchInput,
                                          m_serialFilter,
                                          m_titleFilter,
                                          m_storyFilter,
                                          m_directorFilter,
                                          m_actressFilter,
                                          m_actorFilter,
                                          m_makerFilter->lineEdit(),
                                          m_labelFilter->lineEdit(),
                                          m_seriesFilter->lineEdit()};
    for (QLineEdit *input : filterInputs)
    {
        connect(input, &QLineEdit::textChanged, m_filterTimer, qOverload<>(&QTimer::start));
    }
    connect(m_tagSelector, &WorkTagSelector::selectionChanged, this, [this] {
        m_routeTagId = 0;
        m_filterTimer->start();
    });
    connect(m_actorFilter, &QLineEdit::textChanged, this, [this] { m_actorId.reset(); });
    connect(m_sortSelector, &QComboBox::currentIndexChanged, m_filterTimer,
            qOverload<>(&QTimer::start));
    connect(m_scopeSelector, &QComboBox::currentIndexChanged, m_filterTimer,
            qOverload<>(&QTimer::start));
    loadReferenceOptionsAsync();
}

void WorkFilter::applyInitialDefaultTagFilter(const QList<TagOption> &tags)
{
    const auto tag = std::find_if(tags.cbegin(), tags.cend(), [](const TagOption &option)
    {
        return option.name == initialDefaultTagName;
    });
    if (tag != tags.cend())
        m_tagSelector->setSelectedIds({tag->id});
}

void WorkFilter::loadReferenceOptionsAsync()
{
    const QString databasePath = m_database.databaseName();
    QPointer<WorkFilter> guard(this);
    QThreadPool::globalInstance()->start(
        [guard, databasePath]
        {
            SqliteConnection connection;
            QString errorMessage;
            if (!connection.open(databasePath, true, &errorMessage))
            {
                qWarning() << "WorkFilter reference data load failed:" << errorMessage;
                return;
            }
            WorkRepository repository(connection.database());
            const QList<NamedIdOption> makers = repository.makerOptions();
            const QList<NamedIdOption> labels = repository.labelOptions();
            const QList<NamedIdOption> series = repository.seriesOptions();
            if (guard.isNull())
                return;
            QMetaObject::invokeMethod(
                guard,
                [guard, makers, labels, series]
                {
                    if (guard.isNull())
                        return;
                    populateSelector(guard->m_makerFilter, makers);
                    populateSelector(guard->m_labelFilter, labels);
                    populateSelector(guard->m_seriesFilter, series);
                },
                Qt::QueuedConnection);
        });
}

void WorkFilter::clearInputs()
{
    m_filterTimer->stop();
    const QList<QLineEdit *> filterInputs{m_searchInput,
                                          m_serialFilter,
                                          m_titleFilter,
                                          m_storyFilter,
                                          m_directorFilter,
                                          m_actressFilter,
                                          m_actorFilter};
    for (QLineEdit *input : filterInputs)
    {
        const QSignalBlocker blocker(input);
        input->clear();
    }
    for (auto *selector : {m_makerFilter, m_labelFilter, m_seriesFilter})
    {
        const QSignalBlocker blocker(selector);
        const QSignalBlocker editBlocker(selector->lineEdit());
        selector->setMaker(std::nullopt);
    }
    m_routeTagId = 0;
    m_actorId.reset();
    m_tagSelector->clearSelection();
    m_filterTimer->stop();
}

void WorkFilter::reloadReferenceOptions()
{
    QString errorMessage;
    SqliteConnection connection;
    if (!connection.open(m_database.databaseName(), true, &errorMessage))
    {
        ToastNotification::showMessage(window(), QStringLiteral("刷新筛选项失败：%1").arg(errorMessage),
                                       ToastNotification::Level::Error, 4000, &m_themeService);
        return;
    }
    WorkRepository repository(connection.database());
    populateSelector(m_makerFilter, repository.makerOptions());
    populateSelector(m_labelFilter, repository.labelOptions());
    populateSelector(m_seriesFilter, repository.seriesOptions());
}

WorkSortOrder WorkFilter::selectedSortOrder() const
{
    return static_cast<WorkSortOrder>(m_sortSelector->currentData().toInt());
}

WorkSearch WorkFilter::search() const
{
    WorkSearch search;
    search.serialNumber = m_serialFilter->text().trimmed();
    search.title = m_titleFilter->text().trimmed();
    search.chineseStory = m_storyFilter->text().trimmed();
    search.notes = m_searchInput->text().trimmed();
    search.director = m_directorFilter->text().trimmed();
    search.actressName = m_actressFilter->text().trimmed();
    search.actorName = m_actorFilter->text().trimmed();
    search.actorId = m_actorId;
    search.makerId = m_makerFilter->maker();
    search.labelId = m_labelFilter->maker();
    search.seriesId = m_seriesFilter->maker();
    search.randomSeed = m_randomSeed;
    search.randomSeed2 = m_randomSeed2;
    const int scope = m_scopeSelector->currentIndex();
    search.requireLocalVideo = scope == 4;
    search.restrictToIncludedWorkIds = scope >= 1 && scope <= 3;
    if (search.restrictToIncludedWorkIds) {
        SqliteConnection connection;
        if (connection.open(m_privateDatabase.databaseName(), true)) {
            PrivateRepository repository(connection.database());
            switch (scope) {
            case 1: search.includedWorkIds = repository.favoriteWorkIds(); break;
            case 2: search.includedWorkIds = repository.favoriteUnwatchedWorkIds(); break;
            case 3: search.includedWorkIds = repository.masturbationWorkIds(); break;
            }
        }
    }
    search.tagIds = m_tagSelector->selectedIds();
    if (search.tagIds.isEmpty() && m_routeTagId > 0) search.tagIds.append(m_routeTagId);
    search.order = selectedSortOrder();
    return search;
}

} // namespace darkeye
