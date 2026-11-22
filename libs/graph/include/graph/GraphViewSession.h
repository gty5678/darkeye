#pragma once

#include "graph/GraphStore.h"

#include <QHash>
#include <QSet>

namespace darkeye::graph {

enum class GraphFilterMode
{
    All,
    Empty,
    Ego,
    FavoriteWorks,
    FilteredWorks,
    FilteredActresses,
};

struct GraphViewNode
{
    GraphNode node;
    float radius = 7.0F;

    bool operator==(const GraphViewNode &) const = default;
};

struct GraphViewSnapshot
{
    QVector<GraphViewNode> nodes;
    QVector<GraphEdge> edges;
};

struct GraphDeltaOperation
{
    enum class Type
    {
        AddNode,
        UpdateNode,
        RemoveNode,
        AddEdge,
        UpdateEdge,
        RemoveEdge,
    };

    Type type;
    GraphViewNode node;
    GraphEdge edge;
};

using GraphDelta = QVector<GraphDeltaOperation>;

/**
 * Per-view filter state over a shared GraphStore.
 *
 * The session does no rendering and has no QObject dependency. A future Qt/Python
 * bridge can own one session per ForceViewRhiWidget and pass its snapshots or
 * deltas to the rendering library without rebuilding NetworkX subgraphs.
 */
class GraphViewSession
{
public:
    explicit GraphViewSession(const GraphStore &store);

    void setFilterMode(GraphFilterMode mode);
    void setEgoCenter(QString nodeId);
    void setEgoRadius(int radius);
    void setFavoriteWorkIds(QSet<QString> workIds);
    bool setFilteredWorkIds(QSet<QString> workIds);
    bool setFilteredActressIds(QSet<QString> actressIds);
    [[nodiscard]] GraphFilterMode filterMode() const noexcept;

    [[nodiscard]] GraphViewSnapshot snapshot() const;
    [[nodiscard]] GraphViewSnapshot reload();
    [[nodiscard]] GraphDelta refresh();

private:
    [[nodiscard]] QSet<int> filteredNodeIndexes() const;
    [[nodiscard]] static QString edgeKey(const GraphEdge &edge);
    [[nodiscard]] static QHash<QString, GraphViewNode>
    nodeMap(const GraphViewSnapshot &snapshot);
    [[nodiscard]] static QHash<QString, GraphEdge>
    edgeMap(const GraphViewSnapshot &snapshot);

    const GraphStore &m_store;
    GraphFilterMode m_filterMode = GraphFilterMode::All;
    QString m_egoCenterId;
    int m_egoRadius = 1;
    QSet<QString> m_favoriteWorkIds;
    QSet<QString> m_filteredWorkIds;
    QSet<QString> m_filteredActressIds;
    bool m_loaded = false;
    GraphViewSnapshot m_previous;
};

} // namespace darkeye::graph
