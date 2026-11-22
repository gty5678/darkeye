#pragma once

#include "graph/GraphViewSession.h"

#include <QColor>

class ForceViewRhiWidget;

namespace darkeye::graph_view {

struct GraphViewStyle
{
    QColor actressColor = QColor(QStringLiteral("#ff99cc"));
    QColor workColor = QColor(QStringLiteral("#99ccff"));
    QColor centerColor = QColor(QStringLiteral("#ffd700"));
    QColor defaultColor = QColor(QStringLiteral("#5c5c5c"));
    QString centerId;
};

/** Converts graph-core snapshots and deltas to the QRhi widget's compact API. */
class GraphViewPresenter
{
public:
    explicit GraphViewPresenter(ForceViewRhiWidget &view);

    void load(const graph::GraphViewSnapshot &snapshot,
              const GraphViewStyle &style = {});
    void apply(const graph::GraphDelta &delta,
               const GraphViewStyle &style = {});
    void updateColors(const graph::GraphViewSnapshot &snapshot,
                      const GraphViewStyle &style);

private:
    [[nodiscard]] QColor colorFor(const graph::GraphNode &node,
                                  const GraphViewStyle &style) const;

    ForceViewRhiWidget &m_view;
};

} // namespace darkeye::graph_view
