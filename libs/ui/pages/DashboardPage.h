#pragma once

#include "darkeye_ui/base/LazyWidget.h"

#include <QSqlDatabase>

class QListWidget;

namespace darkeye
{

class StatsOverviewCards;

class DashboardPage final : public LazyWidget
{
    Q_OBJECT

public:
    DashboardPage(QSqlDatabase publicDatabase, QSqlDatabase privateDatabase,
                  QWidget *parent = nullptr);

    void refresh();

private:
    void lazyLoad() override;
    void refreshActivity();

    QSqlDatabase m_publicDatabase;
    QSqlDatabase m_privateDatabase;
    StatsOverviewCards *m_overview = nullptr;
    QListWidget *m_recentView = nullptr;
    QListWidget *m_recentAdded = nullptr;
    QListWidget *m_pending = nullptr;
};

} // namespace darkeye
