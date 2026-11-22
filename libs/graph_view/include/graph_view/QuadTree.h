#ifndef QUADTREE_H
#define QUADTREE_H

#include <vector>
#include <cstdint>

/**
 * QuadTree node for Barnes-Hut N-body simulation.
 * Stores center of mass, total mass, cell size, and children.
 */
struct QuadNode
{
    float cx = 0.0f;      // center of mass x
    float cy = 0.0f;      // center of mass y
    float mass = 0.0f;    // total mass in this cell
    float halfSize = 0.0f; // half of cell side length (s/2 for theta: s/d)
    int child[4] = {-1, -1, -1, -1}; // NW, NE, SW, SE
    int leafIdx = -1;     // node index if leaf (single point), else -1
    float minX = 0.0f, minY = 0.0f, maxX = 0.0f, maxY = 0.0f;
};

/**
 * 2D Quadtree for Barnes-Hut acceleration.
 * Serial build, read-only after build.
 */
class QuadTree
{
public:
    QuadTree() = default;

    /** Build tree from positions and masses. Clears previous state. */
    void build(const float* pos, const float* mass, int n);

    int rootIndex() const { return 0; }
    const std::vector<QuadNode>& nodes() const { return m_nodes; }
    bool empty() const { return m_nodes.empty(); }

private:
    std::vector<QuadNode> m_nodes;

    int allocNode();
    void insert(int nodeIdx, int pointIdx, float boxCx, float boxCy, float boxHalf,
                const float* pos, const float* mass, int n);
    void computeMassAndCom(int nodeIdx);
};

#endif // QUADTREE_H


