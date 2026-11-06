#include "ui/pages/WorkPage.h"

#include "darkeye_ui/components/CompleterLineEdit.h"
#include "darkeye_ui/components/DesignButton.h"
#include "darkeye_ui/components/DesignComboBox.h"
#include "darkeye_ui/components/DesignInput.h"
#include "darkeye_ui/components/DesignLabel.h"
#include "darkeye_ui/components/IconButton.h"
#include "darkeye_ui/components/InteractionEffects.h"
#include "darkeye_ui/components/LazyScrollArea.h"
#include "darkeye_ui/components/MakerSelector.h"
#include "darkeye_ui/components/ToastNotification.h"
#include "darkeye_ui/components/TokenControls.h"
#include "database/SqliteConnection.h"
#include "database/repositories/PersonRepository.h"
#include "ui/components/WorkCard.h"
#include "ui/components/WorkTagSelector.h"

#include <QAbstractItemView>
#include <QComboBox>
#include <QHBoxLayout>
#include <QHeaderView>
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

WorkPage::WorkPage(QSqlDatabase database, ThemeService &themeService,
                   QString coverDirectory, QSqlDatabase privateDatabase, QString fanartDirectory,
                   QUrl imageFetchEndpoint, QWidget *parent)
    : LazyWidget(parent), m_themeService(themeService), m_database(database),
      m_privateDatabase(privateDatabase), m_repository(std::move(database)),
      m_privateRepository(std::move(privateDatabase)),
      m_coverDirectory(std::move(coverDirectory)), m_fanartDirectory(std::move(fanartDirectory)),
      m_imageFetchEndpoint(std::move(imageFetchEndpoint))
{
}

