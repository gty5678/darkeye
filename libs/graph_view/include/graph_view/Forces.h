#ifndef FORCES_H
#define FORCES_H

#include "PhysicsState.h"
#include "QuadTree.h"
#include <string>

// ---------------------------------------------------------------------------
// Force – abstract base
// ---------------------------------------------------------------------------
class Force
{
public:
    virtual ~Force() = default;

    virtual void initialize(PhysicsState* state) { m_state = state; }
    virtual void apply(float alpha) = 0;

protected:
    PhysicsState* m_state = nullptr;
};

// ---------------------------------------------------------------------------
// CenterForce – pulls every node toward (cx, cy)   O(N)
// ---------------------------------------------------------------------------
class CenterForce : public Force
{
public:
    CenterForce(float cx = 0.0f, float cy = 0.0f, float strength = 0.1f);

    void apply(float alpha) override;

    void setStrength(float s) { m_strength = s; }
    float strength() const    { return m_strength; }

private:
    float m_cx;
    float m_cy;
    float m_strength;
};

// ---------------------------------------------------------------------------
// LinkForce – spring between connected nodes   O(E)
// ---------------------------------------------------------------------------
class LinkForce : public Force
{
public:
    LinkForce(float k = 0.02f, float distance = 30.0f);

    void apply(float alpha) override;

    void setK(float k)            { m_k = k; }
    float k() const               { return m_k; }
    void setDistance(float d)      { m_distance = d; }
    float distance() const        { return m_distance; }

private:
    float m_k;
    float m_distance;
};

// ---------------------------------------------------------------------------
// ManyBodyForce — pairwise repulsion   O(N^2) or O(N log N) with Barnes-Hut
//   Uses block-tiled for smaller N and Barnes-Hut for larger N.
//   See kBarnesHutThreshold for the switch point.
// ---------------------------------------------------------------------------
class ManyBodyForce : public Force
{
public:
    static constexpr int kBarnesHutThreshold = 1500;

    ManyBodyForce(float strength = 100.0f, float cutoff2 = 40000.0f, float theta = 0.6f);

    void apply(float alpha) override;

    void setStrength(float s) { m_strength = s; }
    float strength() const    { return m_strength; }
    void setTheta(float t)    { m_theta = t; }
    float theta() const       { return m_theta; }

private:
    void applyBlock(float alpha, int blockSize);
    void applyBarnesHut(float alpha);

    float m_strength;
    float m_cutoff2;
    float m_theta;
    QuadTree m_quadTree;
};

// ---------------------------------------------------------------------------
// CollisionForce — point-to-point repulsion when dist < radius
//   Uses brute-force for smaller N and uniform-grid acceleration for larger N.
//   See kGridThreshold for the switch point.
// ---------------------------------------------------------------------------
class CollisionForce : public Force
{
public:
    static constexpr int kGridThreshold = 600;

    CollisionForce(float radius = 10.0f, float strength = 50.0f);

    void apply(float alpha) override;

    void setRadius(float r)      { m_radius = r; }
    float radius() const         { return m_radius; }
    void setStrength(float s)    { m_strength = s; }
    float strength() const        { return m_strength; }

private:
    void applyBruteForce(float alpha);  // O(N^2) serial
    void applyGrid(float alpha);        // O(N) uniform grid acceleration

    float m_radius;
    float m_strength;
};

#endif // FORCES_H


