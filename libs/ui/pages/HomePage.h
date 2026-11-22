#pragma once

#include "darkeye_ui/base/LazyWidget.h"
#include "domain/Work.h"
#include <QSqlDatabase>

namespace darkeye {

class ThemeService;
class ShopSceneWidget;

class HomePage final : public LazyWidget
{
    Q_OBJECT

public:
    explicit HomePage(QSqlDatabase database, const QString &coverDirectory, ThemeService &themes,
                      QWidget *parent = nullptr);

signals:
    void detailRequested(qint64 workId);

private slots:
    void openCover(int index);

private:
    void lazyLoad() override;
    void refresh();
    void loadCovers();
    void updateOutlineColor();
    ThemeService &m_themes;
    QSqlDatabase m_database;
    QString m_coverDirectory;
    WorkSearch m_search;
    QList<WorkSummary> m_works;
    ShopSceneWidget *m_view = nullptr;
};

} // namespace darkeye
