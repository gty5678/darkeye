#include "ui/components/StatsOverviewCards.h"

#include "database/repositories/StatisticsRepository.h"
#include "darkeye_ui/components/DesignLabel.h"
#include "darkeye_ui/theme/ChamferPath.h"
#include "darkeye_ui/theme/ThemeService.h"

#include <QPainter>
#include <QHBoxLayout>
#include <QVBoxLayout>

namespace darkeye
{

namespace
{
class StatisticCard final : public QWidget
{
public:
    StatisticCard(ThemeService &themeService, QWidget *parent)
        : QWidget(parent), m_themeService(themeService)
    {
        setObjectName(QStringLiteral("StatisticsOverviewCard"));
        setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
        setMaximumWidth(148);
        connect(&m_themeService, &ThemeService::themeChanged, this, [this] { update(); });
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        const auto tokens = m_themeService.currentTokens();
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(QPen(QColor(tokens.border), 1));
        painter.setBrush(QColor(tokens.background));
        painter.drawPath(chamferPath(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), 10));
    }

private:
    ThemeService &m_themeService;
};
} // namespace

StatsOverviewCards::StatsOverviewCards(QSqlDatabase publicDatabase,
                                       QSqlDatabase privateDatabase, ThemeService &themeService,
                                       QWidget *parent)
    : QWidget(parent), m_publicDatabase(std::move(publicDatabase)),
      m_privateDatabase(std::move(privateDatabase))
{
    setObjectName(QStringLiteral("StatsOverviewCards"));
    auto *cards = new QHBoxLayout(this);
    cards->setContentsMargins(0, 0, 0, 0);
    cards->setSpacing(8);
    cards->addStretch();

    const QStringList labels{QStringLiteral("作品总数"), QStringLiteral("女优总数"),
                             QStringLiteral("男优总数"), QStringLiteral("Tag总数"),
                             QStringLiteral("近30天新增作品"), QStringLiteral("收藏作品总数"),
                             QStringLiteral("收藏女优总数")};
    for (qsizetype index = 0; index < labels.size(); ++index)
    {
        auto *card = new StatisticCard(themeService, this);
        auto *cardLayout = new QVBoxLayout(card);
        cardLayout->setContentsMargins(12, 8, 12, 8);
        cardLayout->setSpacing(2);
        auto *value = new DesignLabel(QStringLiteral("--"), card);
        value->setObjectName(QStringLiteral("StatisticValue_%1").arg(index));
        value->setProperty("statisticValue", true);
        auto *description = new DesignLabel(labels.at(index), card);
        cardLayout->addWidget(value);
        cardLayout->addWidget(description);
        cards->addWidget(card, 1);
        m_valueLabels.append(value);
    }
    cards->addStretch();
    refresh();
}

void StatsOverviewCards::refresh()
{
    StatisticsRepository repository(m_publicDatabase, m_privateDatabase);
    const std::optional<OverviewStatistics> statistics = repository.overview();
    if (!statistics.has_value())
    {
        return;
    }
    const QList<int> values{statistics->workCount,
                            statistics->actressCount,
                            statistics->actorCount,
                            statistics->tagCount,
                            statistics->recentThirtyDays,
                            statistics->favoriteWorkCount,
                            statistics->favoriteActressCount};
    for (qsizetype index = 0; index < m_valueLabels.size(); ++index)
    {
        m_valueLabels.at(index)->setText(QString::number(values.at(index)));
    }
}

} // namespace darkeye
