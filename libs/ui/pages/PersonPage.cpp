#include "ui/pages/PersonPage.h"

#include "darkeye_ui/components/CompleterLineEdit.h"
#include "darkeye_ui/components/DesignComboBox.h"
#include "darkeye_ui/components/DesignLabel.h"
#include "darkeye_ui/components/InteractionEffects.h"
#include "darkeye_ui/components/LazyScrollArea.h"
#include "darkeye_ui/components/OctImage.h"
#include "darkeye_ui/components/ToastNotification.h"
#include "ui/components/PersonCard.h"

#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QRandomGenerator>
#include <QTimer>
#include <QVBoxLayout>

namespace darkeye
{

PersonPage::PersonPage(PersonKind kind, QSqlDatabase publicDatabase, QSqlDatabase privateDatabase,
                       ThemeService &themeService, QString imageDirectory, QWidget *parent)
    : LazyWidget(parent), m_kind(kind), m_repository(std::move(publicDatabase)),
      m_privateRepository(std::move(privateDatabase)), m_themeService(themeService),
      m_imageDirectory(std::move(imageDirectory))
{
    setFocusPolicy(Qt::ClickFocus);
}

void PersonPage::lazyLoad()
{
    m_randomSeed = QRandomGenerator::global()->generate();
    m_randomSeed2 = QRandomGenerator::global()->generate();
    buildUi();
    m_lazyArea->setViewportItemHandler([](QWidget *widget, bool active) {
        static_cast<PersonCard *>(widget)->avatar()->setImageActive(active);
    });
    m_lazyArea->setLoader([this](int pageIndex, int pageSize)
                          { return loadCardPage(pageIndex, pageSize); });
    updateCount();
}

void PersonPage::showEvent(QShowEvent *event)
{
    LazyWidget::showEvent(event);
    // Reset the stack's focus target on every visit, including after the user
    // previously edited the name filter, so its completer does not open on entry.
    setFocus(Qt::OtherFocusReason);
}

void PersonPage::buildUi()
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(6);

    auto *filterBar = new QWidget(this);
    filterBar->setObjectName(QStringLiteral("PersonFilterBar"));
    // Match WorkPage: a 44px bar with naturally sized, centered controls.
    filterBar->setFixedHeight(44);
    auto *filters = new QHBoxLayout(filterBar);
    filters->setContentsMargins(10, 0, 10, 0);
    filters->setSpacing(6);

    filters->addWidget(new DesignLabel(m_kind == PersonKind::Actress ? QStringLiteral("女优")
                                                                     : QStringLiteral("男优"),
                                       filterBar));
    const QStringList nameSuggestions = m_repository.nameSuggestions(m_kind);
    m_nameInput = new CompleterLineEdit([nameSuggestions] { return nameSuggestions; }, filterBar);
    m_nameInput->setFixedWidth(180);
    filters->addWidget(m_nameInput);

    m_cupSelector = new DesignComboBox(filterBar);
    m_cupSelector->addItem(QString());
    m_cupSelector->addItems(m_repository.cupOptions());
    auto *cupLabel = new DesignLabel(QStringLiteral("罩杯"), filterBar);
    for (QWidget *control : {static_cast<QWidget *>(cupLabel),
                            static_cast<QWidget *>(m_cupSelector)}) {
        auto policy = control->sizePolicy();
        policy.setRetainSizeWhenHidden(true);
        control->setSizePolicy(policy);
        filters->addWidget(control);
        control->setVisible(m_kind == PersonKind::Actress);
    }

    // Keep secondary actions in one trailing group.  The Python pages place
    // refresh, reset and result information at the right edge of this bar.
    auto *trailingControls = new QWidget(filterBar);
    auto *trailingLayout = new QHBoxLayout(trailingControls);
    trailingLayout->setContentsMargins(0, 0, 0, 0);
    trailingLayout->setSpacing(6);

