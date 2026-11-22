#include "GraphViewPresenter.h"

#include "ForceViewRhiWidget.h"

#include <QHash>
#include <QVariantMap>

namespace darkeye::graph_view {

GraphViewPresenter::GraphViewPresenter(ForceViewRhiWidget &view)
    : m_view(view)
{
}

void GraphViewPresenter::load(const graph::GraphViewSnapshot &snapshot,
                              const GraphViewStyle &style)
{
    QHash<QString, int> indexById;
    QVector<float> radii;
    QVector<QColor> colors;
    QStringList ids;
    QStringList labels;
    radii.reserve(snapshot.nodes.size());
    colors.reserve(snapshot.nodes.size());
    ids.reserve(snapshot.nodes.size());
    labels.reserve(snapshot.nodes.size());

    for (int index = 0; index < snapshot.nodes.size(); ++index) {
        const graph::GraphViewNode &node = snapshot.nodes[index];
        indexById.insert(node.node.id, index);
        ids.append(node.node.id);
        labels.append(node.node.label);
        radii.append(node.radius);
        colors.append(colorFor(node.node, style));
    }

    QVector<int> edges;
    edges.reserve(snapshot.edges.size() * 2);
    for (const graph::GraphEdge &edge : snapshot.edges) {
        const auto source = indexById.constFind(edge.sourceId);
        const auto target = indexById.constFind(edge.targetId);
        if (source != indexById.cend() && target != indexById.cend()) {
            edges.append(*source);
            edges.append(*target);
        }
    }
    m_view.setGraph(snapshot.nodes.size(), edges, {}, ids, labels, radii, colors);
}

void GraphViewPresenter::apply(const graph::GraphDelta &delta,
                               const GraphViewStyle &style)
{
    QVariantList operations;
    operations.reserve(delta.size());
    for (const graph::GraphDeltaOperation &operation : delta) {
        QVariantMap map;
        switch (operation.type) {
        case graph::GraphDeltaOperation::Type::AddNode:
        case graph::GraphDeltaOperation::Type::UpdateNode: {
            map.insert(QStringLiteral("op"),
                       operation.type == graph::GraphDeltaOperation::Type::AddNode
                           ? QStringLiteral("add_node")
                           : QStringLiteral("update_node"));
            map.insert(QStringLiteral("id"), operation.node.node.id);
            QVariantMap attributes;
            attributes.insert(QStringLiteral("label"), operation.node.node.label);
            attributes.insert(QStringLiteral("radius"), operation.node.radius);
            attributes.insert(QStringLiteral("color"),
                              colorFor(operation.node.node, style));
            map.insert(QStringLiteral("attr"), attributes);
            break;
        }
        case graph::GraphDeltaOperation::Type::RemoveNode:
            map.insert(QStringLiteral("op"), QStringLiteral("del_node"));
            map.insert(QStringLiteral("id"), operation.node.node.id);
            break;
        case graph::GraphDeltaOperation::Type::AddEdge:
        case graph::GraphDeltaOperation::Type::UpdateEdge:
        case graph::GraphDeltaOperation::Type::RemoveEdge:
            map.insert(QStringLiteral("op"),
                       operation.type == graph::GraphDeltaOperation::Type::AddEdge
                           ? QStringLiteral("add_edge")
                           : operation.type == graph::GraphDeltaOperation::Type::UpdateEdge
                               ? QStringLiteral("update_edge")
                               : QStringLiteral("del_edge"));
            map.insert(QStringLiteral("u"), operation.edge.sourceId);
            map.insert(QStringLiteral("v"), operation.edge.targetId);
            map.insert(QStringLiteral("attr"),
                       QVariantMap{{QStringLiteral("type"), operation.edge.type}});
            break;
        }
        operations.append(map);
    }
    m_view.apply_diff_runtime(operations);
}

void GraphViewPresenter::updateColors(const graph::GraphViewSnapshot &snapshot,
                                      const GraphViewStyle &style)
{
    QHash<QString, QColor> colorsById;
    colorsById.reserve(snapshot.nodes.size());
    for (const auto &node : snapshot.nodes) {
        colorsById.insert(node.node.id, colorFor(node.node, style));
    }

    // Runtime additions/removals can change renderer order independently of
    // snapshot order. Upload colors in the renderer's current node order.
    const QStringList ids = m_view.getNodeIds();
    QVector<QColor> colors;
    colors.reserve(ids.size());
    for (const QString &id : ids) {
        colors.append(colorsById.value(id, style.defaultColor));
    }
    m_view.setNodeColors(colors);
}

QColor GraphViewPresenter::colorFor(const graph::GraphNode &node,
                                    const GraphViewStyle &style) const
{
    if (!style.centerId.isEmpty() && node.id == style.centerId) {
        return style.centerColor;
    }
    if (node.group == QStringLiteral("actress") || node.id.startsWith(QLatin1Char('a'))) {
        return style.actressColor;
    }
    if (node.group == QStringLiteral("work") || node.id.startsWith(QLatin1Char('w'))) {
        return style.workColor;
    }
    return style.defaultColor;
}

} // namespace darkeye::graph_view
