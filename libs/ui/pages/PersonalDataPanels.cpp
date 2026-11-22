#include "ui/pages/PersonalDataPanels.h"

#include "ui/pages/PageScrollArea.h"

#include "database/repositories/PrivateRepository.h"
#include "database/repositories/StatisticsRepository.h"
#include "darkeye_ui/components/Charts.h"
#include "darkeye_ui/components/DesignButton.h"
#include "darkeye_ui/components/DesignLabel.h"
#include "darkeye_ui/components/IconButton.h"
#include "darkeye_ui/components/SoftShadow.h"
#include "ui/components/PersonCard.h"
#include "ui/components/StatsOverviewCards.h"
#include "darkeye_ui/theme/ChamferPath.h"
#include "darkeye_ui/theme/ThemeService.h"

#include <QButtonGroup>
#include <QDate>
#include <QFrame>
#include <QEvent>
#include <QHBoxLayout>
#include <QPainter>
#include <QPainterPath>
#include <QPointer>
#include <QScrollArea>
#include <QStackedWidget>
#include <QTimer>
#include <QThreadPool>
#include <QVBoxLayout>

namespace darkeye
{

class OctagonCard : public QWidget
{
public:
    OctagonCard(ThemeService &themeService, const QMargins &margins, QWidget *parent)
        : QWidget(parent), m_themeService(themeService)
    {
        setObjectName(QStringLiteral("PersonalSummaryCard"));
        // Preserve the 170x250 card body and give the shadow its own space.
        setFixedSize(170 + 2 * kShadowMargin, 250 + 2 * kShadowMargin);
        setAttribute(Qt::WA_TranslucentBackground);
        setAutoFillBackground(false);

        m_layout = new QVBoxLayout(this);
        m_layout->setContentsMargins(margins + QMargins(kShadowMargin, kShadowMargin,
                                                       kShadowMargin, kShadowMargin));
        connect(&m_themeService, &ThemeService::themeChanged, this,
                [this] { update(); });
        requestShadow();
    }

public:
    QVBoxLayout *contentLayout() const { return m_layout; }

protected:
    bool event(QEvent *event) override
    {
        const bool handled = QWidget::event(event);
        if (event->type() == QEvent::DevicePixelRatioChange)
            requestShadow();
        return handled;
    }

    void paintEvent(QPaintEvent *event) override
    {
        Q_UNUSED(event);
        const ThemeTokens tokens = m_themeService.currentTokens();
        QPainter painter(this);
        if (!m_shadow.isNull()) painter.drawPixmap(0, 0, m_shadow);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(tokens.background));
        painter.drawPath(cardPath());
    }

private:
    QPainterPath cardPath() const
    {
        return chamferPath(QRectF(rect()).adjusted(kShadowMargin, kShadowMargin,
                                                  -kShadowMargin, -kShadowMargin), 20);
    }

    void requestShadow()
    {
        const quint64 request = ++m_shadowRequest;
        const qreal dpr = devicePixelRatioF();
        const QSize pixels(qCeil(width() * dpr), qCeil(height() * dpr));
        const auto path = cardPath();
        QPointer<OctagonCard> guard(this);
        QThreadPool::globalInstance()->start([guard, request, dpr, pixels, path] {
            const QImage image = renderSoftShadow(pixels, path, dpr);
            if (!guard) return;
            QMetaObject::invokeMethod(guard, [guard, request, image] {
                if (!guard || guard->m_shadowRequest != request) return;
                guard->m_shadow = QPixmap::fromImage(image);
                guard->update();
            }, Qt::QueuedConnection);
        });
    }

    static constexpr int kShadowMargin = 8;
    ThemeService &m_themeService;
    QVBoxLayout *m_layout = nullptr;
    QPixmap m_shadow;
    quint64 m_shadowRequest = 0;
};

