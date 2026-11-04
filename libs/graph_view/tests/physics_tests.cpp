#include "Forces.h"
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


