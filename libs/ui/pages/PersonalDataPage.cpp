#include "ui/pages/PersonalDataPage.h"

#include "database/repositories/PrivateRepository.h"
#include "database/repositories/StatisticsRepository.h"
#include "darkeye_ui/components/Charts.h"
#include "darkeye_ui/components/DesignButton.h"
#include "darkeye_ui/components/DesignLabel.h"
#include "darkeye_ui/components/IconButton.h"
#include "darkeye_ui/components/SoftShadow.h"
#include "ui/components/PersonCard.h"
#include "ui/components/StatsOverviewCards.h"
#include "darkeye_ui/theme/ThemeService.h"

#include <QButtonGroup>
#include <QDate>
#include <QDir>
#include <QFrame>
#include <QEvent>
#include <QHBoxLayout>
#include <QPainter>
#include <QPainterPath>
#include <QPointer>
#include <QScrollArea>
#include <QScrollBar>
#include <QStackedWidget>
#include <QTimer>
#include <QThreadPool>
#include <QVBoxLayout>

namespace darkeye
{

class OctagonCard : public QWidget
{
public:
    OctagonCard(ThemeService &themeService, const QMargins &margins, bool drawBorder = true,
                QWidget *parent = nullptr)
        : QWidget(parent), m_themeService(themeService), m_drawBorder(drawBorder)
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
        if (m_drawBorder)
            painter.setPen(QPen(QColor(tokens.border), 1));
        else
            painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(tokens.background));
        painter.drawPath(cardPath());
    }

private:
    QPainterPath cardPath() const
    {
        constexpr int chamfer = 20;
        const QRectF rectangle = QRectF(rect()).adjusted(kShadowMargin, kShadowMargin,
                                                        -kShadowMargin, -kShadowMargin);
        QPainterPath path;
        path.moveTo(rectangle.left() + chamfer, rectangle.top());
        path.lineTo(rectangle.right() - chamfer, rectangle.top());
        path.lineTo(rectangle.right(), rectangle.top() + chamfer);
        path.lineTo(rectangle.right(), rectangle.bottom() - chamfer);
        path.lineTo(rectangle.right() - chamfer, rectangle.bottom());
        path.lineTo(rectangle.left() + chamfer, rectangle.bottom());
        path.lineTo(rectangle.left(), rectangle.bottom() - chamfer);
        path.lineTo(rectangle.left(), rectangle.top() + chamfer);
        path.closeSubpath();

        return path;
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
    bool m_drawBorder = true;
    QVBoxLayout *m_layout = nullptr;
    QPixmap m_shadow;
    quint64 m_shadowRequest = 0;
};

class TopActressCard final : public OctagonCard
{
    Q_OBJECT

public:
    TopActressCard(int days, QString imageDirectory, ThemeService &themeService,
                   QWidget *parent = nullptr)
        : OctagonCard(themeService, QMargins(10, 0, 10, 10), false, parent),
          m_imageDirectory(std::move(imageDirectory))
    {
        auto *title = new DesignLabel(QStringLiteral("过去%1天最喜欢的女优").arg(days), this);
        title->setAlignment(Qt::AlignCenter);
        m_personCard = new PersonCard(0, QStringLiteral("加载中..."), {},
                                      m_imageDirectory, this);
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
    QString m_imageDirectory;
    PersonCard *m_personCard = nullptr;
};

PersonalDataPage::PersonalDataPage(QSqlDatabase publicDatabase, QSqlDatabase privateDatabase,
                                   ThemeService &themeService, QString actressImageDirectory,
                                   QWidget *parent)
    : LazyWidget(parent), m_publicDatabase(std::move(publicDatabase)),
      m_privateDatabase(std::move(privateDatabase)), m_themeService(themeService),
      m_actressImageDirectory(std::move(actressImageDirectory))
{
}

void PersonalDataPage::lazyLoad()
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(0, 10, 0, 0);

    m_overview = new StatsOverviewCards(m_publicDatabase, m_privateDatabase, this);
    root->addWidget(m_overview);

