#pragma once

#include <cmath>

// Only the initial layout runs without a cadence limit. Later interaction
// always advances one step per display interval, regardless of device speed.
class GpuSimulationSchedule
{
public:
    static constexpr int maxBatchSteps = 32;
    static constexpr int normalIntervalMs = 16;

    static int batchSize(bool dragging, bool initialLayout = false)
    {
        return initialLayout && !dragging ? maxBatchSteps : 1;
    }
    static int timerInterval(bool dragging, bool initialLayout)
    {
        return initialLayout && !dragging ? 0 : normalIntervalMs;
    }
    double stepMs() const { return m_stepMs; }
    void reset() { m_stepMs = 0.0; }
    void observe(double completedMs, int steps)
    {
        if (!std::isfinite(completedMs) || completedMs <= 0.0 || steps <= 0) return;
        const double sample = completedMs / steps;
        m_stepMs = m_stepMs == 0.0 ? sample : m_stepMs * 0.75 + sample * 0.25;
    }
private:
    double m_stepMs = 0.0;
};
