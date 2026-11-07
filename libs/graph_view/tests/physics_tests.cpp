#include "Forces.h"
#include "Simulation.h"
#include "DampedIntegration.h"
#include "PhysicsState.h"

#include <atomic>
#include <cmath>
#include <iostream>
#include <thread>
#include <vector>

namespace {

bool nearlyEqual(float lhs, float rhs, float tolerance = 1e-4f)
{
    return std::abs(lhs - rhs) <= tolerance;
}

bool testSmoothDampedMotion()
{
    PhysicsState state;
    state.init(1, {});
    state.pos = {100.0f, 0.0f};
    Simulation sim(&state);
    sim.addForce("center", std::make_unique<CenterForce>(0, 0, 0.1f));
    sim.start();
    sim.tick();
    // Analytic solution of v' = drive - friction*v for a constant force.
    const double friction = -std::log(0.75) / 0.1;
    const double expectedTravel = -30.0 * (0.1 - 0.25 / friction);
    const double scale = 1.0 / std::sqrt(1.0 + 0.75 * 0.75 / 225.0);
    if (!nearlyEqual(state.pos[0], 100.0f + expectedTravel * scale)
        || !(state.pos[0] > 99.25f)) {
        std::cerr << "damped integration did not soften the first impulse\n";
        return false;
    }
    for (int i = 0; i < 1000 && sim.isActive(); ++i) sim.tick();
    if (!std::isfinite(state.pos[0]) || std::abs(state.pos[0]) > 0.5f) {
        std::cerr << "damped center motion did not settle\n";
        return false;
    }
    const auto undamped = DampedIntegration::coefficients(0.1f, 1.0f);
    const auto stopped = DampedIntegration::coefficients(0.1f, 0.0f);
    return nearlyEqual(undamped.forceWeight, 0.05f)
        && nearlyEqual(stopped.forceWeight, 0.0f);
}

bool testPinnedNodesHaveNoResidualVelocity()
{
    PhysicsState state;
    state.init(1, {});
    state.dragging[0] = 1;
    state.setDragPos(0, 100.0f, -20.0f);
    state.vel = {1000.0f, -1000.0f};
    Simulation sim(&state);
    sim.addForce("center", std::make_unique<CenterForce>(0, 0, 10000.0f));
    sim.start();
    for (int i = 0; i < 10; ++i) sim.tick();
    if (state.pos != state.dragPos || state.vel != std::vector<float>({0, 0})) {
        std::cerr << "pinned node retained force-induced velocity\n";
        return false;
    }
    state.dragging[0] = 0;
    sim.removeForce("center");
    sim.tick();
    return state.pos == state.dragPos && state.vel == std::vector<float>({0, 0});
}

bool testBarnesHutDoesNotIncludeSelf()
{
    PhysicsState state;
    state.init(ManyBodyForce::kBarnesHutThreshold, {});
    state.mass[0] = 100.0f;
    float expected = 0.0f;
    for (int i = 1; i < state.nNodes; ++i) {
        const float x = 10.0f + i * 0.001f;
        state.pos[2 * i] = x;
        expected -= 100.0f / (x * x);
    }
    ManyBodyForce force(1.0f, 40000.0f, 100.0f);
    force.initialize(&state);
    force.apply(1.0f);
    // Distant descendants may still be approximated, so allow their small error.
    if (std::abs(state.vel[0] - expected) > std::abs(expected) * 0.01f) {
        std::cerr << "Barnes-Hut included self in an aggregate\n";
        return false;
    }
    return true;
}

bool testMotionControlsCoolingAndStopping()
{
    PhysicsState movingState, quietState;
    movingState.init(10000, {});
    quietState.init(10000, {});
    Simulation moving(&movingState), quiet(&quietState);
    moving.start(); quiet.start();
    for (int i = 0; i < 200; ++i) {
        moving.completeGpuTick(1000000.0f, 100.0f);
        quiet.completeGpuTick(0.0f, 0.0f);
    }
    if (!(moving.alpha() > quiet.alpha())) return false;
    for (int i = 0; i < 2000; ++i) moving.completeGpuTick(1000000.0f, 100.0f);
    if (!moving.isActive()) return false; // heat alone must not freeze motion
    moving.pause();
    moving.resume();
    if (!moving.isActive()) return false; // minimum heat must still be resumable
    // One moving node must not be hidden by the other 9999 stationary nodes.
    for (int i = 0; i < 100; ++i) moving.completeGpuTick(1.0f, 1.0f);
    if (!moving.isActive()) return false;
    for (int i = 0; i < 200 && moving.isActive(); ++i) moving.completeGpuTick(0.0f, 0.0f);
    return !moving.isActive();
}

bool testInvalidEdgesAreRejected()
{
    PhysicsState state;
    state.init(3, {0, 1, -1, 2, 2, 3, 1, 1, 2, 0, 99});
    const std::vector<int> expected = {0, 1, 2, 0};
    if (state.edges != expected) {
        std::cerr << "invalid edge filtering failed\n";
        return false;
    }
    return true;
}

bool testOverlappingCollisionIsSymmetric()
{
    PhysicsState state;
    state.init(2, {});
    state.pos = {0.0f, 0.0f, 0.0f, 0.0f};

    CollisionForce collision(10.0f, 50.0f);
    collision.initialize(&state);
    collision.apply(1.0f);

    const float sumX = state.vel[0] + state.vel[2];
    const float sumY = state.vel[1] + state.vel[3];
    const float speed0 = std::hypot(state.vel[0], state.vel[1]);
    const float speed1 = std::hypot(state.vel[2], state.vel[3]);
    if (!nearlyEqual(sumX, 0.0f) || !nearlyEqual(sumY, 0.0f)
        || !nearlyEqual(speed0, speed1) || !std::isfinite(speed0)
        || speed0 > 501.0f) {
        std::cerr << "overlapping collision is not symmetric\n";
        return false;
    }
    return true;
}

bool testRenderSnapshotsStayConsistent()
{
    PhysicsState state;
    state.init(1, {});
    state.syncRenderPosFromPos();
    std::atomic<bool> finished{false};
    std::atomic<bool> consistent{true};

    std::thread reader([&]() {
        std::vector<float> snapshot;
        while (!finished.load(std::memory_order_acquire)) {
            state.copyRenderPos(snapshot);
            if (snapshot.size() != 2 || snapshot[0] != snapshot[1]) {
                consistent.store(false, std::memory_order_release);
                return;
            }
        }
    });
    for (int i = 1; i <= 10000; ++i) {
        const float value = static_cast<float>(i);
        state.pos[0] = value;
        state.pos[1] = value;
        state.publishRenderPos();
    }
    finished.store(true, std::memory_order_release);
    reader.join();

    if (!consistent.load(std::memory_order_acquire)) {
        std::cerr << "render position snapshot was torn\n";
        return false;
    }
    return true;
}

bool testBarnesHutCutoffBoundary()
{
    PhysicsState state;
    state.init(ManyBodyForce::kBarnesHutThreshold, {});
    for (int i = 1; i < state.nNodes; ++i) {
        state.pos[2 * i] = i == 1 ? 190.0f : 210.0f + i * 0.001f;
        state.pos[2 * i + 1] = i == 1 ? 0.0f : (i % 17) * 0.001f;
    }
    state.pos[0] = state.pos[1] = 0.0f;
    ManyBodyForce force(100.0f, 40000.0f);
    force.initialize(&state);
    force.apply(1.0f);
    const float expected = -100.0f / (190.0f * 190.0f);
    if (!nearlyEqual(state.vel[0], expected, 1e-7f)
        || !nearlyEqual(state.vel[1], 0.0f, 1e-7f)) {
        std::cerr << "Barnes-Hut lost a neighbor inside the cutoff\n";
        return false;
    }
    return true;
}

} // namespace

int main()
{
    if (!testMotionControlsCoolingAndStopping())
        return 1;
    if (!testSmoothDampedMotion() || !testPinnedNodesHaveNoResidualVelocity()
        || !testBarnesHutDoesNotIncludeSelf())
        return 1;
    if (!testBarnesHutCutoffBoundary())
        return 1;
    if (!testInvalidEdgesAreRejected())
        return 1;
    if (!testOverlappingCollisionIsSymmetric())
        return 1;
    if (!testRenderSnapshotsStayConsistent())
        return 1;
    return 0;
}


