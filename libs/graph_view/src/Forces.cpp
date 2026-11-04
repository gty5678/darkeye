#include "Forces.h"
#include "QuadTree.h"
#include <cmath>
#include <algorithm>
#include <cstdint>
#include <unordered_map>
#include <vector>

namespace {
const float kOverlapDist2 = 1e-10f;
const float kOverlapDist  = 1e-5f;

/** Deterministic unit direction from j toward i for overlapping pairs (i, j). */
inline void overlapFallbackDirection(int i, int j, float& ux, float& uy)
{
    const int lower = std::min(i, j);
    const int upper = std::max(i, j);
    uint32_t hash = static_cast<uint32_t>(lower) * 0x9e3779b9U;
    hash ^= static_cast<uint32_t>(upper) * 0x85ebca6bU;
    hash ^= hash >> 16;
    const float ax = static_cast<float>(hash & 0xffffU) / 32767.5f - 1.0f;
    const float ay = static_cast<float>((hash >> 16) & 0xffffU) / 32767.5f - 1.0f;
    float len2 = ax * ax + ay * ay;
    if (len2 < 1e-20f) {
        ux = 1.0f;
        uy = 0.0f;
    } else {
        float invLen = 1.0f / std::sqrt(len2);
        ux = ax * invLen;
        uy = ay * invLen;
    }
    if (i > j) {
        ux = -ux;
        uy = -uy;
    }
}

/** Barnes-Hut force accumulation for node i from quadtree node. */
inline void manyBodyAccumulate(
    int i,
    int nodeIdx,
    const std::vector<QuadNode>& nodes,
    const float* pos,
    const float* mass,
    float strength,
    float cutoff2,
    float theta,
    float alpha,
    float& fx,
    float& fy)
{
    const QuadNode& nd = nodes[nodeIdx];
    float xi = pos[2 * i];
    float yi = pos[2 * i + 1];
    float mi = mass[i];

    if (nd.leafIdx >= 0) {
        int j = nd.leafIdx;
        if (i == j) return;
        float dx = xi - nd.cx;
        float dy = yi - nd.cy;
        float rawDist2 = dx * dx + dy * dy;
        float dist2 = rawDist2 + 1e-6f;
        if (dist2 >= cutoff2) return;

        float mj = nd.mass;
        float s  = strength * mi * mj / std::max(dist2, 1.0f) * alpha;
        float ux, uy;
        if (rawDist2 < kOverlapDist2) {
            overlapFallbackDirection(i, j, ux, uy);
        } else {
            float invd = 1.0f / std::sqrt(dist2);
            ux = dx * invd;
            uy = dy * invd;
        }
        fx += s * ux;
        fy += s * uy;
        return;
    }

    // Approximate only subtrees entirely inside the cutoff. A center of mass
    // outside the cutoff does not imply that every descendant is outside.
    float dx_com = xi - nd.cx;
    float dy_com = yi - nd.cy;
    float dist2_com = dx_com * dx_com + dy_com * dy_com + 1e-6f;

    float d = std::sqrt(dist2_com);
    float cellSize = 2.0f * nd.halfSize;
    float s_over_d = cellSize / std::max(d, 1e-6f);

    const float nearX = std::max({nd.minX - xi, xi - nd.maxX, 0.0f});
    const float nearY = std::max({nd.minY - yi, yi - nd.maxY, 0.0f});
    if (nearX * nearX + nearY * nearY + 1e-6f >= cutoff2) return;
    const float farX = std::max(std::abs(xi - nd.minX), std::abs(xi - nd.maxX));
    const float farY = std::max(std::abs(yi - nd.minY), std::abs(yi - nd.maxY));
    const bool fullyInsideCutoff = farX * farX + farY * farY + 1e-6f < cutoff2;

    if (s_over_d < theta && fullyInsideCutoff) {
        // Cell is far enough: use cutoff on center-of-mass distance
        if (dist2_com >= cutoff2) return;

        float mj = nd.mass;
        float s  = strength * mi * mj / std::max(dist2_com, 1.0f) * alpha;
        float ux = dx_com / d;
        float uy = dy_com / d;
        fx += s * ux;
        fy += s * uy;
    } else {
        for (int c = 0; c < 4; ++c) {
            if (nd.child[c] >= 0)
                manyBodyAccumulate(i, nd.child[c], nodes, pos, mass,
                                   strength, cutoff2, theta, alpha, fx, fy);
        }
    }
}
}

