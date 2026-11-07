#include "GpuSimulation.h"
#include "DampedIntegration.h"
#include "PhysicsState.h"

#include <QByteArray>
#include <QElapsedTimer>
#include <QFile>
#include <rhi/qrhi.h>
#include <algorithm>
#include <array>
#include <cstring>
#include <cmath>
#include <limits>
#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <rhi/qrhi_platform.h>
#include <d3d11.h>
#include <wrl/client.h>
#endif

namespace {
struct alignas(16) Uniforms {
    quint32 meta[4] = {};
    quint32 sorting[4] = {};
    float forces[4] = {};
    float collision[4] = {};
    float integration[4] = {};
    float motion[4] = {};
};
static_assert(sizeof(Uniforms) == 96);
constexpr quint32 kMaxNodes = 1u << 20;
struct Readback {
    QRhiReadbackResult result;
    QElapsedTimer timer;
    bool ready = false;
    double latencyMs = 0.0;
    int step = 0;
};
#ifdef Q_OS_WIN
struct GpuQueries {
    Microsoft::WRL::ComPtr<ID3D11Query> disjoint;
    std::vector<Microsoft::WRL::ComPtr<ID3D11Query>> timestamps;
    std::vector<int> stages;
    int steps = 1;
};
#endif
}

struct GpuSimulation::Impl {
    QRhi* rhi = nullptr;
    quint32 count = 0;
    quint32 padded = 1;
    quint32 stride = 0;
    int front = 0;
    bool uploadInitial = true;
    bool failed = false;
    QString error;
    QByteArray initial;
    QByteArray adjacency;
    std::array<std::unique_ptr<QRhiBuffer>, 2> states;
    std::unique_ptr<QRhiBuffer> controls;
    std::array<std::unique_ptr<QRhiBuffer>, 2> keys;
    std::unique_ptr<QRhiBuffer> histogram;
    std::unique_ptr<QRhiBuffer> tree;
    std::unique_ptr<QRhiBuffer> neighbors;
    std::unique_ptr<QRhiBuffer> uniforms;
    std::array<std::unique_ptr<QRhiShaderResourceBindings>, 4> bindings;
    std::unique_ptr<QRhiComputePipeline> pipeline;
    std::shared_ptr<Readback> readback;
    bool optimized = true, profiling = false;
    int submitted = 0, snapshot = 0;
    Timings timings;
#ifdef Q_OS_WIN
    ID3D11Device* device = nullptr;
    ID3D11DeviceContext* context = nullptr;
    std::vector<std::shared_ptr<GpuQueries>> queries;
#endif

    bool fail(const QString& message, QString* out = nullptr)
    {
        failed = true;
        error = message;
        if (out) *out = message;
        return false;
    }

    void pollQueries()
    {
#ifdef Q_OS_WIN
        for (auto it = queries.begin(); it != queries.end();) {
            const auto& q = *it;
            D3D11_QUERY_DATA_TIMESTAMP_DISJOINT frequency{};
            if (context->GetData(q->disjoint.Get(), &frequency, sizeof(frequency), D3D11_ASYNC_GETDATA_DONOTFLUSH) != S_OK) { ++it; continue; }
            std::vector<UINT64> stamps(q->timestamps.size());
            bool ready = true;
            for (size_t i = 0; i < stamps.size(); ++i)
                ready &= context->GetData(q->timestamps[i].Get(), &stamps[i], sizeof(UINT64), D3D11_ASYNC_GETDATA_DONOTFLUSH) == S_OK;
            if (!ready) { ++it; continue; }
            if (!frequency.Disjoint && frequency.Frequency) {
                double ms[4] = {};
                for (size_t i = 0; i + 1 < stamps.size(); ++i)
                    ms[q->stages[i]] += double(stamps[i + 1] - stamps[i]) * 1000.0 / double(frequency.Frequency) / q->steps;
                timings.gpuBoundsMs = ms[0]; timings.gpuSortMs = ms[1];
                timings.gpuTreeMs = ms[2]; timings.gpuForceMs = ms[3];
            }
            it = queries.erase(it);
        }
#endif
    }

    std::unique_ptr<QRhiBuffer> buffer(QRhiBuffer::Type type, QRhiBuffer::UsageFlags usage, quint32 size)
    {
        std::unique_ptr<QRhiBuffer> b(rhi->newBuffer(type, usage, std::max(16u, size)));
        if (!b->create()) return {};
        return b;
    }
};

