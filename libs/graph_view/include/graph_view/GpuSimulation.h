#pragma once

#include <memory>
#include <vector>
#include <QString>

class QRhi;
class QRhiCommandBuffer;
class QRhiBuffer;
struct PhysicsState;

// All spatial construction and physics execute in compute passes. CPU owns only
// topology/parameters and consumes asynchronous snapshots for the existing view.
class GpuSimulation
{
public:
    struct Parameters {
        float manyBody = 10000.0f;
        float center = 0.01f;
        float link = 0.3f;
        float linkDistance = 30.0f;
        float collisionRadius = 10.0f;
        float collisionStrength = 50.0f;
        float theta = 0.6f;
        float alpha = 1.0f;
        float dt = 0.1f;
        float velocityDecay = 0.75f;
        float maxDisplacement = 15.0f;
        float cutoffSquared = 40000.0f;
    };
    static constexpr int maxBatchSteps = 32;
    GpuSimulation();
    ~GpuSimulation();
    GpuSimulation(const GpuSimulation&) = delete;
    GpuSimulation& operator=(const GpuSimulation&) = delete;

    bool initialize(QRhi* rhi, const PhysicsState& state, QString* error = nullptr, bool optimized = true);
    struct Timings {
        double cpuRecordMs = 0;
        double readbackLatencyMs = 0;
        double gpuBoundsMs = -1, gpuSortMs = -1, gpuTreeMs = -1, gpuForceMs = -1;
        int dispatches = 0, steps = 0;
    };
    void setProfilingEnabled(bool enabled);
    Timings timings() const;
    bool recordStep(QRhiCommandBuffer* cb, const PhysicsState& controls, const Parameters& parameters);
    // Up to maxBatchSteps resident steps share one upload and readback.
    bool recordSteps(QRhiCommandBuffer* cb, const PhysicsState& controls,
                     const std::vector<Parameters>& steps, bool readback = true);
    bool recordSnapshot(QRhiCommandBuffer* cb);
    void recordInitialState(QRhiCommandBuffer* cb);
    int submittedSteps() const;
    int snapshotSteps() const;
    QRhiBuffer* stateBuffer() const;
    bool hasPendingStep() const;
    // Returns interleaved x,y,vx,vy for one completed step; never blocks.
    bool takeResult(std::vector<float>& state, double* latencyMs = nullptr, float* totalSpeed = nullptr,
                    float* peakSpeed = nullptr);
    bool failed() const;
    QString errorString() const;

private:
    struct Impl;
    std::unique_ptr<Impl> d;
};
