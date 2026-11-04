#include "QuadTree.h"
#include <algorithm>
#include <cmath>
#include <limits>

namespace {

const float kMinBoxSize = 1e-6f;
const float kBoxPadding = 1.0f;
const float kSamePosEps = 1e-10f;

inline int quadrant(float px, float py, float boxCx, float boxCy)
{
    int qx = (px >= boxCx) ? 1 : 0;
    int qy = (py >= boxCy) ? 1 : 0;
    return qx + 2 * qy;
}

inline void childCenter(int q, float boxCx, float boxCy, float boxHalf,
                         float& cx, float& cy)
{
    cx = (q & 1) ? boxCx + boxHalf * 0.5f : boxCx - boxHalf * 0.5f;
    cy = (q & 2) ? boxCy + boxHalf * 0.5f : boxCy - boxHalf * 0.5f;
}

} // namespace

int QuadTree::allocNode()
{
    int idx = static_cast<int>(m_nodes.size());
    m_nodes.emplace_back();
    return idx;
}

// IMPORTANT: This function never stores persistent references/pointers to
// m_nodes elements across allocNode() or recursive insert() calls, because
// those may trigger std::vector reallocation and invalidate all references.
void QuadTree::insert(int nodeIdx, int pointIdx, float boxCx, float boxCy, float boxHalf,
                      const float* pos, const float* mass, int n)
{
    float px = pos[2 * pointIdx];
    float py = pos[2 * pointIdx + 1];

    if (m_nodes[nodeIdx].leafIdx >= 0) {
        int existingIdx = m_nodes[nodeIdx].leafIdx;
        if (existingIdx == pointIdx) return;

        for (int c = 0; c < 4; ++c)
            m_nodes[nodeIdx].child[c] = -1;

        float ex = pos[2 * existingIdx];
        float ey = pos[2 * existingIdx + 1];

        int qExisting = quadrant(ex, ey, boxCx, boxCy);
        int qNew      = quadrant(px, py, boxCx, boxCy);

        int chIdx = allocNode();  // may reallocate m_nodes
        m_nodes[nodeIdx].child[qExisting] = chIdx;
        m_nodes[chIdx].leafIdx  = existingIdx;
        m_nodes[chIdx].cx       = ex;
        m_nodes[chIdx].cy       = ey;
        m_nodes[chIdx].mass     = mass[existingIdx];
        m_nodes[chIdx].halfSize = boxHalf * 0.5f;

        float chCx, chCy;
        childCenter(qExisting, boxCx, boxCy, boxHalf, chCx, chCy);
        float chHalf = boxHalf * 0.5f;

        bool samePosition      = (std::abs(px - ex) < kSamePosEps &&
                                  std::abs(py - ey) < kSamePosEps);
        bool cannotSplitFurther = (chHalf < kMinBoxSize);

        if (qNew == qExisting && !samePosition && !cannotSplitFurther) {
            insert(chIdx, pointIdx, chCx, chCy, chHalf, pos, mass, n);
        } else if (qNew == qExisting) {
            qNew = (qExisting + 1 + (pointIdx % 3)) % 4;
            int ch2Idx = allocNode();
            m_nodes[nodeIdx].child[qNew] = ch2Idx;
            m_nodes[ch2Idx].leafIdx  = pointIdx;
            m_nodes[ch2Idx].cx       = px;
            m_nodes[ch2Idx].cy       = py;
            m_nodes[ch2Idx].mass     = mass[pointIdx];
            m_nodes[ch2Idx].halfSize = boxHalf * 0.5f;
        } else {
            int ch2Idx = allocNode();
            m_nodes[nodeIdx].child[qNew] = ch2Idx;
            m_nodes[ch2Idx].leafIdx  = pointIdx;
            m_nodes[ch2Idx].cx       = px;
            m_nodes[ch2Idx].cy       = py;
            m_nodes[ch2Idx].mass     = mass[pointIdx];
            m_nodes[ch2Idx].halfSize = boxHalf * 0.5f;
        }

        m_nodes[nodeIdx].leafIdx  = -1;
        m_nodes[nodeIdx].halfSize = boxHalf;
        computeMassAndCom(nodeIdx);
        return;
    }

    if (m_nodes[nodeIdx].child[0] < 0 && m_nodes[nodeIdx].child[1] < 0 &&
        m_nodes[nodeIdx].child[2] < 0 && m_nodes[nodeIdx].child[3] < 0) {
        m_nodes[nodeIdx].leafIdx  = pointIdx;
        m_nodes[nodeIdx].cx       = px;
        m_nodes[nodeIdx].cy       = py;
        m_nodes[nodeIdx].mass     = mass[pointIdx];
        m_nodes[nodeIdx].halfSize = boxHalf;
        return;
    }

    int q = quadrant(px, py, boxCx, boxCy);
    float chCx, chCy;
    childCenter(q, boxCx, boxCy, boxHalf, chCx, chCy);
    float chHalf = boxHalf * 0.5f;

    if (m_nodes[nodeIdx].child[q] < 0) {
        int chIdx = allocNode();
        m_nodes[nodeIdx].child[q] = chIdx;
        m_nodes[chIdx].leafIdx  = pointIdx;
        m_nodes[chIdx].cx       = px;
        m_nodes[chIdx].cy       = py;
        m_nodes[chIdx].mass     = mass[pointIdx];
        m_nodes[chIdx].halfSize = chHalf;
    } else {
        int childIdx = m_nodes[nodeIdx].child[q];
        insert(childIdx, pointIdx, chCx, chCy, chHalf, pos, mass, n);
    }
    computeMassAndCom(nodeIdx);
}

