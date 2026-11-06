#pragma once

#include <cmath>

// Exact displacement for a constant force with exponential velocity damping.
// Forces retain their existing per-tick impulse units and terminal velocity.
// The force coefficient tends to dt/2 as damping tends to zero.
struct DampedIntegration
{
    float velocityWeight;
    float forceWeight;

    static DampedIntegration coefficients(float dt, float decay)
    {
        if (decay <= 0.0f) return {0.0f, 0.0f};
        const double loss = 1.0 - static_cast<double>(decay);
        if (loss < 1e-6) return {dt, dt * 0.5f};
        const double weight = loss / -std::log(static_cast<double>(decay));
        return {static_cast<float>(dt * weight),
                static_cast<float>(dt * decay / loss * (1.0 - weight))};
    }
};