GpuSimulation::GpuSimulation() : d(std::make_unique<Impl>()) {}
GpuSimulation::~GpuSimulation() = default;

bool GpuSimulation::initialize(QRhi* rhi, const PhysicsState& state, QString* error, bool optimized)
{
    d = std::make_unique<Impl>();
    d->rhi = rhi;
    d->optimized = optimized;
#ifdef Q_OS_WIN
    if (rhi && rhi->backend() == QRhi::D3D11) {
        const auto* handles = static_cast<const QRhiD3D11NativeHandles*>(rhi->nativeHandles());
        d->device = static_cast<ID3D11Device*>(handles->dev);
        d->context = static_cast<ID3D11DeviceContext*>(handles->context);
    }
#endif
    if (!rhi || !rhi->isFeatureSupported(QRhi::Compute)
        || !rhi->isFeatureSupported(QRhi::ReadBackNonUniformBuffer))
        return d->fail(QStringLiteral("Compute or storage-buffer readback is unavailable"), error);
    if (state.nNodes <= 0 || static_cast<quint32>(state.nNodes) > kMaxNodes)
        return d->fail(QStringLiteral("GPU layout requires 1..1048576 nodes"), error);
    d->count = static_cast<quint32>(state.nNodes);
    while (d->padded < d->count) d->padded <<= 1;
    d->stride = static_cast<quint32>(rhi->ubufAligned(sizeof(Uniforms)));

    // A node gathers its own incident edges; duplicates retain their original
    // multiplicity. No float atomic adds and no concurrent velocity writes.
    std::vector<quint32> offsets(d->count + 1, 0);
    for (int e = 0; e < state.edgeCount(); ++e) {
        const int s = state.edges[2 * e], t = state.edges[2 * e + 1];
        if (s < 0 || t < 0 || s >= state.nNodes || t >= state.nNodes || s == t) continue;
        ++offsets[s + 1];
        ++offsets[t + 1];
    }
    for (quint32 i = 1; i <= d->count; ++i) offsets[i] += offsets[i - 1];
    if ((static_cast<quint64>(offsets.back()) + offsets.size()) * sizeof(quint32)
        > std::numeric_limits<quint32>::max())
        return d->fail(QStringLiteral("GPU adjacency buffer is too large"), error);
    std::vector<quint32> adjacent(offsets.back());
    auto cursor = offsets;
    for (int e = 0; e < state.edgeCount(); ++e) {
        const int s = state.edges[2 * e], t = state.edges[2 * e + 1];
        if (s < 0 || t < 0 || s >= state.nNodes || t >= state.nNodes || s == t) continue;
        adjacent[cursor[s]++] = static_cast<quint32>(t);
        adjacent[cursor[t]++] = static_cast<quint32>(s);
    }
    d->adjacency.append(reinterpret_cast<const char*>(offsets.data()), offsets.size() * sizeof(quint32));
    d->adjacency.append(reinterpret_cast<const char*>(adjacent.data()), adjacent.size() * sizeof(quint32));
    std::vector<float> packed(5 * d->count, 0.0f);
    for (quint32 i = 0; i < d->count; ++i) {
        packed[5 * i] = state.pos[2 * i];
        packed[5 * i + 1] = state.pos[2 * i + 1];
        packed[5 * i + 2] = state.vel[2 * i];
        packed[5 * i + 3] = state.vel[2 * i + 1];
    }
    d->initial = QByteArray(reinterpret_cast<const char*>(packed.data()), packed.size() * sizeof(float));
    for (auto& b : d->states)
        b = d->buffer(QRhiBuffer::Static, QRhiBuffer::StorageBuffer, d->count * 20);
    d->controls = d->buffer(QRhiBuffer::Static, QRhiBuffer::StorageBuffer, d->count * 16);
    for (auto& b : d->keys) b = d->buffer(QRhiBuffer::Static, QRhiBuffer::StorageBuffer, d->padded * 8);
    d->histogram = d->buffer(QRhiBuffer::Static, QRhiBuffer::StorageBuffer, (d->padded + ((d->padded + 127) / 128) * 16) * 4);
    d->tree = d->buffer(QRhiBuffer::Static, QRhiBuffer::StorageBuffer, (2 * d->padded - 1) * 32);
    d->neighbors = d->buffer(QRhiBuffer::Static, QRhiBuffer::StorageBuffer, static_cast<quint32>(d->adjacency.size()));
    // <= 2*20 reductions + 210 bitonic stages + four other stages.
    d->uniforms = d->buffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, d->stride * 256 * maxBatchSteps);
    if (!d->states[0] || !d->states[1] || !d->controls || !d->keys[0] || !d->keys[1] || !d->histogram || !d->tree || !d->neighbors || !d->uniforms)
        return d->fail(QStringLiteral("Could not allocate GPU layout buffers"), error);
    for (int i = 0; i < 2; ++i) for (int k = 0; k < 2; ++k) {
        auto& bindings = d->bindings[i * 2 + k];
        bindings.reset(rhi->newShaderResourceBindings());
        using B = QRhiShaderResourceBinding;
        bindings->setBindings({
            B::uniformBufferWithDynamicOffset(0, B::ComputeStage, d->uniforms.get(), sizeof(Uniforms)),
            B::bufferLoadStore(1, B::ComputeStage, d->states[i].get()),
            B::bufferLoad(2, B::ComputeStage, d->controls.get()),
            B::bufferLoadStore(3, B::ComputeStage, d->keys[k].get()),
            B::bufferLoadStore(4, B::ComputeStage, d->tree.get()),
            B::bufferLoad(5, B::ComputeStage, d->neighbors.get()),
            B::bufferStore(6, B::ComputeStage, d->states[1 - i].get()),
            B::bufferLoadStore(7, B::ComputeStage, d->keys[1 - k].get()),
            B::bufferLoadStore(8, B::ComputeStage, d->histogram.get())
        });
        if (!bindings->create()) return d->fail(QStringLiteral("Could not create GPU layout bindings"), error);
    }
    QFile file(QStringLiteral(":/forceview/shaders/shaders/force_simulation.comp.qsb"));
    if (!file.open(QIODevice::ReadOnly)) return d->fail(QStringLiteral("GPU layout shader is missing"), error);
    const QShader shader = QShader::fromSerialized(file.readAll());
    if (!shader.isValid()) return d->fail(QStringLiteral("GPU layout shader is invalid"), error);
    d->pipeline.reset(rhi->newComputePipeline());
    d->pipeline->setShaderStage({QRhiShaderStage::Compute, shader});
    d->pipeline->setShaderResourceBindings(d->bindings[0].get());
    if (!d->pipeline->create()) return d->fail(QStringLiteral("Could not create GPU layout pipeline"), error);
    return true;
}

