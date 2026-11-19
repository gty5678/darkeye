#include "ui/components/StatsOverviewCards.h"

#include "database/repositories/StatisticsRepository.h"
#include "darkeye_ui/components/DesignLabel.h"
#include "darkeye_ui/layouts/FlowLayout.h"

#include <QFrame>
#include <QHBoxLayout>
#include <QVBoxLayout>

namespace darkeye
{

StatsOverviewCards::StatsOverviewCards(QSqlDatabase publicDatabase,
                                       QSqlDatabase privateDatabase, QWidget *parent,
                                       Layout cardArrangement)
    : QWidget(parent), m_publicDatabase(std::move(publicDatabase)),
      m_privateDatabase(std::move(privateDatabase))
{
    setObjectName(QStringLiteral("StatsOverviewCards"));
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 4, 0, 4);
    layout->setSpacing(4);
    auto *row = cardArrangement == Layout::SingleRow ? new QHBoxLayout : nullptr;
    QLayout *cards = row ? static_cast<QLayout *>(row) : new FlowLayout(nullptr, 0, 8);
    if (row)
    {
        row->setSpacing(8);
        row->addStretch();
    }

    const QStringList labels{QStringLiteral("作品总数"), QStringLiteral("女优总数"),
                             QStringLiteral("男优总数"), QStringLiteral("Tag总数"),
                             QStringLiteral("近30天新增作品"), QStringLiteral("收藏作品总数"),
                             QStringLiteral("收藏女优总数")};
    for (qsizetype index = 0; index < labels.size(); ++index)
    {
        auto *card = new QFrame(this);
        card->setObjectName(QStringLiteral("StatisticsOverviewCard"));
        card->setFrameShape(QFrame::StyledPanel);
        card->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
        auto *cardLayout = new QVBoxLayout(card);
        cardLayout->setContentsMargins(12, 8, 12, 8);
        cardLayout->setSpacing(4);
        auto *value = new DesignLabel(QStringLiteral("--"), card);
        value->setObjectName(QStringLiteral("StatisticValue_%1").arg(index));
        QFont valueFont = value->font();
        valueFont.setPixelSize(20);
        valueFont.setBold(true);
        value->setFont(valueFont);
        auto *description = new DesignLabel(labels.at(index), card);
        description->setProperty("muted", true);
        cardLayout->addWidget(value);
        cardLayout->addWidget(description);
        cards->addWidget(card);
        m_valueLabels.append(value);
    }
    if (row)
        row->addStretch();
    layout->addLayout(cards);
    refresh();
}

void StatsOverviewCards::refresh()
{
    StatisticsRepository repository(m_publicDatabase, m_privateDatabase);
    const std::optional<DashboardStatistics> statistics = repository.dashboard();
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
