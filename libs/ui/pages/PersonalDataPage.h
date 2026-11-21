#pragma once

#include "darkeye_ui/base/LazyWidget.h"

#include <QList>
#include <QSqlDatabase>

class QLabel;
class QStackedWidget;

namespace darkeye
{

class CalendarHeatmap;
class StatsOverviewCards;
class ThemeService;
class TopActressCard;

class PersonalDataPage final : public LazyWidget
{
    Q_OBJECT

public:
    PersonalDataPage(QSqlDatabase publicDatabase, QSqlDatabase privateDatabase,
                     ThemeService &themeService, QString actressImageDirectory,
                     QWidget *parent = nullptr);

    void refresh();

signals:
    void actressDetailRequested(qint64 actressId);
    void actressEditRequested(qint64 actressId);

private:
    void lazyLoad() override;
    void refreshHeatmap();
    void changeYear(int year);

    QSqlDatabase m_publicDatabase;
    QSqlDatabase m_privateDatabase;
    ThemeService &m_themeService;
    QString m_actressImageDirectory;
    StatsOverviewCards *m_overview = nullptr;
    QList<TopActressCard *> m_topActressCards;
    QLabel *m_salesCycle = nullptr;
    int m_currentYear = 0;
    int m_recordKindIndex = 0;
    bool m_heatmapInitialLoadPending = true;
    QLabel *m_heatmapTitle = nullptr;
    QStackedWidget *m_heatmapContent = nullptr;
    QLabel *m_heatmapPlaceholder = nullptr;
    CalendarHeatmap *m_heatmap = nullptr;
};

} // namespace darkeye
