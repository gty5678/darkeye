#pragma once

#include <QHash>
#include <QSet>
#include <QString>
#include <QVector>

#include <optional>

namespace darkeye::graph {

struct GraphNode
{
    QString id;
    QString label;
    QString group;

    bool operator==(const GraphNode &) const = default;
};

struct GraphEdge
{
    QString sourceId;
    QString targetId;
    QString type;

    bool operator==(const GraphEdge &) const = default;
};

/**
 * Shared topology store for all graph views.
 *
 * Public IDs remain the Python-compatible strings such as "a36" and "w123".
 * Internally, each active node receives a compact integer index. Incremental
 * mutations preserve that index, so view sessions can use adjacency traversal
 * without crossing the Python boundary for every node and edge.
 */
class GraphStore
{
public:
    void replace(const QVector<GraphNode> &nodes, const QVector<GraphEdge> &edges);

    bool upsertNode(GraphNode node);
    bool removeNode(const QString &nodeId);
    bool upsertEdge(GraphEdge edge);
    bool removeEdge(const QString &sourceId, const QString &targetId);

    [[nodiscard]] bool containsNode(const QString &nodeId) const;
    [[nodiscard]] QVector<GraphNode> nodes() const;
    [[nodiscard]] QVector<GraphEdge> edges() const;
    [[nodiscard]] QVector<int> activeNodeIndexes() const;
    [[nodiscard]] QVector<int> neighbors(int nodeIndex) const;
    [[nodiscard]] const GraphNode *nodeAt(int nodeIndex) const;
    [[nodiscard]] std::optional<int> indexOf(const QString &nodeId) const;

private:
    struct NodeRecord
    {
        GraphNode node;
        bool active = true;
    };

    struct EdgeRecord
    {
        int source = -1;
        int target = -1;
        QString type;
    };

    [[nodiscard]] static quint64 edgeKey(int first, int second);

    QVector<NodeRecord> m_nodes;
    QHash<QString, int> m_indexById;
    QHash<quint64, EdgeRecord> m_edges;
    QVector<QSet<int>> m_adjacency;
};

} // namespace darkeye::graph