// =====================================================================
// CenterForce
// =====================================================================
CenterForce::CenterForce(float cx, float cy, float strength)
    : m_cx(cx), m_cy(cy), m_strength(strength)
{}

void CenterForce::apply(float alpha)
{
    const int N = m_state->nNodes;
    const float sa = m_strength * alpha;
    float* vel = m_state->vel.data();
    const float* pos = m_state->pos.data();

    for (int i = 0; i < N; ++i) {
        float dx = m_cx - pos[2 * i];
        float dy = m_cy - pos[2 * i + 1];
        vel[2 * i]     += dx * sa;
        vel[2 * i + 1] += dy * sa;
    }
}

// =====================================================================
// LinkForce
// =====================================================================
LinkForce::LinkForce(float k, float distance)
    : m_k(k), m_distance(distance)
{}

void LinkForce::apply(float alpha)
{
    const int E = m_state->edgeCount();
    if (E == 0) return;

    const int*   edges = m_state->edges.data();
    const float* pos   = m_state->pos.data();
    float*       vel   = m_state->vel.data();
    const float  ka    = m_k * alpha;
    const float  dist  = m_distance;

    for (int e = 0; e < E; ++e) {
        int s = edges[2 * e];
        int d = edges[2 * e + 1];
        if (s < 0 || s >= m_state->nNodes || d < 0 || d >= m_state->nNodes || s == d)
            continue;

        float dx = pos[2 * d]     - pos[2 * s];
        float dy = pos[2 * d + 1] - pos[2 * s + 1];
        float len = std::sqrt(dx * dx + dy * dy) + 1e-6f;
        float f   = (len - dist) / len * ka;

        float fx = dx * f;
        float fy = dy * f;

        vel[2 * s]     += fx;
        vel[2 * s + 1] += fy;
        vel[2 * d]     -= fx;
        vel[2 * d + 1] -= fy;
    }
}

// =====================================================================
// ManyBodyForce
// =====================================================================
ManyBodyForce::ManyBodyForce(float strength, float cutoff2, float theta)
    : m_strength(strength), m_cutoff2(cutoff2), m_theta(theta)
{}

void ManyBodyForce::apply(float alpha)
{
    const int N = m_state->nNodes;
    if (N < 2) return;

    if (N < kBarnesHutThreshold) {
        applyBlock(alpha, 256);
    } else {
        applyBarnesHut(alpha);
    }
}

// Block-tiled O(N^2) — mirrors Python manybody_block_kernel
void ManyBodyForce::applyBlock(float alpha, int block)
{
    const int    N        = m_state->nNodes;
    const float* pos      = m_state->pos.data();
    const float* mass     = m_state->mass.data();
    float*       vel      = m_state->vel.data();
    const float  strength = m_strength;
    const float  cutoff2  = m_cutoff2;

    for (int i0 = 0; i0 < N; i0 += block) {
        int i1 = std::min(i0 + block, N);
        // Iterate upper-triangular block pairs only. Each unordered node pair
        // must be processed exactly once because this kernel writes to both i and j.
        for (int j0 = i0; j0 < N; j0 += block) {
            int j1 = std::min(j0 + block, N);
            for (int i = i0; i < i1; ++i) {
                float xi = pos[2 * i];
                float yi = pos[2 * i + 1];
                float mi = mass[i];
                for (int j = j0; j < j1; ++j) {
                    // Skip symmetric half when in the same block
                    if (i0 == j0 && i >= j) continue;

                    float dx = xi - pos[2 * j];
                    float dy = yi - pos[2 * j + 1];
                    float rawDist2 = dx * dx + dy * dy;
                    float dist2 = rawDist2 + 1e-6f;
                    if (dist2 >= cutoff2) continue;

                    float mj = mass[j];
                    float s  = strength * mi * mj / std::max(dist2, 1.0f) * alpha;
                    float ux, uy;
                    if (rawDist2 < kOverlapDist2) {
                        overlapFallbackDirection(i, j, ux, uy);
                    } else {
                        float invd = 1.0f / std::sqrt(dist2);
                        ux = dx * invd;
                        uy = dy * invd;
                    }
                    float fx = s * ux;
                    float fy = s * uy;

                    vel[2 * i]     += fx;
                    vel[2 * i + 1] += fy;
                    vel[2 * j]     -= fx;
                    vel[2 * j + 1] -= fy;
                }
            }
        }
    }
}