class TopActressCard final : public OctagonCard
{
    Q_OBJECT

public:
    TopActressCard(std::optional<int> days, QString imageDirectory, ThemeService &themeService,
                   QWidget *parent = nullptr)
        : OctagonCard(themeService, QMargins(10, 0, 10, 10), parent),
          m_days(days)
    {
        const QString titleText = days ? QStringLiteral("过去%1天最喜欢的女优").arg(*days)
                                       : QStringLiteral("历史最喜欢的女优");
        auto *title = new DesignLabel(titleText, this);
        title->setAlignment(Qt::AlignCenter);
        m_personCard = new PersonCard(0, QStringLiteral("加载中..."), {},
                                      imageDirectory, this);
        contentLayout()->addWidget(title, 0, Qt::AlignCenter);
        contentLayout()->addWidget(m_personCard, 0, Qt::AlignCenter);
        connect(m_personCard, &PersonCard::activated, this, [this](qint64 actressId) {
            if (actressId > 0)
                emit actressDetailRequested(actressId);
        });
        connect(m_personCard, &PersonCard::editRequested, this, [this](qint64 actressId) {
            if (actressId > 0)
                emit actressEditRequested(actressId);
        });
    }

    std::optional<int> days() const { return m_days; }

    void setStatistic(const std::optional<TopActressStatistic> &statistic)
    {
        if (!statistic.has_value())
        {
            m_personCard->updateData(0, QStringLiteral("暂无记录"), {});
            return;
        }
        m_personCard->updateData(statistic->actressId, statistic->name,
                                 statistic->imagePath);
    }

signals:
    void actressDetailRequested(qint64 actressId);
    void actressEditRequested(qint64 actressId);

private:
    std::optional<int> m_days;
    PersonCard *m_personCard = nullptr;
};

namespace
{
QVBoxLayout *createPanelLayout(LazyWidget *page)
{
    auto *content = addPageScrollArea(page);
    content->setObjectName(QStringLiteral("PersonalPanelCanvas"));
    content->setAttribute(Qt::WA_StyledBackground);
    auto *layout = new QVBoxLayout(content);
    layout->setContentsMargins(12, 8, 12, 8);
    return layout;
}
} // namespace

LibraryOverviewPage::LibraryOverviewPage(QSqlDatabase publicDatabase, QSqlDatabase privateDatabase,
                                         ThemeService &themeService, QWidget *parent)
    : LazyWidget(parent), m_publicDatabase(std::move(publicDatabase)),
      m_privateDatabase(std::move(privateDatabase)), m_themeService(themeService)
{
}

void LibraryOverviewPage::lazyLoad()
{
    auto *root = createPanelLayout(this);
    m_overview = new StatsOverviewCards(m_publicDatabase, m_privateDatabase, m_themeService,
                                       root->parentWidget());
    root->addWidget(m_overview);
    root->addStretch();
}

void LibraryOverviewPage::refresh()
{
    if (isInitialized())
        m_overview->refresh();
}

TopActressesPage::TopActressesPage(QSqlDatabase publicDatabase, QSqlDatabase privateDatabase,
                                 ThemeService &themeService, QString actressImageDirectory,
                                 QWidget *parent)
    : LazyWidget(parent), m_publicDatabase(std::move(publicDatabase)),
      m_privateDatabase(std::move(privateDatabase)), m_themeService(themeService),
      m_actressImageDirectory(std::move(actressImageDirectory))
{
}

void TopActressesPage::lazyLoad()
{
    auto *root = createPanelLayout(this);
    auto *summary = new QHBoxLayout;
    summary->setContentsMargins(0, 0, 0, 0);
    summary->setSpacing(6);
    summary->addStretch();
    const std::optional<int> periods[] = {30, 180, 365, std::nullopt};
    for (const auto days : periods)
    {
        auto *card = new TopActressCard(days, m_actressImageDirectory,
                                        m_themeService, root->parentWidget());
        connect(card, &TopActressCard::actressDetailRequested, this,
                &TopActressesPage::actressDetailRequested);
        connect(card, &TopActressCard::actressEditRequested, this,
                &TopActressesPage::actressEditRequested);
        m_topActressCards.append(card);
        summary->addWidget(card);
    }
    summary->addStretch();
    root->addLayout(summary);

    root->addStretch();
    refresh();
}

