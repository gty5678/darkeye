#include "Simulation.h"
#include "DampedIntegration.h"
#include <cmath>
#include <algorithm>

// =====================================================================
// Construction
// =====================================================================
Simulation::Simulation(PhysicsState* state)
    : m_state(state)
{}

// =====================================================================
// Force management
// =====================================================================
void Simulation::addForce(const std::string& name, std::unique_ptr<Force> force)
{
    force->initialize(m_state);
    m_forces[name] = std::move(force);
}

void Simulation::removeForce(const std::string& name)
{
    m_forces.erase(name);
}

Force* Simulation::getForce(const std::string& name) const
{
    auto it = m_forces.find(name);
    return (it != m_forces.end()) ? it->second.get() : nullptr;
}

// =====================================================================
// Tick — single simulation step
// =====================================================================
void Simulation::tick()
{
    bool anyDragging = false;
    if (!m_state->dragging.empty()) {
        for (uint8_t d : m_state->dragging) {
            if (d) { anyDragging = true; break; }
        }
    }
    if (anyDragging) {
        m_alpha = 1.0f;
        m_active = true;
    }
    if (!m_active) return;

    if (!m_state->dragPos.empty()) {
        const int N = m_state->nNodes;
        float* pos = m_state->pos.data();
        float* vel = m_state->vel.data();
        const float* dpos = m_state->dragPos.data();
        const auto& drag = m_state->dragging;
        for (int i = 0; i < N; ++i) {
            if (drag[i]) {
                pos[2 * i]     = dpos[2 * i];
                pos[2 * i + 1] = dpos[2 * i + 1];
                vel[2 * i]     = 0.0f;
                vel[2 * i + 1] = 0.0f;
            }
        }
    }

    // Preserve incoming velocity; force kernels accumulate impulses into vel.
    m_previousVelocity = m_state->vel;
    std::fill(m_state->vel.begin(), m_state->vel.end(), 0.0f);

    // Apply each force (modifies vel)
    for (auto& [name, force] : m_forces) {
        force->apply(m_alpha);
    }

    // Integrate velocities -> positions
    float totalSpeed = integrate();

    completeGpuTick(totalSpeed, m_peakSpeed);
}

bool Simulation::prepareGpuTick()
{
    const bool dragging = std::any_of(m_state->dragging.begin(), m_state->dragging.end(),
                                      [](uint8_t d) { return d != 0; });
    if (dragging) {
        m_alpha = 1.0f;
        m_active = true;
    }
    return m_active;
}

std::vector<float> Simulation::gpuTickAlphas(int maximumTicks) const
{
    std::vector<float> alphas;
    const bool dragging = std::any_of(m_state->dragging.begin(), m_state->dragging.end(),
                                      [](uint8_t d) { return d != 0; });
    float alpha = dragging ? 1.0f : m_alpha;
    for (int step = 0; step < maximumTicks && (m_active || dragging); ++step) {
        alphas.push_back(alpha);
        if (!dragging && m_firstStarted && m_tickCount + step + 1 >= m_cooldownDelay)
            alpha = std::max(m_alphaMin, alpha * (1.0f - m_alphaDecay));
    }
    return alphas;
}

void Simulation::completeGpuTick(float totalSpeed, float peakSpeed)
{
    const bool dragging = std::any_of(m_state->dragging.begin(), m_state->dragging.end(),
                                     [](uint8_t d) { return d != 0; });
    ++m_tickCount;
    if (!dragging && m_firstStarted && m_tickCount >= m_cooldownDelay)
        m_alpha = std::max(m_alphaMin, m_alpha * (1.0f - m_alphaDecay));
    // GPU batches advance the clock without inventing a motion observation.
    if (std::isfinite(totalSpeed)) considerGpuConvergence(totalSpeed, 1, peakSpeed);
}