    auto *refreshButton =
        new RotateButton(QStringLiteral("refresh_cw"), &m_themeService, trailingControls);
    refreshButton->setIconPixelSize(24);
    refreshButton->setButtonPixelSize(24);
    refreshButton->setToolTip(QStringLiteral("刷新人物"));
    auto *clearButton =
        new ShakeButton(QStringLiteral("eraser"), &m_themeService, trailingControls);
    clearButton->setIconPixelSize(24);
    clearButton->setButtonPixelSize(24);
    clearButton->setToolTip(QStringLiteral("清空筛选"));

    m_countLabel = new DesignLabel({}, trailingControls);
    m_countLabel->setFixedWidth(100);
    filters->addStretch();
    trailingLayout->addWidget(refreshButton);
    trailingLayout->addWidget(clearButton);
    trailingLayout->addWidget(m_countLabel);

    m_scopeSelector = new DesignComboBox(trailingControls);
    m_scopeSelector->addItem(QStringLiteral("公共库范围"), false);
    m_scopeSelector->addItem(QStringLiteral("收藏库范围"), true);
    auto scopePolicy = m_scopeSelector->sizePolicy();
    scopePolicy.setRetainSizeWhenHidden(true);
    m_scopeSelector->setSizePolicy(scopePolicy);
    trailingLayout->addWidget(m_scopeSelector);
    m_scopeSelector->setVisible(m_kind == PersonKind::Actress);

    m_sortSelector = new DesignComboBox(trailingControls);
    m_sortSelector->addItem(QStringLiteral("随机顺序"), static_cast<int>(PersonSortOrder::Random));
    m_sortSelector->addItem(QStringLiteral("添加顺序"),
                            static_cast<int>(PersonSortOrder::CreatedAscending));
    m_sortSelector->addItem(QStringLiteral("添加逆序"),
                            static_cast<int>(PersonSortOrder::CreatedDescending));
    if (m_kind == PersonKind::Actress)
    {
        m_sortSelector->addItem(QStringLiteral("年龄顺序"),
                                static_cast<int>(PersonSortOrder::BirthdayAscending));
        m_sortSelector->addItem(QStringLiteral("年龄逆序"),
                                static_cast<int>(PersonSortOrder::BirthdayDescending));
        m_sortSelector->addItem(QStringLiteral("出道顺序"),
                                static_cast<int>(PersonSortOrder::DebutAscending));
        m_sortSelector->addItem(QStringLiteral("出道逆序"),
                                static_cast<int>(PersonSortOrder::DebutDescending));
        m_sortSelector->addItem(QStringLiteral("身高顺序"),
                                static_cast<int>(PersonSortOrder::HeightAscending));
        m_sortSelector->addItem(QStringLiteral("身高逆序"),
                                static_cast<int>(PersonSortOrder::HeightDescending));
        m_sortSelector->addItem(QStringLiteral("罩杯顺序"),
                                static_cast<int>(PersonSortOrder::CupAscending));
        m_sortSelector->addItem(QStringLiteral("罩杯逆序"),
                                static_cast<int>(PersonSortOrder::CupDescending));
        m_sortSelector->addItem(QStringLiteral("腰臀比顺序"),
                                static_cast<int>(PersonSortOrder::WaistHipRatioAscending));
        m_sortSelector->addItem(QStringLiteral("腰臀比逆序"),
                                static_cast<int>(PersonSortOrder::WaistHipRatioDescending));
    }
    else
    {
        m_sortSelector->addItem(QStringLiteral("封面优先"),
                                static_cast<int>(PersonSortOrder::ImageFirst));
    }
    m_sortSelector->setCurrentIndex(
        m_sortSelector->findData(static_cast<int>(PersonSortOrder::CreatedDescending)));
    trailingLayout->addWidget(m_sortSelector);
    filters->addWidget(trailingControls, 0, Qt::AlignRight | Qt::AlignVCenter);
    m_sortSelector->setMinimumContentsLength(5);
    m_sortSelector->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    filters->setAlignment(Qt::AlignVCenter);
    trailingLayout->setAlignment(Qt::AlignVCenter);
    root->addWidget(filterBar);

    m_lazyArea = new LazyScrollArea(150, this);
    root->addWidget(m_lazyArea, 1);

