#include "ui/pages/DashboardPage.h"
#include "ui/pages/PageScrollArea.h"

#include "database/repositories/StatisticsRepository.h"
#include "darkeye_ui/components/DesignLabel.h"
#include "ui/components/StatsOverviewCards.h"

#include <QHBoxLayout>
#include <QListWidget>
#include <QVBoxLayout>

#include <utility>

namespace
{

void updateList(QListWidget *list, const QStringList &items)
{
    list->clear();
    list->addItems(items);
    int height = 2 * list->frameWidth();
    for (int row = 0; row < list->count(); ++row)
        height += list->sizeHintForRow(row);
    list->setFixedHeight(qMin(height, 200));
}

QStringList activityLabels(
    const std::optional<QVector<darkeye::DashboardActivity>> &activities, const QString &emptyText,
    bool showKind)
{
    if (!activities)
        return {QStringLiteral("数据加载失败")};
    if (activities->isEmpty())
        return {emptyText};

    QStringList labels;
    for (const auto &activity : *activities)
    {
        QString name = activity.name;
        if (showKind)
        {
            const QString kind = activity.kind == darkeye::DashboardActivity::Kind::Work
                                     ? QStringLiteral("作品")
                                     : QStringLiteral("女优");
            name = QStringLiteral("%1 · %2").arg(kind, name);
        }
        labels.append(QStringLiteral("%1    %2").arg(activity.time.left(16), name));
    }
    return labels;
}

} // namespace

namespace darkeye
{

DashboardPage::DashboardPage(QSqlDatabase publicDatabase, QSqlDatabase privateDatabase,
                             QWidget *parent)
    : LazyWidget(parent), m_publicDatabase(std::move(publicDatabase)),
      m_privateDatabase(std::move(privateDatabase))
{
}

void DashboardPage::lazyLoad()
{
    auto *frame = addPageScrollArea(this);
    auto *frameLayout = new QHBoxLayout(frame);
    frameLayout->setContentsMargins(0, 0, 0, 0);
    frameLayout->setSpacing(0);

    auto *content = new QWidget(frame);
    content->setObjectName(QStringLiteral("DashboardContent"));
    content->setMaximumWidth(1000);
    frameLayout->addStretch();
    frameLayout->addWidget(content, 1);
    frameLayout->addStretch();

    auto *root = new QVBoxLayout(content);
    root->setContentsMargins(16, 16, 16, 16);
    root->setSpacing(16);
    m_overview = new StatsOverviewCards(m_publicDatabase, m_privateDatabase, content,
                                        StatsOverviewCards::Layout::Wrapping);
    root->addWidget(m_overview);

    const auto addSection = [root, content](const QString &objectName, const QString &title)
    {
        auto *layout = new QVBoxLayout;
        layout->setSpacing(6);
        auto *heading = new DesignLabel(title, content);
        heading->setObjectName(QStringLiteral("dashboard_section_title"));
        layout->addWidget(heading);
        auto *list = new QListWidget(content);
        list->setObjectName(objectName);
        list->setSelectionMode(QAbstractItemView::NoSelection);
        list->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        layout->addWidget(list);
        root->addLayout(layout);
        return list;
    };
    m_recentView = addSection(QStringLiteral("DashboardRecentViewList"),
                              QStringLiteral("最近观看"));
    m_recentAdded = addSection(QStringLiteral("DashboardRecentAddedList"),
                               QStringLiteral("最近新增"));
    m_pending = addSection(QStringLiteral("DashboardPendingList"), QStringLiteral("待处理事项"));
    root->addStretch();
    refreshActivity();
}

void DashboardPage::refresh()
{
    if (!isInitialized())
    {
        initialize();
        return;
    }
    m_overview->refresh();
    refreshActivity();
}

void DashboardPage::refreshActivity()
{
    StatisticsRepository repository(m_publicDatabase, m_privateDatabase);
    updateList(m_recentView, activityLabels(repository.recentWatching(),
                                           QStringLiteral("暂无观看记录"), false));
    updateList(m_recentAdded, activityLabels(repository.recentAdditions(),
                                            QStringLiteral("暂无新增记录"), true));

    const auto pending = repository.dashboardPendingCounts();
    if (!pending)
    {
        updateList(m_pending, {QStringLiteral("数据加载失败")});
        return;
    }
    updateList(m_pending, {QStringLiteral("%1 部作品没有封面").arg(pending->missingCover),
                           QStringLiteral("%1 部作品未绑定女优").arg(pending->unlinkedActress)});
}

} // namespace darkeye
