#pragma once

#include "darkeye_ui/base/LazyWidget.h"

#include <QSqlDatabase>

namespace darkeye
{

class PersonalDataPage;
class PlotTabPage;
class ThemeService;

class StatisticsPage final : public LazyWidget
{
    Q_OBJECT

public:
    enum class Panel { Plots, Information };

    StatisticsPage(QSqlDatabase publicDatabase, QSqlDatabase privateDatabase,
                   ThemeService &themeService, QString actressImageDirectory,
                   QWidget *parent = nullptr, Panel panel = Panel::Plots);

    void refresh();
    [[nodiscard]] QWidget *captureContent();

signals:
    void actressDetailRequested(qint64 actressId);
    void actressEditRequested(qint64 actressId);

private:
    void lazyLoad() override;

    Panel m_panel;
    QSqlDatabase m_publicDatabase;
    QSqlDatabase m_privateDatabase;
    ThemeService &m_themeService;
    QString m_actressImageDirectory;
    PersonalDataPage *m_personalData = nullptr;
    PlotTabPage *m_plots = nullptr;
};

} // namespace darkeye