void QuadTree::computeMassAndCom(int nodeIdx)
{
    if (m_nodes[nodeIdx].leafIdx >= 0) return;

    float totalMass = 0.0f;
    float mx = 0.0f, my = 0.0f;
    for (int c = 0; c < 4; ++c) {
        int ch = m_nodes[nodeIdx].child[c];
        if (ch < 0) continue;
        float cm = m_nodes[ch].mass;
        totalMass += cm;
        mx += m_nodes[ch].cx * cm;
        my += m_nodes[ch].cy * cm;
    }
    if (totalMass > 0.0f) {
        m_nodes[nodeIdx].mass = totalMass;
        m_nodes[nodeIdx].cx   = mx / totalMass;
        m_nodes[nodeIdx].cy   = my / totalMass;
    }
}

void QuadTree::build(const float* pos, const float* mass, int n)
{
    m_nodes.clear();
    if (n <= 0) return;

    // Reserve to minimise reallocations (typical tree has ~2-4N nodes;
    // degenerate overlap cases can create up to ~32N due to recursive splits)
    m_nodes.reserve(static_cast<size_t>(n) * 8 + 16);

    float xmin =  std::numeric_limits<float>::max();
    float ymin =  std::numeric_limits<float>::max();
    float xmax = -std::numeric_limits<float>::max();
    float ymax = -std::numeric_limits<float>::max();

    for (int i = 0; i < n; ++i) {
        float x = pos[2 * i];
        float y = pos[2 * i + 1];
        if (std::isnan(x) || std::isnan(y)) continue;
        xmin = std::min(xmin, x);
        ymin = std::min(ymin, y);
        xmax = std::max(xmax, x);
        ymax = std::max(ymax, y);
    }

    if (xmin > xmax) return; // all NaN

    float dx = xmax - xmin;
    float dy = ymax - ymin;
    if (dx < kMinBoxSize) dx = kMinBoxSize;
    if (dy < kMinBoxSize) dy = kMinBoxSize;
    float half  = std::max(dx, dy) * 0.5f + kBoxPadding;
    float boxCx = (xmin + xmax) * 0.5f;
    float boxCy = (ymin + ymax) * 0.5f;

    allocNode(); // root = index 0
    m_nodes[0].halfSize = half;

    for (int i = 0; i < n; ++i) {
        float x = pos[2 * i];
        float y = pos[2 * i + 1];
        if (std::isnan(x) || std::isnan(y)) continue;
        insert(0, i, boxCx, boxCy, half, pos, mass, n);
    }

    computeMassAndCom(0);

    // Actual descendant bounds also cover coincident points placed in a
    // different quadrant to terminate subdivision.
    for (int index = static_cast<int>(m_nodes.size()) - 1; index >= 0; --index) {
        auto& node = m_nodes[index];
        if (node.leafIdx >= 0) {
            node.minX = node.maxX = node.cx;
            node.minY = node.maxY = node.cy;
        } else {
            node.minX = node.minY = std::numeric_limits<float>::max();
            node.maxX = node.maxY = -std::numeric_limits<float>::max();
            for (int child : node.child) {
                if (child < 0) continue;
                node.minX = std::min(node.minX, m_nodes[child].minX);
                node.minY = std::min(node.minY, m_nodes[child].minY);
                node.maxX = std::max(node.maxX, m_nodes[child].maxX);
                node.maxY = std::max(node.maxY, m_nodes[child].maxY);
            }
        }
    }
}


