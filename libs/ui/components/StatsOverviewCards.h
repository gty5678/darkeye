#pragma once

#include <QList>
#include <QSqlDatabase>
#include <QWidget>

class QLabel;

namespace darkeye
{

class ThemeService;

class StatsOverviewCards final : public QWidget
{
public:
    StatsOverviewCards(QSqlDatabase publicDatabase, QSqlDatabase privateDatabase,
                       ThemeService &themeService, QWidget *parent = nullptr);

    void refresh();

private:
    QSqlDatabase m_publicDatabase;
    QSqlDatabase m_privateDatabase;
    QList<QLabel *> m_valueLabels;
};

} // namespace darkeye
