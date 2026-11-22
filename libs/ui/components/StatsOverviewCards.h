#pragma once

#include <QList>
#include <QSqlDatabase>
#include <QWidget>

class QLabel;

namespace darkeye
{

class StatsOverviewCards final : public QWidget
{
public:
    enum class Layout { SingleRow, Wrapping };

    StatsOverviewCards(QSqlDatabase publicDatabase, QSqlDatabase privateDatabase,
                       QWidget *parent = nullptr, Layout cardLayout = Layout::SingleRow);

    void refresh();

private:
    QSqlDatabase m_publicDatabase;
    QSqlDatabase m_privateDatabase;
    QList<QLabel *> m_valueLabels;
};

} // namespace darkeye