void WorkPage::lazyLoad()
{
    m_randomSeed = QRandomGenerator::global()->bounded(1, 1'000'001);
    m_randomSeed2 = QRandomGenerator::global()->bounded(1, 1'000'001);
    const AppSettings appSettings = settings::app();
    m_largeCoverView = appSettings.workLargeCoverView;
    m_tagPanelVisible = appSettings.workTagSelectorVisible;
    buildUi();
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
    // Python WorkPage keeps Qt's six-pixel spacing between the 44px filter bar
    // and the waterfall body. Preserve it so the first card baseline matches.
    root->setSpacing(6);

    auto *filterBar = new QWidget(this);
    filterBar->setObjectName(QStringLiteral("WorkFilterBar"));
    filterBar->setFixedHeight(44);
    auto *filterBarLayout = new QHBoxLayout(filterBar);
    filterBarLayout->setContentsMargins(10, 0, 10, 0);
    filterBarLayout->setSpacing(6);

    m_tagPanelButton = new IconButton(m_tagPanelVisible ? QStringLiteral("panel_left_close")
                                                        : QStringLiteral("panel_left_open"),
                                      &m_themeService, filterBar);
    m_tagPanelButton->setProperty("workControlId", QStringLiteral("WorkTagPanelButton"));
    m_tagPanelButton->setIconPixelSize(22);
    m_tagPanelButton->setButtonPixelSize(28);

    m_tagPanelButton->setToolTip(m_tagPanelVisible ? QStringLiteral("隐藏标签筛选栏")
                                                   : QStringLiteral("显示标签筛选栏"));
    filterBarLayout->addWidget(m_tagPanelButton);

    auto *filterScroll = new QScrollArea(filterBar);
    filterScroll->setObjectName(QStringLiteral("WorkFilterScrollArea"));
    filterScroll->setWidgetResizable(true);
    filterScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    filterScroll->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    filterScroll->setFixedHeight(44);
    filterScroll->setFrameShape(QFrame::NoFrame);
    auto *filterContent = new QWidget(filterScroll);
    auto *filters = new QHBoxLayout(filterContent);
    filters->setContentsMargins(0, 0, 0, 0);
    filters->setSpacing(6);
    filters->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);

    const auto addFilter =
        [filters, filterContent](const QString &label, DesignLineEdit *input, int width)
    {
        filters->addWidget(new DesignLabel(label, filterContent));
        input->setFixedWidth(width);
        filters->addWidget(input);
    };
    const QString databasePath = m_database.databaseName();
    const auto loadSuggestions = [databasePath](auto query)
    {
        SqliteConnection connection;
        QString errorMessage;
        if (!connection.open(databasePath, true, &errorMessage))
        {
            qWarning() << "WorkPage completion data load failed:" << errorMessage;
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
        qWarning() << "WorkPage initial filter data load failed:" << initialLoadError;
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
    const auto addComboFilter =
        [filters, filterContent](const QString &label, QComboBox *combo, int width)
    {
        filters->addWidget(new DesignLabel(label, filterContent));
        combo->setFixedWidth(width);
        filters->addWidget(combo);
    };
    addFilter(QStringLiteral("番号："), m_serialFilter, 120);
    addFilter(QStringLiteral("女优"), m_actressFilter, 120);
    addFilter(QStringLiteral("标题包含："), m_titleFilter, 100);
    addFilter(QStringLiteral("故事包含："), m_storyFilter, 100);
    addFilter(QStringLiteral("简单笔记包含："), m_searchInput, 100);
    addFilter(QStringLiteral("导演"), m_directorFilter, 150);
    addFilter(QStringLiteral("男优"), m_actorFilter, 120);
    m_makerFilter = new MakerSelector({}, filterContent);
    m_makerFilter->setProperty("workControlId", QStringLiteral("WorkMakerFilter"));
    m_labelFilter = new MakerSelector({}, filterContent);
    m_labelFilter->setProperty("workControlId", QStringLiteral("WorkLabelFilter"));
    m_seriesFilter = new MakerSelector({}, filterContent);
    m_seriesFilter->setProperty("workControlId", QStringLiteral("WorkSeriesFilter"));
    addComboFilter(QStringLiteral("片商"), m_makerFilter, 160);
    addComboFilter(QStringLiteral("厂牌"), m_labelFilter, 160);
    addComboFilter(QStringLiteral("系列"), m_seriesFilter, 160);
    filterScroll->setWidget(filterContent);
    filterBarLayout->addWidget(filterScroll, 1);

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
    m_scopeSelector->setProperty("workControlId", QStringLiteral("WorkScopeSelector"));
    m_scopeSelector->addItems({QStringLiteral("公共库范围"), QStringLiteral("收藏库范围"),
                               QStringLiteral("收藏未观看"), QStringLiteral("已撸过"),
                               QStringLiteral("本地有视频")});
    m_scopeSelector->setCurrentIndex(0);
    m_scopeSelector->setToolTip(QStringLiteral("选择公共库、收藏、观看记录或本地视频范围"));
    m_viewButton = new IconButton(m_largeCoverView ? QStringLiteral("layout_grid")
                                                   : QStringLiteral("layout_waterfall"),
                                   &m_themeService, filterBar);
    m_viewButton->setProperty("workControlId", QStringLiteral("WorkCoverViewButton"));
    m_viewButton->setIconPixelSize(22);
    m_viewButton->setButtonPixelSize(28);

    m_viewButton->setToolTip(m_largeCoverView
                                 ? QStringLiteral("当前：大图卡片视图，点击切换为标准瀑布流")
                                 : QStringLiteral("当前：标准卡片视图，点击切换为大图卡片"));

    // Keep all commands and result metadata in an explicit trailing group.
    // This mirrors the Python toolbar and remains right-aligned even when
    // the scrolling filter area has no surplus width.
    auto *trailingControls = new QWidget(filterBar);
    auto *trailingLayout = new QHBoxLayout(trailingControls);
    trailingLayout->setContentsMargins(0, 0, 0, 0);
    trailingLayout->setSpacing(6);
    for (QWidget *control : {static_cast<QWidget *>(refreshButton),
                             static_cast<QWidget *>(clearButton),
                             static_cast<QWidget *>(m_countLabel),
                             static_cast<QWidget *>(m_scopeSelector),
                             static_cast<QWidget *>(m_sortSelector),
                             static_cast<QWidget *>(m_viewButton)})
    {
        control->setParent(trailingControls);
        trailingLayout->addWidget(control);
    }
    filterBarLayout->addWidget(trailingControls, 0, Qt::AlignRight | Qt::AlignVCenter);
    root->addWidget(filterBar);

    m_lazyArea = new LazyScrollArea(m_largeCoverView ? 250 : 220, this);
    m_lazyArea->setPageSize(70);
    auto *body = new QHBoxLayout;
    body->setContentsMargins(0, 0, 0, 0);
    body->setSpacing(0);
    m_tagPanel = new QWidget(this);
    m_tagPanel->setObjectName(QStringLiteral("WorkTagPanel"));
    auto *tagLayout = new QHBoxLayout(m_tagPanel);
    tagLayout->setContentsMargins(0, 0, 0, 0);
    m_tagSelector = new WorkTagSelector(tagOptions, &m_themeService, m_tagPanel);
    m_tagSelector->setSelectedColumnWidth(84);
    applyInitialDefaultTagFilter(tagOptions);
    m_tagSelector->setLoader(
        [this]
        {
            SqliteConnection connection;
            QString errorMessage;
            if (!connection.open(m_database.databaseName(), true, &errorMessage))
            {
                qWarning() << "WorkPage tag load failed:" << errorMessage;
                return QList<TagOption>{};
            }
            WorkRepository repository(connection.database());
            return repository.tagOptions();
        });
    tagLayout->addWidget(m_tagSelector, 0, Qt::AlignLeft);
    m_tagPanel->setVisible(m_tagPanelVisible);
    body->addWidget(m_tagPanel);
    body->addWidget(m_lazyArea, 1);
    root->addLayout(body, 1);

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
    connect(
        m_tagPanelButton, &QPushButton::clicked, this,
        [this]
        {
            m_tagPanelVisible = !m_tagPanel->isVisible();
            m_tagPanel->setVisible(m_tagPanelVisible);
            m_tagPanelButton->setIconName(m_tagPanelVisible ? QStringLiteral("panel_left_close")
                                                            : QStringLiteral("panel_left_open"));
            m_tagPanelButton->setToolTip(m_tagPanelVisible ? QStringLiteral("隐藏标签筛选栏")
                                                           : QStringLiteral("显示标签筛选栏"));
            AppSettings appSettings = settings::app();
            appSettings.workTagSelectorVisible = m_tagPanelVisible;
            settings::saveApp(appSettings);
        });
    connect(clearButton, &QPushButton::clicked, this, &WorkPage::clearFilters);

    connect(m_viewButton, &QPushButton::clicked, this, &WorkPage::toggleCoverSize);
    m_filterTimer = new QTimer(this);
    m_filterTimer->setSingleShot(true);
    m_filterTimer->setInterval(50);
    connect(m_filterTimer, &QTimer::timeout, this, &WorkPage::applyFilters);
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
    connect(m_tagSelector, &WorkTagSelector::selectionChanged, m_filterTimer,
            qOverload<>(&QTimer::start));
    connect(m_sortSelector, &QComboBox::currentIndexChanged, m_filterTimer,
            qOverload<>(&QTimer::start));
    connect(m_scopeSelector, &QComboBox::currentIndexChanged, m_filterTimer,
            qOverload<>(&QTimer::start));
    loadReferenceOptionsAsync();
}

void WorkPage::applyInitialDefaultTagFilter(const QList<TagOption> &tags)
{
    const auto tag = std::find_if(tags.cbegin(), tags.cend(), [](const TagOption &option)
    {
        return option.name == initialDefaultTagName;
    });
    if (tag != tags.cend())
        m_tagSelector->setSelectedIds({tag->id});
}

void WorkPage::loadReferenceOptionsAsync()
{
    const QString databasePath = m_database.databaseName();
    QPointer<WorkPage> guard(this);
    QThreadPool::globalInstance()->start(
        [guard, databasePath]
        {
            SqliteConnection connection;
            QString errorMessage;
            if (!connection.open(databasePath, true, &errorMessage))
            {
                qWarning() << "WorkPage reference data load failed:" << errorMessage;
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

void WorkPage::applyFilters()
{
    refresh();
}

void WorkPage::clearFilters()
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
    m_tagSelector->clearSelection();
    m_filterTimer->stop();
    applyFilters();
}

void WorkPage::toggleCoverSize()
{
    m_largeCoverView = !m_largeCoverView;
    m_lazyArea->setColumnWidth(m_largeCoverView ? 250 : 220);
    m_viewButton->setIconName(m_largeCoverView ? QStringLiteral("layout_grid")
                                               : QStringLiteral("layout_waterfall"));
    m_viewButton->setToolTip(m_largeCoverView
                                 ? QStringLiteral("当前：大图卡片视图，点击切换为标准瀑布流")
                                 : QStringLiteral("当前：标准卡片视图，点击切换为大图卡片"));
    AppSettings appSettings = settings::app();
    appSettings.workLargeCoverView = m_largeCoverView;
    settings::saveApp(appSettings);
    refresh();
}

void WorkPage::refresh()
{
    initialize();
    refreshData();
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
    const std::optional<int> total = repository.count(currentSearch(), &errorMessage);
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
    m_countLabel->setText(QStringLiteral("过滤总数:%1").arg(*total));
    qInfo() << "WorkPage query returned" << *total << "rows";
    m_lazyArea->reset();
}

void WorkPage::focusSearch()
{
    initialize();
    if (m_searchInput == nullptr)
        return;
    m_searchInput->setFocus(Qt::ShortcutFocusReason);
    m_searchInput->selectAll();
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
    initialize();
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
    refresh();
}

void WorkPage::refreshTags()
{
    initialize();
    m_tagSelector->reloadTags();
    refresh();
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
    WorkSearch search = currentSearch();
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
    clearFilters();

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
            m_actorFilter->setText(displayName);
            refreshData();
            return;
        }
    }
}

void WorkPage::filterByTag(qint64 tagId)
{
    if (tagId <= 0) return;
    initialize();
    clearFilters();
    m_tagSelector->setSelectedIds({tagId});
    refreshData();
}

WorkSortOrder WorkPage::selectedSortOrder() const
{
    return static_cast<WorkSortOrder>(m_sortSelector->currentData().toInt());
}

WorkSearch WorkPage::currentSearch() const
{
    WorkSearch search;
    search.serialNumber = m_serialFilter->text().trimmed();
    search.title = m_titleFilter->text().trimmed();
    search.chineseStory = m_storyFilter->text().trimmed();
    search.notes = m_searchInput->text().trimmed();
    search.director = m_directorFilter->text().trimmed();
    search.actressName = m_actressFilter->text().trimmed();
    search.actorName = m_actorFilter->text().trimmed();
    search.makerId = m_makerFilter->maker();
    search.labelId = m_labelFilter->maker();
    search.seriesId = m_seriesFilter->maker();
    search.randomSeed = m_randomSeed;
    search.randomSeed2 = m_randomSeed2;
    QString privateError;
    switch (m_scopeSelector->currentIndex())
    {
    case 1:
        search.restrictToIncludedWorkIds = true;
        {
            SqliteConnection connection;
            if (connection.open(m_privateDatabase.databaseName(), true, &privateError))
            {
                PrivateRepository repository(connection.database());
                search.includedWorkIds = repository.favoriteWorkIds(&privateError);
            }
        }
        break;
    case 2:
        search.restrictToIncludedWorkIds = true;
        {
            SqliteConnection connection;
            if (connection.open(m_privateDatabase.databaseName(), true, &privateError))
            {
                PrivateRepository repository(connection.database());
                search.includedWorkIds = repository.favoriteUnwatchedWorkIds(&privateError);
            }
        }
        break;
    case 3:
        search.restrictToIncludedWorkIds = true;
        {
            SqliteConnection connection;
            if (connection.open(m_privateDatabase.databaseName(), true, &privateError))
            {
                PrivateRepository repository(connection.database());
                search.includedWorkIds = repository.masturbationWorkIds(&privateError);
            }
        }
        break;
    case 4:
        search.requireLocalVideo = true;
        break;
    default:
        break;
    }
    search.tagIds = m_tagSelector->selectedIds();
    search.order = selectedSortOrder();
    return search;
}

} // namespace darkeye
