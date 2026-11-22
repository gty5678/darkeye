#include "graph/GraphStore.h"

#include <algorithm>

namespace darkeye::graph {

quint64 GraphStore::edgeKey(int first, int second)
{
    const quint32 low = static_cast<quint32>(std::min(first, second));
    const quint32 high = static_cast<quint32>(std::max(first, second));
    return (static_cast<quint64>(low) << 32U) | high;
}

void GraphStore::replace(const QVector<GraphNode> &nodes,
                         const QVector<GraphEdge> &edges)
{
    m_nodes.clear();
    m_indexById.clear();
    m_edges.clear();
    m_adjacency.clear();

    for (const GraphNode &node : nodes) {
        upsertNode(node);
    }
    for (const GraphEdge &edge : edges) {
        upsertEdge(edge);
    }
}

bool GraphStore::upsertNode(GraphNode node)
{
    if (node.id.isEmpty()) {
        return false;
    }
    if (node.label.isEmpty()) {
        node.label = node.id;
    }

    const auto existing = m_indexById.constFind(node.id);
    if (existing != m_indexById.cend()) {
        NodeRecord &record = m_nodes[*existing];
        const bool changed = !record.active || record.node != node;
        record.node = std::move(node);
        record.active = true;
        return changed;
    }

    const int index = m_nodes.size();
    m_nodes.append({std::move(node), true});
    m_indexById.insert(m_nodes[index].node.id, index);
    m_adjacency.append(QSet<int>{});
    return true;
}

bool GraphStore::removeNode(const QString &nodeId)
{
    const auto index = indexOf(nodeId);
    if (!index.has_value() || !m_nodes[*index].active) {
        return false;
    }

    const QVector<int> adjacent = m_adjacency[*index].values();
    for (const int neighbor : adjacent) {
        removeEdge(nodeId, m_nodes[neighbor].node.id);
    }
    m_nodes[*index].active = false;
    return true;
}

bool GraphStore::upsertEdge(GraphEdge edge)
{
    const auto source = indexOf(edge.sourceId);
    const auto target = indexOf(edge.targetId);
    if (!source.has_value() || !target.has_value() || *source == *target
        || !m_nodes[*source].active || !m_nodes[*target].active) {
        return false;
    }

    const quint64 key = edgeKey(*source, *target);
    const auto existing = m_edges.find(key);
    if (existing != m_edges.end()) {
        const bool changed = existing->type != edge.type;
        existing->type = std::move(edge.type);
        return changed;
    }

    m_edges.insert(key, {*source, *target, std::move(edge.type)});
    m_adjacency[*source].insert(*target);
    m_adjacency[*target].insert(*source);
    return true;
}

bool GraphStore::removeEdge(const QString &sourceId, const QString &targetId)
{
    const auto source = indexOf(sourceId);
    const auto target = indexOf(targetId);
    if (!source.has_value() || !target.has_value()) {
        return false;
    }

    const quint64 key = edgeKey(*source, *target);
    if (!m_edges.remove(key)) {
        return false;
    }
    m_adjacency[*source].remove(*target);
    m_adjacency[*target].remove(*source);
    return true;
}

bool GraphStore::containsNode(const QString &nodeId) const
{
    const auto index = indexOf(nodeId);
    return index.has_value() && m_nodes[*index].active;
}

QVector<GraphNode> GraphStore::nodes() const
{
    QVector<GraphNode> result;
    for (const NodeRecord &record : m_nodes) {
        if (record.active) {
            result.append(record.node);
        }
    }
    return result;
}

QVector<GraphEdge> GraphStore::edges() const
{
    QVector<GraphEdge> result;
    for (const EdgeRecord &edge : m_edges) {
        if (nodeAt(edge.source) != nullptr && nodeAt(edge.target) != nullptr) {
            result.append({m_nodes[edge.source].node.id,
                           m_nodes[edge.target].node.id,
                           edge.type});
        }
    }
    return result;
}

QVector<int> GraphStore::activeNodeIndexes() const
{
    QVector<int> result;
    for (int index = 0; index < m_nodes.size(); ++index) {
        if (m_nodes[index].active) {
            result.append(index);
        }
    }
    return result;
}

QVector<int> GraphStore::neighbors(int nodeIndex) const
{
    if (nodeAt(nodeIndex) == nullptr) {
        return {};
    }
    QVector<int> result = m_adjacency[nodeIndex].values();
    std::sort(result.begin(), result.end());
    return result;
}

const GraphNode *GraphStore::nodeAt(int nodeIndex) const
{
    if (nodeIndex < 0 || nodeIndex >= m_nodes.size()
        || !m_nodes[nodeIndex].active) {
        return nullptr;
    }
    return &m_nodes[nodeIndex].node;
}

std::optional<int> GraphStore::indexOf(const QString &nodeId) const
{
    const auto found = m_indexById.constFind(nodeId);
    if (found == m_indexById.cend()) {
        return std::nullopt;
    }
    return *found;
}

} // namespace darkeye::graph
