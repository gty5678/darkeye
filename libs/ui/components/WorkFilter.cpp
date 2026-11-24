#include "ui/components/WorkFilter.h"

#include "darkeye_ui/components/CollapsibleSection.h"
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
#include "settings/Settings.h"
#include "ui/components/WorkTagSelector.h"

#include <QComboBox>
#include <QCoreApplication>
#include <QEvent>
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

class WorkFilterScrollArea final : public QScrollArea
{
public:
    using QScrollArea::QScrollArea;

    QSize sizeHint() const override
    {
        QSize hint = QScrollArea::sizeHint();
        // The form's preferred height changes when the disclosure is toggled.
        if (widget())
            hint.setHeight(widget()->sizeHint().height());
        return hint;
    }
};

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
                       ThemeService &themeService, QWidget *parent, QString settingsFile)
    : LazyWidget(parent), m_themeService(themeService), m_database(std::move(database)),
      m_privateDatabase(std::move(privateDatabase)), m_settingsFile(std::move(settingsFile))
{
}

void WorkFilter::setupScopeSelector(QComboBox &selector, int initialScope)
{
    selector.addItem(QStringLiteral("公共库范围"), static_cast<int>(Scope::Public));
    selector.addItem(QStringLiteral("收藏库范围"), static_cast<int>(Scope::Favorites));
    selector.addItem(QStringLiteral("收藏未观看"), static_cast<int>(Scope::UnwatchedFavorites));
    selector.addItem(QStringLiteral("已撸过"), static_cast<int>(Scope::Watched));
    selector.addItem(QStringLiteral("本地有视频"), static_cast<int>(Scope::LocalVideo));

    const int index = selector.findData(initialScope);
    selector.setCurrentIndex(index >= 0 ? index : selector.findData(static_cast<int>(Scope::Public)));
}

void WorkFilter::setupSortSelector(QComboBox &selector, int initialSortOrder)
{
    selector.addItem(QStringLiteral("随机顺序"), static_cast<int>(WorkSortOrder::Random));
    selector.addItem(QStringLiteral("添加逆序"),
                     static_cast<int>(WorkSortOrder::CreatedDescending));
    selector.addItem(QStringLiteral("添加顺序"),
                     static_cast<int>(WorkSortOrder::CreatedAscending));
    selector.addItem(QStringLiteral("番号顺序"),
                     static_cast<int>(WorkSortOrder::SerialAscending));
    selector.addItem(QStringLiteral("番号逆序"),
                     static_cast<int>(WorkSortOrder::SerialDescending));
    selector.addItem(QStringLiteral("制作商顺序"),
                     static_cast<int>(WorkSortOrder::MakerAscending));
    selector.addItem(QStringLiteral("制作商逆序"),
                     static_cast<int>(WorkSortOrder::MakerDescending));
    selector.addItem(QStringLiteral("更新时间顺序"),
                     static_cast<int>(WorkSortOrder::UpdatedAscending));
    selector.addItem(QStringLiteral("更新时间逆序"),
                     static_cast<int>(WorkSortOrder::UpdatedDescending));
    selector.addItem(QStringLiteral("发布时间逆序"),
                     static_cast<int>(WorkSortOrder::ReleaseDateDescending));
    selector.addItem(QStringLiteral("发布时间顺序"),
                     static_cast<int>(WorkSortOrder::ReleaseDateAscending));
    selector.addItem(QStringLiteral("拍摄年龄顺序"),
                     static_cast<int>(WorkSortOrder::ActressAgeAscending));
    selector.addItem(QStringLiteral("拍摄年龄逆序"),
                     static_cast<int>(WorkSortOrder::ActressAgeDescending));
    const int index = selector.findData(initialSortOrder);
    selector.setCurrentIndex(index >= 0 ? index
        : selector.findData(static_cast<int>(WorkSortOrder::CreatedDescending)));
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
    m_moreFilters->expand();
    m_searchInput->setFocus(Qt::ShortcutFocusReason);
    m_searchInput->selectAll();
}