    m_filterTimer = new QTimer(this);
    m_filterTimer->setSingleShot(true);
    m_filterTimer->setInterval(50);
    connect(m_filterTimer, &QTimer::timeout, this, &PersonPage::applyFilters);
    connect(m_nameInput, &CompleterLineEdit::textChanged, m_filterTimer,
            qOverload<>(&QTimer::start));
    connect(m_cupSelector, &QComboBox::currentIndexChanged, this,
            [this] { m_filterTimer->start(); });
    connect(m_scopeSelector, &QComboBox::currentIndexChanged, this,
            [this] { m_filterTimer->start(); });
    connect(m_sortSelector, &QComboBox::currentIndexChanged, this,
            [this] { m_filterTimer->start(); });
    connect(refreshButton, &QPushButton::clicked, this, &PersonPage::refresh);
    connect(clearButton, &QPushButton::clicked, this, &PersonPage::clearFilters);
}

void PersonPage::refresh()
{
    initialize();
    if (static_cast<PersonSortOrder>(m_sortSelector->currentData().toInt()) ==
        PersonSortOrder::Random)
    {
        m_randomSeed = QRandomGenerator::global()->generate();
        m_randomSeed2 = QRandomGenerator::global()->generate();
    }
    m_lazyArea->reset();
    updateCount();
}

QWidget *PersonPage::captureContent()
{
    initialize();
    if (m_lazyArea == nullptr)
        return nullptr;
    return m_lazyArea->widget();
}

PersonKind PersonPage::kind() const noexcept
{
    return m_kind;
}

void PersonPage::applyFilters()
{
    m_lazyArea->reset();
    updateCount();
}

void PersonPage::clearFilters()
{
    m_nameInput->clear();
    m_cupSelector->setCurrentIndex(0);
    m_scopeSelector->setCurrentIndex(0);
    m_sortSelector->setCurrentIndex(
        m_sortSelector->findData(static_cast<int>(PersonSortOrder::CreatedDescending)));
    applyFilters();
}

void PersonPage::updateCount()
{
    QString errorMessage;
    const std::optional<int> total = m_repository.count(currentSearch(), &errorMessage);
    if (!total.has_value())
    {
        m_countLabel->setText(QStringLiteral("查询失败"));
        Toast::showError(this, errorMessage, &m_themeService);
        return;
    }
    m_countLabel->setText(QStringLiteral("过滤总数:%1").arg(*total));
}

PersonSearch PersonPage::currentSearch() const
{
    PersonSearch search;
    search.kind = m_kind;
    search.name = m_nameInput->text();
    search.cup = m_kind == PersonKind::Actress ? m_cupSelector->currentText() : QString();
    search.sortOrder = static_cast<PersonSortOrder>(m_sortSelector->currentData().toInt());
    search.randomSeed = m_randomSeed;
    search.randomSeed2 = m_randomSeed2;
    if (m_kind == PersonKind::Actress && m_scopeSelector->currentData().toBool())
    {
        search.restrictToIncludedIds = true;
        search.includedIds = m_privateRepository.favoriteActressIds();
    }
    return search;
}

QList<QWidget *> PersonPage::loadCardPage(int pageIndex, int pageSize)
{
    PersonSearch search = currentSearch();
    search.limit = pageSize;
    search.offset = pageIndex * pageSize;
    QString errorMessage;
    const QList<PersonSummary> people = m_repository.search(search, &errorMessage);
    if (!errorMessage.isEmpty())
    {
        Toast::showError(this, errorMessage, &m_themeService);
        return {};
    }
    QList<QWidget *> cards;
    cards.reserve(people.size());
    for (const PersonSummary &person : people)
    {
        auto *card = new PersonCard(person.id, person.name, person.imagePath, m_imageDirectory, nullptr, true);
        connect(card, &PersonCard::activated, this,
                [this](qint64 personId) { emit detailRequested(m_kind, personId); });
        connect(card, &PersonCard::editRequested, this,
                [this](qint64 personId) { emit editRequested(m_kind, personId); });
        cards.append(card);
    }
    return cards;
}

} // namespace darkeye
