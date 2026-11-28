#pragma once

#include "darkeye_ui/base/LazyWidget.h"

#include <QSet>

class QShowEvent;
class QResizeEvent;
class QLabel;

namespace darkeye {
class ThemeService;
class StateToggleButton;
class TokenCheckBox;
class ForceViewSettingsPanel;
namespace graph {
class GraphManager;
}
namespace graph_view {
class GraphViewWidget;
}
namespace settings {
class Paths;
}

/** Python ForceDirectPage equivalent: page-only controls around GraphViewWidget. */
class ForceDirectPage final : public LazyWidget
{
    Q_OBJECT

public:
    explicit ForceDirectPage(ThemeService &themeService, graph::GraphManager &graphManager,
                             QWidget *parent = nullptr, const settings::Paths *paths = nullptr,
                             bool localGraph = false);
    void refreshGraph();
    void setEgoGraph(const QString &centerId, int radius = -1);
    void showFilteredWorks(QSet<qint64> workIds);
    void showFilteredActresses(const QSet<qint64> &actressIds);
    void showEmptyGraph();

signals:
    void workRequested(qint64 workId);
    void actressRequested(qint64 actressId);

private:
    void lazyLoad() override;
    void showEvent(QShowEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void applyTheme();
    void updateOverlayGeometry();

    ThemeService &m_themeService;
    graph::GraphManager &m_graphManager;
    graph_view::GraphViewWidget *m_graphView = nullptr;
    QLabel *m_loadingOverlay = nullptr;
    TokenCheckBox *m_favoriteOnly = nullptr;
    StateToggleButton *m_settingsButton = nullptr;
    ForceViewSettingsPanel *m_settingsPanel = nullptr;
    int m_egoRadius = 3;
    bool m_needsRefresh = false;
};

} // namespace darkeye