// Parallel kernel (currently not selected by default policy)
void ManyBodyForce::applyParallel(float alpha)
{
    const int    N        = m_state->nNodes;
    const float* pos      = m_state->pos.data();
    const float* mass     = m_state->mass.data();
    float*       vel      = m_state->vel.data();
    const float  strength = m_strength;
    const float  cutoff2  = m_cutoff2;

    // Each thread accumulates its own force for node i, then writes.
    // This avoids race conditions on vel[j] — only vel[i] is written per i.

#ifdef _MSC_VER
#pragma warning(suppress : 6993) // MSVC code analysis does not model OpenMP execution.
#endif
#pragma omp parallel for schedule(static)
    for (int i = 0; i < N; ++i) {
        float xi = pos[2 * i];
        float yi = pos[2 * i + 1];
        float mi = mass[i];

        float fx_sum = 0.0f;
        float fy_sum = 0.0f;

        for (int j = 0; j < N; ++j) {
            if (i == j) continue;

            float dx = xi - pos[2 * j];
            float dy = yi - pos[2 * j + 1];
            float rawDist2 = dx * dx + dy * dy;
            float dist2 = rawDist2 + 1e-6f;
            if (dist2 >= cutoff2) continue;

            float s = strength * mi * mass[j] / std::max(dist2, 1.0f) * alpha;
            float ux, uy;
            if (rawDist2 < kOverlapDist2) {
                overlapFallbackDirection(i, j, ux, uy);
            } else {
                float invd = 1.0f / std::sqrt(dist2);
                ux = dx * invd;
                uy = dy * invd;
            }
            fx_sum += s * ux;
            fy_sum += s * uy;
        }

        vel[2 * i]     += fx_sum;
        vel[2 * i + 1] += fy_sum;
    }
}

// Barnes-Hut O(N log N) — serial tree build, parallel force computation
void ManyBodyForce::applyBarnesHut(float alpha)
{
    const int    N        = m_state->nNodes;
    const float* pos      = m_state->pos.data();
    const float* mass     = m_state->mass.data();
    float*       vel      = m_state->vel.data();
    const float  strength = m_strength;
    const float  cutoff2  = m_cutoff2;
    const float  theta    = m_theta;

    // Serial tree build
    m_quadTree.build(pos, mass, N);
    if (m_quadTree.empty()) return;

    const std::vector<QuadNode>& nodes = m_quadTree.nodes();

#pragma omp parallel for schedule(static)
    for (int i = 0; i < N; ++i) {
        float fx_sum = 0.0f;
        float fy_sum = 0.0f;
        manyBodyAccumulate(i, m_quadTree.rootIndex(), nodes, pos, mass,
                           strength, cutoff2, theta, alpha, fx_sum, fy_sum);
        vel[2 * i]     += fx_sum;
        vel[2 * i + 1] += fy_sum;
    }
}

// =====================================================================
// CollisionForce
// =====================================================================
CollisionForce::CollisionForce(float radius, float strength)
    : m_radius(radius), m_strength(strength)
{}

void CollisionForce::apply(float alpha)
{
    const int N = m_state->nNodes;
    if (N < 2) return;

    if (N < kGridThreshold) {
        applyBruteForce(alpha);
    } else {
        applyGrid(alpha);   // O(N) uniform grid; use applyParallel(alpha) for brute-force
    }
}

