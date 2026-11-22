#pragma once

#include "darkeye_ui/base/LazyWidget.h"

#include <QSqlDatabase>

namespace darkeye
{

class ThemeService;

class StatisticsChartPage final : public LazyWidget
{
    Q_OBJECT

public:
    explicit StatisticsChartPage(QSqlDatabase publicDatabase, QSqlDatabase privateDatabase,
                                 ThemeService &themeService,
                                 QWidget *parent = nullptr);

    [[nodiscard]] QWidget *captureContent();

private:
    void lazyLoad() override;

    QSqlDatabase m_publicDatabase;
    QSqlDatabase m_privateDatabase;
    ThemeService &m_themeService;
    QWidget *m_chart = nullptr;
};

} // namespace darkeye
