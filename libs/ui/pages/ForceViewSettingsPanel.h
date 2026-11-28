#pragma once

#include <QScrollArea>

class QFormLayout;
class QResizeEvent;
class QSettings;
class QSlider;

namespace darkeye {

class ThemeService;
namespace graph_view { class GraphViewWidget; }

/** Floating controls for the force-directed graph, matching the Python view's
 * effect and display sections. */
class ForceViewSettingsPanel final : public QScrollArea
{
    Q_OBJECT

public:
    explicit ForceViewSettingsPanel(ThemeService &themeService, graph_view::GraphViewWidget &graph,
                                    QSettings *settings, QWidget *parent = nullptr);
    [[nodiscard]] int preferredHeight() const;
    [[nodiscard]] int graphNeighborDepth() const;
    void setGraphNeighborDepthVisible(bool visible);

signals:
    void graphNeighborDepthChanged(int value);
    void contentSizeChanged();

private:
    void resizeEvent(QResizeEvent *event) override;
    void updateContentWidth();
    QFormLayout *m_displayForm = nullptr;
    QSlider *m_graphNeighborDepth = nullptr;
};

} // namespace darkeye