void CollisionForce::applyGrid(float alpha)
{
    const int    N        = m_state->nNodes;
    const float* pos      = m_state->pos.data();
    float*       vel      = m_state->vel.data();
    const float  R        = m_radius;
    const float  sa       = m_strength * alpha;
    const float  eps      = 1e-6f;

    // Cell size >= R so we only need to check 3x3 neighborhood
    const float cellSize = std::max(R, 1e-6f);
    const float invCell  = 1.0f / cellSize;

    struct CellKey {
        int x, y;
        bool operator==(const CellKey& o) const { return x == o.x && y == o.y; }
    };
    struct CellHash {
        size_t operator()(const CellKey& c) const {
            size_t h = static_cast<size_t>(c.x) * 0x9e3779b97f4a7c15ULL;
            h ^= static_cast<size_t>(c.y) * 0x517cc1b727220a95ULL;
            return h;
        }
    };
    std::unordered_map<CellKey, std::vector<int>, CellHash> grid;

    for (int i = 0; i < N; ++i) {
        int cx = static_cast<int>(std::floor(pos[2 * i] * invCell));
        int cy = static_cast<int>(std::floor(pos[2 * i + 1] * invCell));
        grid[{cx, cy}].push_back(i);
    }

#pragma omp parallel for schedule(static)
    for (int i = 0; i < N; ++i) {
        float xi = pos[2 * i];
        float yi = pos[2 * i + 1];
        int   cx = static_cast<int>(std::floor(xi * invCell));
        int   cy = static_cast<int>(std::floor(yi * invCell));

        float fx_sum = 0.0f;
        float fy_sum = 0.0f;

        for (int dx = -1; dx <= 1; ++dx) {
            for (int dy = -1; dy <= 1; ++dy) {
                auto it = grid.find({cx + dx, cy + dy});
                if (it == grid.end()) continue;

                for (int j : it->second) {
                    if (i == j) continue;
                    float ddx = xi - pos[2 * j];
                    float ddy = yi - pos[2 * j + 1];
                    float dist = std::sqrt(ddx * ddx + ddy * ddy) + eps;
                    if (dist >= R) continue;

                    float overlap = R - dist;
                    float f = sa * overlap;
                    float ux, uy;
                    if (dist < kOverlapDist) {
                        overlapFallbackDirection(i, j, ux, uy);
                    } else {
                        ux = ddx / dist;
                        uy = ddy / dist;
                    }
                    fx_sum += f * ux;
                    fy_sum += f * uy;
                }
            }
        }

        vel[2 * i]     += fx_sum;
        vel[2 * i + 1] += fy_sum;
    }
}

void CollisionForce::applyParallel(float alpha)
{
    const int    N        = m_state->nNodes;
    const float* pos      = m_state->pos.data();
    float*       vel      = m_state->vel.data();
    const float  R        = m_radius;
    const float  sa       = m_strength * alpha;
    const float  eps      = 1e-6f;

#pragma omp parallel for schedule(static)
    for (int i = 0; i < N; ++i) {
        float xi = pos[2 * i];
        float yi = pos[2 * i + 1];
        float fx_sum = 0.0f;
        float fy_sum = 0.0f;

        for (int j = 0; j < N; ++j) {
            if (i == j) continue;
            float dx = xi - pos[2 * j];
            float dy = yi - pos[2 * j + 1];
            float dist = std::sqrt(dx * dx + dy * dy) + eps;
            if (dist >= R) continue;

            float overlap = R - dist;
            float f = sa * overlap;
            float ux, uy;
            if (dist < kOverlapDist) {
                overlapFallbackDirection(i, j, ux, uy);
            } else {
                ux = dx / dist;
                uy = dy / dist;
            }
            fx_sum += f * ux;
            fy_sum += f * uy;
        }

        vel[2 * i]     += fx_sum;
        vel[2 * i + 1] += fy_sum;
    }
}

void CollisionForce::applyBruteForce(float alpha)
{
    const int    N        = m_state->nNodes;
    const float* pos      = m_state->pos.data();
    float*       vel      = m_state->vel.data();
    const float  R        = m_radius;
    const float  sa       = m_strength * alpha;
    const float  eps      = 1e-6f;

    for (int i = 0; i < N; ++i) {
        float xi = pos[2 * i];
        float yi = pos[2 * i + 1];
        float fx_sum = 0.0f;
        float fy_sum = 0.0f;

        for (int j = 0; j < N; ++j) {
            if (i == j) continue;
            float dx = xi - pos[2 * j];
            float dy = yi - pos[2 * j + 1];
            float dist = std::sqrt(dx * dx + dy * dy) + eps;
            if (dist >= R) continue;

            float overlap = R - dist;
            float f = sa * overlap;
            float ux, uy;
            if (dist < kOverlapDist) {
                overlapFallbackDirection(i, j, ux, uy);
            } else {
                ux = dx / dist;
                uy = dy / dist;
            }
            fx_sum += f * ux;
            fy_sum += f * uy;
        }

        vel[2 * i]     += fx_sum;
        vel[2 * i + 1] += fy_sum;
    }
}


