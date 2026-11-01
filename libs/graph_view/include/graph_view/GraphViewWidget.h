#pragma once

#include "graph/GraphViewSession.h"
#include "graph_view/GraphViewPresenter.h"

#include <QWidget>

class QLabel;
class QTimer;

class ForceViewRhiWidget;
class QShowEvent;

namespace darkeye::graph {
class GraphManager;
}

namespace darkeye::graph_view {

/** Presentation adapter: graph manager/session in, QRhi graph view out. */
class GraphViewWidget final : public QWidget
{
    Q_OBJECT

public:
    explicit GraphViewWidget(graph::GraphManager &manager, QWidget *parent = nullptr);

    void setFavoriteOnly(bool enabled);
    /** Prepare CPU data, simulation and font atlas while hidden when requested. */
    void setEgoGraph(const QString &centerId, int radius = 1, bool prepareWhileHidden = false);
    void showEgoGraph(int radius);
    void showAll();
    void showEmpty();
    void setEgoRadius(int radius);
    void setNodeColor(const QString &group, const QColor &color);
    /** Enable or disable the image overlay shown while hovering a graph node. */
    void setImageOverlayEnabled(bool enabled);
    /** Override the default image directories inferred from the public database path. */
    void setImageDirectories(QString actressDirectory, QString workCoverDirectory);
    void refreshGraph();
    [[nodiscard]] ForceViewRhiWidget *view() const noexcept;

signals:
    void nodeLeftClicked(const QString &nodeId);
    void loadFailed(const QString &errorMessage);

private:
    void showEvent(QShowEvent *event) override;
    void reloadView();
    void applyManagerChange();
    void loadVisibleSnapshot();
    void showNodeImage(const QString &nodeId, float radius, bool dragging);
    void updateNodeImagePosition();
    void hideNodeImage();
    [[nodiscard]] QString imageFilePath(const QString &nodeId) const;

    graph::GraphManager &m_manager;
    graph::GraphViewSession m_session;
    ForceViewRhiWidget *m_view = nullptr;
    GraphViewPresenter m_presenter;
    GraphViewStyle m_style;
    int m_egoRadius = 2;
    bool m_loaded = false;
    bool m_pendingVisibleLoad = false;
    bool m_pendingManagerChange = false;
    bool m_prepareWhileHidden = false;
    bool m_imageOverlayEnabled = true;
    QString m_actressImageDirectory;
    QString m_workCoverDirectory;
    QString m_hoveredNodeId;
    float m_hoveredNodeRadius = 0.0F;
    QLabel *m_nodeImage = nullptr;
    QTimer *m_nodeImagePositionTimer = nullptr;
};

} // namespace darkeye::graph_view
