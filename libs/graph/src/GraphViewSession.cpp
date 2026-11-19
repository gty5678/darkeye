#include "graph/GraphViewSession.h"

#include <algorithm>
#include <limits>
#include <queue>

namespace darkeye::graph {

GraphViewSession::GraphViewSession(const GraphStore &store)
    : m_store(store)
{
}

void GraphViewSession::setFilterMode(GraphFilterMode mode)
{
    m_filterMode = mode;
}

void GraphViewSession::setEgoCenter(QString nodeId)
{
    m_egoCenterId = std::move(nodeId);
}

void GraphViewSession::setEgoRadius(int radius)
{
    m_egoRadius = std::max(0, radius);
}

void GraphViewSession::setFavoriteWorkIds(QSet<QString> workIds)
{
    m_favoriteWorkIds = std::move(workIds);
}

bool GraphViewSession::setFilteredWorkIds(QSet<QString> workIds)
{
    if (m_filteredWorkIds == workIds) return false;
    m_filteredWorkIds = std::move(workIds);
    return true;
}

bool GraphViewSession::setFilteredActressIds(QSet<QString> actressIds)
{
    if (m_filteredActressIds == actressIds) return false;
    m_filteredActressIds = std::move(actressIds);
    return true;
}

GraphFilterMode GraphViewSession::filterMode() const noexcept
{
    return m_filterMode;
}

QSet<int> GraphViewSession::filteredNodeIndexes() const
{
    if (m_filterMode == GraphFilterMode::Empty) {
        return {};
    }
    if (m_filterMode == GraphFilterMode::All) {
        const QVector<int> activeIndexes = m_store.activeNodeIndexes();
        return QSet<int>(activeIndexes.cbegin(), activeIndexes.cend());
    }
    const bool filterActresses = m_filterMode == GraphFilterMode::FilteredActresses;
    if (filterActresses || m_filterMode == GraphFilterMode::FavoriteWorks
        || m_filterMode == GraphFilterMode::FilteredWorks) {
        const auto &ids = filterActresses ? m_filteredActressIds
            : (m_filterMode == GraphFilterMode::FavoriteWorks ? m_favoriteWorkIds : m_filteredWorkIds);
        const QString seedGroup = filterActresses ? QStringLiteral("actress") : QStringLiteral("work");
        const QString neighborGroup = filterActresses ? QStringLiteral("work") : QStringLiteral("actress");
        const QChar seedPrefix = filterActresses ? u'a' : u'w';
        const QChar neighborPrefix = filterActresses ? u'w' : u'a';
        QSet<int> result;
        for (const QString &id : ids) {
            const auto index = m_store.indexOf(id);
            const GraphNode *node = index.has_value() ? m_store.nodeAt(*index) : nullptr;
            if (node == nullptr || (node->group != seedGroup && !node->id.startsWith(seedPrefix))) {
                continue;
            }
            result.insert(*index);
            for (const int neighbor : m_store.neighbors(*index)) {
                const GraphNode *neighborNode = m_store.nodeAt(neighbor);
                if (neighborNode != nullptr
                    && (neighborNode->group == neighborGroup
                        || neighborNode->id.startsWith(neighborPrefix))) {
                    result.insert(neighbor);
                }
            }
        }
        return result;
    }

    const auto center = m_store.indexOf(m_egoCenterId);
    if (!center.has_value() || m_store.nodeAt(*center) == nullptr) {
        return {};
    }
    QSet<int> result{*center};
    std::queue<std::pair<int, int>> pending;
    pending.emplace(*center, 0);
    while (!pending.empty()) {
        const auto [index, depth] = pending.front();
        pending.pop();
        if (depth >= m_egoRadius) {
            continue;
        }
        for (const int neighbor : m_store.neighbors(index)) {
            if (!result.contains(neighbor)) {
                result.insert(neighbor);
                pending.emplace(neighbor, depth + 1);
            }
        }
    }
    return result;
}

GraphViewSnapshot GraphViewSession::snapshot() const
{
    const QSet<int> visible = filteredNodeIndexes();
    QVector<int> orderedIndexes = visible.values();
    std::sort(orderedIndexes.begin(), orderedIndexes.end(), [this](int left, int right) {
        return m_store.nodeAt(left)->id < m_store.nodeAt(right)->id;
    });

    QHash<int, int> degrees;
    QVector<GraphEdge> visibleEdges;
    for (const GraphEdge &edge : m_store.edges()) {
        const auto source = m_store.indexOf(edge.sourceId);
        const auto target = m_store.indexOf(edge.targetId);
        if (!source.has_value() || !target.has_value()
            || !visible.contains(*source) || !visible.contains(*target)) {
            continue;
        }
        visibleEdges.append(edge);
        degrees[*source] += 1;
        degrees[*target] += 1;
    }
    std::sort(visibleEdges.begin(), visibleEdges.end(), [](const GraphEdge &left,
                                                            const GraphEdge &right) {
        const auto leftPair = std::minmax(left.sourceId, left.targetId);
        const auto rightPair = std::minmax(right.sourceId, right.targetId);
        return leftPair < rightPair;
    });

    int minDegree = std::numeric_limits<int>::max();
    int maxDegree = 0;
    for (const int index : orderedIndexes) {
        const int degree = degrees.value(index, 0);
        minDegree = std::min(minDegree, degree);
        maxDegree = std::max(maxDegree, degree);
    }

    GraphViewSnapshot result;
    result.edges = std::move(visibleEdges);
    result.nodes.reserve(orderedIndexes.size());
    for (const int index : orderedIndexes) {
        const int degree = degrees.value(index, 0);
        const float radius = maxDegree <= minDegree
            ? 7.0F
            : 4.0F + 6.0F * static_cast<float>(degree - minDegree)
                / static_cast<float>(maxDegree - minDegree);
        result.nodes.append({*m_store.nodeAt(index), radius});
    }
    return result;
}

GraphViewSnapshot GraphViewSession::reload()
{
    m_previous = snapshot();
    m_loaded = true;
    return m_previous;
}

GraphDelta GraphViewSession::refresh()
{
    const GraphViewSnapshot current = snapshot();
    if (!m_loaded) {
        m_previous = {};
        m_loaded = true;
    }

    const auto previousNodes = nodeMap(m_previous);
    const auto currentNodes = nodeMap(current);
    const auto previousEdges = edgeMap(m_previous);
    const auto currentEdges = edgeMap(current);
    GraphDelta delta;

    for (auto it = previousEdges.cbegin(); it != previousEdges.cend(); ++it) {
        if (!currentEdges.contains(it.key())) {
            delta.append({GraphDeltaOperation::Type::RemoveEdge, {}, it.value()});
        }
    }
    for (auto it = previousNodes.cbegin(); it != previousNodes.cend(); ++it) {
        if (!currentNodes.contains(it.key())) {
            delta.append({GraphDeltaOperation::Type::RemoveNode, it.value(), {}});
        }
    }
    for (auto it = currentNodes.cbegin(); it != currentNodes.cend(); ++it) {
        const auto previous = previousNodes.constFind(it.key());
        if (previous == previousNodes.cend()) {
            delta.append({GraphDeltaOperation::Type::AddNode, it.value(), {}});
        } else if (previous.value() != it.value()) {
            delta.append({GraphDeltaOperation::Type::UpdateNode, it.value(), {}});
        }
    }
    for (auto it = currentEdges.cbegin(); it != currentEdges.cend(); ++it) {
        const auto previous = previousEdges.constFind(it.key());
        if (previous == previousEdges.cend()) {
            delta.append({GraphDeltaOperation::Type::AddEdge, {}, it.value()});
        } else if (previous.value() != it.value()) {
            delta.append({GraphDeltaOperation::Type::UpdateEdge, {}, it.value()});
        }
    }

    m_previous = current;
    return delta;
}

QString GraphViewSession::edgeKey(const GraphEdge &edge)
{
    const auto [first, second] = std::minmax(edge.sourceId, edge.targetId);
    return first + QChar(0x001f) + second;
}

QHash<QString, GraphViewNode>
GraphViewSession::nodeMap(const GraphViewSnapshot &snapshot)
{
    QHash<QString, GraphViewNode> result;
    for (const GraphViewNode &node : snapshot.nodes) {
        result.insert(node.node.id, node);
    }
    return result;
}

QHash<QString, GraphEdge>
GraphViewSession::edgeMap(const GraphViewSnapshot &snapshot)
{
    QHash<QString, GraphEdge> result;
    for (const GraphEdge &edge : snapshot.edges) {
        result.insert(edgeKey(edge), edge);
    }
    return result;
}

} // namespace darkeye::graph