void TopActressesPage::refresh()
{
    StatisticsRepository statistics(m_publicDatabase, m_privateDatabase);
    for (auto *card : m_topActressCards)
        card->setStatistic(statistics.topActress(card->days()));
}

RecordHeatmapPage::RecordHeatmapPage(QSqlDatabase publicDatabase, QSqlDatabase privateDatabase,
                                     ThemeService &themeService, QWidget *parent)
    : LazyWidget(parent), m_publicDatabase(std::move(publicDatabase)),
      m_privateDatabase(std::move(privateDatabase)), m_themeService(themeService)
{
}

void RecordHeatmapPage::lazyLoad()
{
    auto *root = createPanelLayout(this);
    // Keep the heatmap controls inside one centered panel, as in the Python UI:
    // title and record-type arrows on the left, a vertically scrollable year list on the right.
    auto *heatmapPanel = new QWidget(root->parentWidget());
    heatmapPanel->setObjectName(QStringLiteral("PersonalRecordPanel"));
    auto *panelLayout = new QHBoxLayout(heatmapPanel);
    panelLayout->setContentsMargins(0, 0, 0, 0);

    auto *heatmapColumn = new QVBoxLayout;
    auto *heatmapHeader = new QHBoxLayout;
    StatisticsRepository statistics(m_publicDatabase, m_privateDatabase);
    const int currentYear = QDate::currentDate().year();
    const int earliestYear = qMin(currentYear, statistics.earliestRecordYear());
    m_currentYear = currentYear;

    m_heatmapTitle = new DesignLabel(QStringLiteral("加载中..."), heatmapPanel);
    m_heatmapTitle->setObjectName(QStringLiteral("RecordHeatmapTitle"));
    auto *previousKind = new IconPushButton(QStringLiteral("arrow_up"), &m_themeService,
                                             heatmapPanel);
    previousKind->setObjectName(QStringLiteral("RecordKindPrevious"));
    auto *nextKind = new IconPushButton(QStringLiteral("arrow_down"), &m_themeService,
                                         heatmapPanel);
    nextKind->setObjectName(QStringLiteral("RecordKindNext"));
    heatmapHeader->addWidget(m_heatmapTitle);
    heatmapHeader->addWidget(previousKind);
    heatmapHeader->addWidget(nextKind);
    heatmapColumn->addLayout(heatmapHeader);

    // Match the Python SwitchHeapMap: switch to a stable loading page before
    // replacing a year's cells.  This prevents a transparent-widget repaint
    // from exposing the native black backing store for one frame.
    m_heatmapContent = new QStackedWidget(heatmapPanel);
    m_heatmapContent->setFixedSize(750, 155);
    m_heatmapPlaceholder = new DesignLabel(QStringLiteral("加载中..."), m_heatmapContent);
    m_heatmapPlaceholder->setObjectName(QStringLiteral("PersonalRecordHeatmapPlaceholder"));
    m_heatmapPlaceholder->setAlignment(Qt::AlignCenter);
    m_heatmapPlaceholder->setFixedSize(750, 155);
    m_heatmap = new CalendarHeatmap(currentYear, {}, &m_themeService, m_heatmapContent);
    m_heatmap->setObjectName(QStringLiteral("PersonalRecordHeatmap"));
    m_heatmapContent->addWidget(m_heatmapPlaceholder);
    m_heatmapContent->addWidget(m_heatmap);
    m_heatmapContent->setCurrentWidget(m_heatmapPlaceholder);
    heatmapColumn->addWidget(m_heatmapContent);
    panelLayout->addLayout(heatmapColumn);

    auto *yearList = new QScrollArea(heatmapPanel);
    yearList->setObjectName(QStringLiteral("RecordYearButtonList"));
    yearList->setWidgetResizable(true);
    yearList->setFrameShape(QFrame::NoFrame);
    // Python's ButtonList keeps its 100px buttons plus layout margins and
    // hides the scrollbars instead of shrinking the viewport around them.
    yearList->setFixedSize(118, 200);
    yearList->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    yearList->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    auto *yearContainer = new QWidget(yearList);
    auto *yearLayout = new QVBoxLayout(yearContainer);
    auto *yearGroup = new QButtonGroup(yearContainer);
    yearGroup->setExclusive(true);
    for (int year = currentYear; year >= earliestYear; --year)
    {
        auto *button = new DesignButton(QString::number(year), yearContainer);
        button->setCheckable(true);
        button->setFixedSize(100, 40);
        button->setChecked(year == currentYear);
        yearGroup->addButton(button, year);
        yearLayout->addWidget(button);
    }
    yearLayout->addStretch();
    yearList->setWidget(yearContainer);
    panelLayout->addWidget(yearList);
    root->addWidget(heatmapPanel, 0, Qt::AlignHCenter);
    root->addStretch();

    connect(previousKind, &QPushButton::clicked, this, [this] {
        m_recordKindIndex = (m_recordKindIndex + 2) % 3;
        refresh();
    });
    connect(nextKind, &QPushButton::clicked, this, [this] {
        m_recordKindIndex = (m_recordKindIndex + 1) % 3;
        refresh();
    });
    connect(yearGroup, &QButtonGroup::idClicked, this, [this](int year) {
        changeYear(year);
    });
    refresh();
}