    auto *summary = new QHBoxLayout;
    summary->setContentsMargins(0, 0, 0, 0);
    summary->setSpacing(6);
    summary->addStretch();
    for (const int days : {30, 180, 365})
    {
        auto *card = new TopActressCard(days, m_actressImageDirectory,
                                        m_themeService, this);
        card->setProperty("days", days);
        connect(card, &TopActressCard::actressDetailRequested, this,
                &PersonalDataPage::actressDetailRequested);
        connect(card, &TopActressCard::actressEditRequested, this,
                &PersonalDataPage::actressEditRequested);
        m_topActressCards.append(card);
        summary->addWidget(card);
    }
    auto *cycleCard = new OctagonCard(m_themeService, QMargins(), false, this);
    auto *cycleLayout = cycleCard->contentLayout();
    auto *cycleTitle =
        new DesignLabel(QStringLiteral("收藏作品中未观看去化周期"), cycleCard);
    cycleTitle->setAlignment(Qt::AlignCenter);
    m_salesCycle = new DesignLabel(QStringLiteral("加载中..."), cycleCard);
    m_salesCycle->setAlignment(Qt::AlignCenter);
    m_salesCycle->setStyleSheet(QStringLiteral("font-size: 30pt; color: #999999;"));
    cycleLayout->addWidget(cycleTitle);
    cycleLayout->addWidget(m_salesCycle);
    summary->addWidget(cycleCard);
    summary->addStretch();
    root->addLayout(summary);

    // Keep the heatmap controls inside one centered panel, as in the Python UI:
    // title and record-type arrows on the left, a vertically scrollable year list on the right.
    auto *heatmapPanel = new QWidget(this);
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
    root->addWidget(heatmapPanel, 0, Qt::AlignCenter);
    root->addStretch();

    connect(previousKind, &QPushButton::clicked, this, [this] {
        m_recordKindIndex = (m_recordKindIndex + 2) % 3;
        refreshHeatmap();
    });
    connect(nextKind, &QPushButton::clicked, this, [this] {
        m_recordKindIndex = (m_recordKindIndex + 1) % 3;
        refreshHeatmap();
    });
    connect(yearGroup, &QButtonGroup::idClicked, this, [this](int year) {
        changeYear(year);
    });
    refresh();
}

void PersonalDataPage::refresh()
{
    initialize();
    m_overview->refresh();
    StatisticsRepository statistics(m_publicDatabase, m_privateDatabase);
    for (TopActressCard *card : m_topActressCards)
    {
        card->setStatistic(statistics.topActress(card->property("days").toInt()));
    }
    const auto cycle = statistics.favoriteUnwatchedSalesCycle();
    m_salesCycle->setText(cycle ? QStringLiteral("%1天").arg(*cycle)
                               : QStringLiteral("数据不足"));
    m_salesCycle->setStyleSheet(cycle && *cycle > 30
                                    ? QStringLiteral("font-size: 30pt; color: #FF0000;")
                                    : QStringLiteral("font-size: 30pt;"));
    refreshHeatmap();
}

void PersonalDataPage::refreshHeatmap()
{
    // Python's SwitchHeapMap keeps its loading page visible while the initial
    // data workers run.  Do the same before the first C++ render: otherwise
    // the transparent heatmap can be shown before its first paint completes.
    if (m_heatmapInitialLoadPending)
    {
        m_heatmapInitialLoadPending = false;
        m_heatmapTitle->setText(QStringLiteral("加载中..."));
        m_heatmapContent->setCurrentWidget(m_heatmapPlaceholder);
        QTimer::singleShot(0, this, [this] { refreshHeatmap(); });
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

void PersonalDataPage::changeYear(int year)
{
    if (year == m_currentYear) return;

    m_currentYear = year;
    // Yield once so the loading page is painted before the database lookup and
    // heatmap redraw.  Python does this naturally while its workers load data.
    m_heatmapTitle->setText(QStringLiteral("加载中..."));
    m_heatmapContent->setCurrentWidget(m_heatmapPlaceholder);
    QTimer::singleShot(0, this, [this, year] {
        // A second year click may arrive while this callback is queued.
        if (year == m_currentYear) refreshHeatmap();
    });
}

} // namespace darkeye

#include "PersonalDataPage.moc"