void Simulation::considerGpuConvergence(float totalSpeed, int elapsedSteps, float peakSpeed)
{
    if (!std::isfinite(totalSpeed) || totalSpeed < 0.0f || elapsedSteps <= 0) return;
    const bool dragging = std::any_of(m_state->dragging.begin(), m_state->dragging.end(),
                                     [](uint8_t d) { return d != 0; });
    if (dragging) {
        m_motionSpeed = -1.0f;
        m_settledSteps = m_settledSamples = 0;
        m_alphaDecay = 0.005f;
        return;
    }
    const float average = totalSpeed / std::max(1, m_state->nNodes);
    // Observations can span batches. Bound the filter update to avoid treating
    // one quiet final sample as proof that every preceding step was quiet.
    const float previous = m_motionSpeed;
    const float retention = std::pow(0.75f, float(std::min(elapsedSteps, 8)));
    m_motionSpeed = m_motionSpeed < 0.0f ? average
        : retention * m_motionSpeed + (1.0f - retention) * average;
    const float movement = m_motionSpeed * m_dt;
    // While motion is already decaying, let the forces finish the relaxation.
    // Cooling during that phase would make a distant node creep toward its
    // equilibrium at minimum heat. Cool on plateaus/oscillation and when quiet.
    const bool relaxing = previous > 0.01f && average > 0.01f
        && average < previous * std::pow(0.998f, float(elapsedSteps));
    m_alphaDecay = relaxing ? 0.0f : 0.005f + 0.015f / (1.0f + movement / 0.1f);
    const float peak = peakSpeed >= 0.0f ? peakSpeed : average;
    // Reaching the alpha floor never freezes a moving graph. Check the peak
    // too, so a few moving nodes cannot disappear into a large graph's mean.
    if (m_tickCount >= m_cooldownDelay && m_alpha < 0.01f
        && m_motionSpeed < 0.01f && peak < 0.1f) {
        m_settledSteps += elapsedSteps;
        ++m_settledSamples;
        if (m_settledSteps >= 32 && m_settledSamples >= 3) m_active = false;
    } else {
        m_settledSteps = m_settledSamples = 0;
    }
}

// =====================================================================
// Integrate — exact damped displacement and smooth displacement saturation
// =====================================================================
float Simulation::integrate()
{
    const int   N       = m_state->nNodes;
    float*      pos     = m_state->pos.data();
    float*      vel     = m_state->vel.data();
    const auto& drag    = m_state->dragging;
    const float decay   = m_velocityDecay;
    const float dt      = m_dt;
    const float maxDisp = m_maxDisp;

    const auto weights = DampedIntegration::coefficients(dt, decay);
    float totalSpeed = 0.0f;
    m_peakSpeed = 0.0f;
    for (int i = 0; i < N; ++i) {
        if (drag[i]) {
            vel[2 * i] = vel[2 * i + 1] = 0.0f;
            continue;
        }
        const float fx = vel[2 * i], fy = vel[2 * i + 1];
        const float oldX = m_previousVelocity[2 * i];
        const float oldY = m_previousVelocity[2 * i + 1];
        const float vx = (oldX + fx) * decay;
        const float vy = (oldY + fy) * decay;
        const float dx = oldX * weights.velocityWeight + fx * weights.forceWeight;
        const float dy = oldY * weights.velocityWeight + fy * weights.forceWeight;
        const float speed = std::fabs(vx) + std::fabs(vy);
        totalSpeed += speed;
        m_peakSpeed = std::max(m_peakSpeed, speed);

        // Smooth saturation has no abrupt transition at the displacement cap.
        const float travel2 = std::max(dx * dx + dy * dy, (vx * vx + vy * vy) * dt * dt);
        const float scale = maxDisp > 0.0f
            ? 1.0f / std::sqrt(1.0f + travel2 / (maxDisp * maxDisp)) : 0.0f;
        vel[2 * i] = vx * scale;
        vel[2 * i + 1] = vy * scale;
        pos[2 * i] += dx * scale;
        pos[2 * i + 1] += dy * scale;
    }

    return totalSpeed;
}

// =====================================================================
// Lifecycle
// =====================================================================
void Simulation::start()
{
    m_active = true;
    if (!m_firstStarted) {
        m_firstStarted = true;
    }
}

void Simulation::stop()
{
    m_active = false;
}

void Simulation::pause()
{
    m_active = false;
}

void Simulation::resume()
{
    if (m_firstStarted) {
        m_active = true;
        m_settledSteps = m_settledSamples = 0;
    }
}

void Simulation::restart()
{
    m_firstStarted = true;
    m_alpha     = 1.0f;
    m_tickCount = 0;
    m_motionSpeed = -1.0f;
    m_alphaDecay = 0.02f;
    m_settledSteps = m_settledSamples = 0;
    m_active    = true;
}