bool GpuSimulation::recordStep(QRhiCommandBuffer* cb, const PhysicsState& state, const Parameters& p)
{
    return recordSteps(cb, state, {p});
}

bool GpuSimulation::recordSteps(QRhiCommandBuffer* cb, const PhysicsState& state,
                                const std::vector<Parameters>& steps, bool readback)
{
    if (d->failed || !d->pipeline || (readback && d->readback) || steps.empty() || steps.size() > maxBatchSteps
        || state.nNodes != static_cast<int>(d->count)) return false;
    d->pollQueries();
    QElapsedTimer recordTimer;
    recordTimer.start();
    std::vector<Uniforms> passes;
    for (size_t stepIndex = 0; stepIndex < steps.size(); ++stepIndex) {
        const auto& p = steps[stepIndex];
        Uniforms base;
        base.meta[0] = d->count;
        base.meta[1] = d->padded;
        base.sorting[2] = static_cast<quint32>((d->front + stepIndex) % 2);
        int stage = 0;
        std::copy_n(std::array<float, 4>{p.manyBody, p.center, p.link, p.linkDistance}.data(), 4, base.forces);
        std::copy_n(std::array<float, 4>{p.alpha, p.collisionRadius, p.collisionStrength, p.theta}.data(), 4, base.collision);
        std::copy_n(std::array<float, 4>{p.dt, p.velocityDecay, p.maxDisplacement, p.cutoffSquared}.data(), 4, base.integration);
        const auto weights = DampedIntegration::coefficients(p.dt, p.velocityDecay);
        base.motion[0] = weights.velocityWeight;
        base.motion[1] = weights.forceWeight;
        auto add = [&](quint32 mode, quint32 width = 0, quint32 k = 0, quint32 j = 0) {
            Uniforms u = base;
            u.meta[2] = mode;
            u.meta[3] = width;
            u.sorting[0] = k;
            u.sorting[1] = j;
            u.sorting[3] = stage;
            passes.push_back(u);
        };
        auto reduce = [&]() {
            for (quint32 width = d->padded / (d->optimized ? 256 : 2); width; width >>= 1) add(1, width);
        };
        add(0, d->optimized ? 1 : 0);
        reduce(); // bounds of unsorted particles, entirely on GPU
        stage = 1;
        add(2);
        if (d->optimized) {
            for (quint32 shift = 0; shift < 32; shift += 4) {
                add(6, 0, shift, (d->padded + 127) / 128);
                add(7, 0, shift, (d->padded + 127) / 128);
                add(8, 0, shift, (d->padded + 127) / 128);
            }
        } else {
            for (quint32 k = 2; k <= d->padded; k <<= 1)
                for (quint32 j = k / 2; j; j >>= 1) add(3, 0, k, j);
        }
        stage = 2;
        add(4, d->optimized ? 1 : 0);
        reduce(); // spatial hierarchy mass/COM and exact AABBs
        stage = 3;
        add(5);
    }
    QByteArray uniforms(static_cast<qsizetype>(passes.size() * d->stride), '\0');
    for (size_t i = 0; i < passes.size(); ++i)
        std::memcpy(uniforms.data() + i * d->stride, &passes[i], sizeof(Uniforms));
    std::vector<float> controls(4 * d->count);
    for (quint32 i = 0; i < d->count; ++i) {
        controls[4 * i] = state.dragPos[2 * i];
        controls[4 * i + 1] = state.dragPos[2 * i + 1];
        controls[4 * i + 2] = state.dragging[i] ? 1.0f : 0.0f;
        controls[4 * i + 3] = state.mass[i];
    }
    auto* updates = d->rhi->nextResourceUpdateBatch();
    updates->updateDynamicBuffer(d->uniforms.get(), 0, static_cast<quint32>(uniforms.size()), uniforms.constData());
    updates->uploadStaticBuffer(d->controls.get(), controls.data());
    if (d->uploadInitial) {
        updates->uploadStaticBuffer(d->states[d->front].get(), d->initial.constData());
        updates->uploadStaticBuffer(d->neighbors.get(), d->adjacency.constData());
        d->uploadInitial = false;
        d->initial.clear();
        d->adjacency.clear();
    }
#ifdef Q_OS_WIN
    std::shared_ptr<GpuQueries> queries;
    if (d->profiling && d->device && d->queries.size() < 4) {
        queries = std::make_shared<GpuQueries>();
        queries->steps = static_cast<int>(steps.size());
        D3D11_QUERY_DESC desc{D3D11_QUERY_TIMESTAMP_DISJOINT, 0};
        if (FAILED(d->device->CreateQuery(&desc, &queries->disjoint))) queries.reset();
    }
    auto timestamp = [&](int stage) {
        if (!queries) return;
        Microsoft::WRL::ComPtr<ID3D11Query> query;
        const D3D11_QUERY_DESC desc{D3D11_QUERY_TIMESTAMP, 0};
        if (FAILED(d->device->CreateQuery(&desc, &query))) { queries.reset(); return; }
        cb->beginExternal(); // flush recorded D3D11 commands before timestamp
        if (queries->timestamps.empty()) d->context->Begin(queries->disjoint.Get());
        d->context->End(query.Get());
        if (stage == -1) d->context->End(queries->disjoint.Get());
        cb->endExternal();
        queries->timestamps.push_back(query);
        queries->stages.push_back(stage);
    };
#endif
    for (size_t i = 0; i < passes.size(); ++i) {
        cb->beginComputePass(i == 0 ? updates : nullptr);
#ifdef Q_OS_WIN
        if (i == 0 || passes[i].sorting[3] != passes[i - 1].sorting[3]) timestamp(static_cast<int>(passes[i].sorting[3]));
#endif
        cb->setComputePipeline(d->pipeline.get());
        const QRhiCommandBuffer::DynamicOffset offset{0, static_cast<quint32>(i * d->stride)};
        const quint32 mode = passes[i].meta[2];
        const quint32 keyIndex = mode >= 6 && mode <= 8 ? (passes[i].sorting[0] / 4) % 2 : 0;
        cb->setShaderResources(d->bindings[passes[i].sorting[2] * 2 + keyIndex].get(), 1, &offset);
        const quint32 count = mode == 1 ? passes[i].meta[3] : (mode == 7 ? 128 : d->padded);
        cb->dispatch((count + 127) / 128, 1, 1);
        cb->endComputePass(); // inter-dispatch storage barriers are owned by QRhi
    }
#ifdef Q_OS_WIN
    if (queries) {
        cb->beginComputePass(); timestamp(-1); cb->endComputePass();
        if (queries) d->queries.push_back(queries);
    }
#endif
    d->timings.cpuRecordMs = recordTimer.nsecsElapsed() / 1e6 / steps.size();
    d->timings.dispatches = static_cast<int>(passes.size() / steps.size());
    d->timings.steps = static_cast<int>(steps.size());
    d->front = (d->front + static_cast<int>(steps.size())) % 2;
    d->submitted += static_cast<int>(steps.size());
    if (readback) recordSnapshot(cb);
    return true;
}