bool WorkFilter::flush()
{
    if (!m_filterTimer->isActive()) return false;
    applyFilters();
    return true;
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

void WorkFilter::resetToPublicLibrary(std::optional<WorkSortOrder> order)
{
    m_scopeSelector->setCurrentIndex(m_scopeSelector->findData(static_cast<int>(Scope::Public)));
    if (order)
        m_sortSelector->setCurrentIndex(m_sortSelector->findData(static_cast<int>(*order)));
    clearFilters();
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
    m_moreFilters->expand();
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
    m_moreFilters->expand();
    m_directorFilter->setText(director);
    applyFilters();
}

void WorkFilter::filterByActor(qint64 id, const QString &name)
{
    initialize();
    clearInputs();
    m_moreFilters->expand();
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
    filterBarLayout->setContentsMargins(0, 8, 0, 10);
    filterBarLayout->setSpacing(8);

    auto *filterContent = new QWidget(filterBar);
    filterContent->setObjectName(QStringLiteral("WorkFilterContent"));
    filterContent->setMaximumWidth(580);
    auto *filters = new QFormLayout(filterContent);
    filters->setContentsMargins(0, 0, 0, 0);
    filters->setSpacing(6);
    filters->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);
    filters->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    m_moreFilters = new TokenCollapsibleSection(QStringLiteral("更多筛选"), &m_themeService,
                                                filterContent);
    // Paint the borderless section directly so field edges cannot be sampled as its frame.
    m_moreFilters->setObjectName(QStringLiteral("WorkMoreFilters"));
    auto *moreContent = m_moreFilters->contentWidget();
    auto *moreFilters = new QFormLayout;
    m_moreFilters->contentLayout()->setContentsMargins(0, 6, 0, 0);
    m_moreFilters->contentLayout()->addLayout(moreFilters);
    moreFilters->setContentsMargins(0, 0, 0, 0);
    moreFilters->setSpacing(6);
    moreFilters->setLabelAlignment(filters->labelAlignment());
    moreFilters->setFieldGrowthPolicy(filters->fieldGrowthPolicy());
    const auto addFilter = [](QFormLayout *form, const QString &label, QWidget *input)
    {
        auto *caption = new DesignLabel(label);
        caption->setBuddy(input);
        form->addRow(caption, input);
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
    m_titleFilter = new DesignLineEdit(moreContent);
    m_titleFilter->setProperty("workControlId", QStringLiteral("WorkTitleFilter"));
    m_searchInput = new DesignLineEdit(moreContent);
    m_searchInput->setProperty("workControlId", QStringLiteral("WorkNotesFilter"));
    m_storyFilter = new DesignLineEdit(moreContent);
    m_storyFilter->setProperty("workControlId", QStringLiteral("WorkStoryFilter"));
    m_directorFilter = new CompleterLineEdit(
        [loadSuggestions]
        { return loadSuggestions(&WorkRepository::directorSuggestions); }, moreContent);
    m_directorFilter->setProperty("workControlId", QStringLiteral("WorkDirectorFilter"));
    m_actorFilter = new CompleterLineEdit(
        [loadSuggestions]
        { return loadSuggestions(&WorkRepository::actorSuggestions); }, moreContent);
    m_actorFilter->setProperty("workControlId", QStringLiteral("WorkActorFilter"));
    m_makerFilter = new MakerSelector({}, moreContent);
    m_makerFilter->setProperty("workControlId", QStringLiteral("WorkMakerFilter"));
    m_labelFilter = new MakerSelector({}, moreContent);
    m_labelFilter->setProperty("workControlId", QStringLiteral("WorkLabelFilter"));
    m_seriesFilter = new MakerSelector({}, moreContent);
    m_seriesFilter->setProperty("workControlId", QStringLiteral("WorkSeriesFilter"));

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
    m_countLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);

    const auto appSettings = settings::app(m_settingsFile);
    m_sortSelector = new DesignComboBox(filterBar);
    m_sortSelector->setProperty("compactFilter", true);
    m_sortSelector->setProperty("workControlId", QStringLiteral("WorkSortSelector"));
    setupSortSelector(*m_sortSelector, appSettings.workFilterInitialSortOrder);

    m_scopeSelector = new DesignComboBox(filterBar);
    m_scopeSelector->setProperty("compactFilter", true);
    m_scopeSelector->setProperty("workControlId", QStringLiteral("WorkScopeSelector"));
    setupScopeSelector(*m_scopeSelector, appSettings.workFilterInitialScope);
    m_scopeSelector->setToolTip(QStringLiteral("选择公共库、收藏、观看记录或本地视频范围"));
    addFilter(filters, QStringLiteral("范围"), m_scopeSelector);
    addFilter(filters, QStringLiteral("排序"), m_sortSelector);
    addFilter(filters, QStringLiteral("番号"), m_serialFilter);
    addFilter(filters, QStringLiteral("女优"), m_actressFilter);
    addFilter(moreFilters, QStringLiteral("标题包含"), m_titleFilter);
    addFilter(moreFilters, QStringLiteral("故事包含"), m_storyFilter);
    addFilter(moreFilters, QStringLiteral("笔记包含"), m_searchInput);
    addFilter(moreFilters, QStringLiteral("导演"), m_directorFilter);
    addFilter(moreFilters, QStringLiteral("男优"), m_actorFilter);
    addFilter(moreFilters, QStringLiteral("片商"), m_makerFilter);
    addFilter(moreFilters, QStringLiteral("厂牌"), m_labelFilter);
    addFilter(moreFilters, QStringLiteral("系列"), m_seriesFilter);
    filters->addRow(m_moreFilters);
    auto *toolbar = new QHBoxLayout;
    toolbar->addStretch();
    toolbar->addWidget(m_countLabel);
    toolbar->addWidget(clearButton);
    toolbar->addWidget(refreshButton);
    filterBarLayout->addLayout(toolbar);
    auto *filterScroll = new WorkFilterScrollArea(filterBar);
    filterScroll->setObjectName(QStringLiteral("WorkFilterScrollArea"));
    filterScroll->viewport()->setObjectName(QStringLiteral("WorkFilterViewport"));
    filterScroll->setWidgetResizable(true);
    filterScroll->setFrameShape(QFrame::NoFrame);
    filterScroll->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    filterScroll->setWidget(filterContent);
    filterScroll->setMinimumHeight(100);
    filterScroll->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Maximum);
    filterBarLayout->addWidget(filterScroll, 1);
    root->addWidget(filterBar);

    auto *tagPanel = new QWidget(this);
    tagPanel->setObjectName(QStringLiteral("WorkTagPanel"));
    auto *tagLayout = new QHBoxLayout(tagPanel);
    tagLayout->setContentsMargins(0, 0, 0, 0);
    auto *tagScroll = new QScrollArea(tagPanel);
    tagScroll->setWidgetResizable(true);
    tagScroll->setFrameShape(QFrame::NoFrame);
    tagScroll->setStyleSheet(QStringLiteral("QScrollArea { border: none; outline: none; }"));
    tagScroll->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_tagSelector = new WorkTagSelector(tagOptions, &m_themeService);
    tagScroll->setWidget(m_tagSelector);
    m_tagSelector->setSelectedColumnLayout(WorkTagSelector::SelectedColumnLayout::Compact);
    m_tagSelector->useResponsiveLayout();
    if (appSettings.workFilterAddGreenCover)
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
    root->addWidget(tagPanel, 1);

    connect(m_moreFilters, &TokenCollapsibleSection::toggled, this,
            [this, filters, filterScroll, filterBar, root]
            {
                // Commit the form height and scroll offset before repainting either panel.
                const bool repaint = updatesEnabled();
                if (repaint) setUpdatesEnabled(false);
                m_moreFilters->layout()->activate();
                filters->activate();
                filterScroll->updateGeometry();
                filterBar->updateGeometry();
                root->activate();
                QEvent request(QEvent::LayoutRequest);
                QCoreApplication::sendEvent(filterScroll, &request);
                if (repaint) setUpdatesEnabled(true);
            });

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
    connect(m_moreFilters, &TokenCollapsibleSection::toggled, this,
            &WorkFilter::loadReferenceOptionsAsync, Qt::SingleShotConnection);
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
    return searchRequest().resolve();
}

