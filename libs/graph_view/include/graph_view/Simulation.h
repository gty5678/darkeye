#ifndef SIMULATION_H
#define SIMULATION_H

#include "PhysicsState.h"
#include "Forces.h"

#include <string>
#include <memory>
#include <unordered_map>
#include <vector>

/**
 * Simulation — force-directed graph simulation engine.
 *
 * Owns Forces; mutates the PhysicsState (pos, vel) each tick().
 * Mirrors Python Simulation class from simulation_worker.py.
 */
class Simulation
{
public:
    explicit Simulation(PhysicsState* state);

    // Force management
    void addForce(const std::string& name, std::unique_ptr<Force> force);
    void removeForce(const std::string& name);
    Force* getForce(const std::string& name) const;

    // Single simulation step
    void tick();
    // GPU execution shares the same cooling/lifecycle without running CPU forces.
    bool prepareGpuTick();
    std::vector<float> gpuTickAlphas(int maximumTicks) const;
    void completeGpuTick(float totalSpeed, float peakSpeed = -1.0f);
    void considerGpuConvergence(float totalSpeed, int elapsedSteps = 1, float peakSpeed = -1.0f);

    // Lifecycle
    void start();
    void stop();
    void pause();
    void resume();
    void restart();
    bool isActive() const { return m_active; }

    // Read-only access
    float alpha() const { return m_alpha; }
    int tickCount() const { return m_tickCount; }

private:
    /** Integrate constant per-step forces with exponential damping and a smooth cap.
     *  Returns total absolute speed (used for early-stop heuristic). */
    float integrate();

    PhysicsState* m_state;
    std::unordered_map<std::string, std::unique_ptr<Force>> m_forces;

    // Alpha (cooling)
    float m_alpha        = 1.0f;
    float m_alphaDecay   = 0.02f;
    float m_motionSpeed = -1.0f;
    float m_peakSpeed = 0.0f;
    int m_settledSteps = 0;
    int m_settledSamples = 0;
    float m_alphaMin     = 0.001f;

    // Exact exponential damping with smooth displacement saturation.
    std::vector<float> m_previousVelocity;

    // Integration
    float m_velocityDecay = 0.75f;
    float m_dt            = 0.1f;
    float m_maxDisp       = 15.0f;

    // State
    bool  m_active         = false;
    bool  m_firstStarted   = false;
    int   m_tickCount      = 0;
    int   m_cooldownDelay  = 32;
};

#endif // SIMULATION_H