bool GpuSimulation::recordSnapshot(QRhiCommandBuffer* cb)
{
    if (d->failed || d->readback || d->uploadInitial) return false;
    auto readback = std::make_shared<Readback>();
    readback->step = d->submitted;
    readback->timer.start();
    // The result must outlive this engine if a graph/device is replaced while
    // work is in flight. Its callback owns it until QRhi completes the readback.
    readback->result.completed = [readback]() {
        auto keepAlive = readback;
        keepAlive->latencyMs = keepAlive->timer.nsecsElapsed() / 1e6;
        keepAlive->ready = true;
        keepAlive->result.completed = nullptr;
    };
    d->readback = readback;
    auto* read = d->rhi->nextResourceUpdateBatch();
    read->readBackBuffer(d->states[d->front].get(), 0, d->count * 20, &readback->result);
    cb->resourceUpdate(read);
    return true;
}

void GpuSimulation::recordInitialState(QRhiCommandBuffer* cb)
{
    if (!d->pipeline || !d->uploadInitial) return;
    auto* updates = d->rhi->nextResourceUpdateBatch();
    updates->uploadStaticBuffer(d->states[d->front].get(), d->initial.constData());
    updates->uploadStaticBuffer(d->neighbors.get(), d->adjacency.constData());
    cb->resourceUpdate(updates);
    d->uploadInitial = false;
    d->initial.clear(); d->adjacency.clear();
}