WorkFilter::SearchRequest WorkFilter::searchRequest() const
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
    search.tagIds = m_tagSelector->selectedIds();
    if (search.tagIds.isEmpty() && m_routeTagId > 0) search.tagIds.append(m_routeTagId);
    search.order = selectedSortOrder();
    return {std::move(search), static_cast<Scope>(m_scopeSelector->currentData().toInt()),
            m_privateDatabase.databaseName()};
}

WorkSearch WorkFilter::SearchRequest::resolve(QString *error) const
{
    WorkSearch search = filters;
    search.requireLocalVideo = scope == Scope::LocalVideo;
    search.restrictToIncludedWorkIds =
        scope == Scope::Favorites || scope == Scope::UnwatchedFavorites || scope == Scope::Watched;
    if (!search.restrictToIncludedWorkIds) return search;

    SqliteConnection connection;
    if (!connection.open(privateDatabasePath, true, error)) return search;
    PrivateRepository repository(connection.database());
    switch (scope) {
    case Scope::Favorites: search.includedWorkIds = repository.favoriteWorkIds(error); break;
    case Scope::UnwatchedFavorites: search.includedWorkIds = repository.favoriteUnwatchedWorkIds(error); break;
    case Scope::Watched: search.includedWorkIds = repository.masturbationWorkIds(error); break;
    case Scope::Public:
    case Scope::LocalVideo:
        break;
    }
    return search;
}

} // namespace darkeye