void RecordHeatmapPage::refresh()
{
    if (!m_heatmap)
        return;

    // Python's SwitchHeapMap keeps its loading page visible while the initial
    // data workers run.  Do the same before the first C++ render: otherwise
    // the transparent heatmap can be shown before its first paint completes.
    if (m_heatmapInitialLoadPending)
    {
        m_heatmapInitialLoadPending = false;
        m_heatmapTitle->setText(QStringLiteral("加载中..."));
        m_heatmapContent->setCurrentWidget(m_heatmapPlaceholder);
        QTimer::singleShot(0, this, [this] { refresh(); });
        return;
    }

    const auto kind = static_cast<PersonalRecordKind>(m_recordKindIndex);
    PrivateRepository repository(m_privateDatabase);
    const QMap<QDate, int> counts = repository.dailyCounts(m_currentYear, kind);
    QHash<QDate, int> hash;
    for (auto iterator = counts.cbegin(); iterator != counts.cend(); ++iterator)
    {
        hash.insert(iterator.key(), iterator.value());
    }
    int total = 0;
    for (const int count : counts)
    {
        total += count;
    }
    const QStringList names{QStringLiteral("撸管"), QStringLiteral("做爱"),
                            QStringLiteral("晨勃")};
    m_heatmapTitle->setText(QStringLiteral("%1%2次在当年中")
                                .arg(names.at(m_recordKindIndex))
                                .arg(total));
    m_heatmap->updateData(m_currentYear, hash);
    m_heatmapContent->setCurrentWidget(m_heatmap);
}

void RecordHeatmapPage::changeYear(int year)
{
    if (year == m_currentYear) return;

    m_currentYear = year;
    // Yield once so the loading page is painted before the database lookup and
    // heatmap redraw.  Python does this naturally while its workers load data.
    m_heatmapTitle->setText(QStringLiteral("加载中..."));
    m_heatmapContent->setCurrentWidget(m_heatmapPlaceholder);
    QTimer::singleShot(0, this, [this, year] {
        // A second year click may arrive while this callback is queued.
        if (year == m_currentYear) refresh();
    });
}

} // namespace darkeye

#include "PersonalDataPanels.moc"