bool GpuSimulation::hasPendingStep() const { return static_cast<bool>(d->readback); }
int GpuSimulation::submittedSteps() const { return d->submitted; }
int GpuSimulation::snapshotSteps() const { return d->snapshot; }
QRhiBuffer* GpuSimulation::stateBuffer() const { return d->pipeline && !d->uploadInitial ? d->states[d->front].get() : nullptr; }
bool GpuSimulation::failed() const { return d->failed; }
QString GpuSimulation::errorString() const { return d->error; }
void GpuSimulation::setProfilingEnabled(bool enabled) { d->profiling = enabled; }
GpuSimulation::Timings GpuSimulation::timings() const {
    auto result = d->timings;
    if (!d->profiling) result.gpuBoundsMs = result.gpuSortMs = result.gpuTreeMs = result.gpuForceMs = -1;
    return result;
}

bool GpuSimulation::takeResult(std::vector<float>& state, double* latencyMs, float* totalSpeed, float* peakSpeed)
{
    d->pollQueries();
    if (!d->readback || !d->readback->ready) return false;
    auto result = std::move(d->readback);
    d->snapshot = result->step;
    if (result->result.data.size() != static_cast<qsizetype>(d->count * 20)) {
        d->fail(QStringLiteral("GPU layout readback failed"));
        return false;
    }
    state.resize(4 * d->count);
    float speed = 0.0f, peak = 0.0f;
    for (quint32 i = 0; i < d->count; ++i) {
        const char* source = result->result.data.constData() + i * 20;
        std::memcpy(state.data() + 4 * i, source, 16);
        float nodeSpeed;
        std::memcpy(&nodeSpeed, source + 16, sizeof(float));
        speed += nodeSpeed;
        peak = std::max(peak, nodeSpeed);
    }
    if (totalSpeed) *totalSpeed = speed;
    if (peakSpeed) *peakSpeed = peak;
    if (latencyMs) *latencyMs = result->latencyMs;
    d->timings.readbackLatencyMs = result->latencyMs;
    if (!std::isfinite(speed) || !std::all_of(state.begin(), state.end(), [](float v) { return std::isfinite(v); })) {
        d->fail(QStringLiteral("GPU layout produced non-finite state"));
        return false;
    }
    return true;
}
