#include "GpuGraphGeometry.h"
#include <QByteArray>
#include <QFile>
#include <rhi/qrhi.h>
#include <algorithm>
#include <cstring>
#include <limits>
struct GpuGraphGeometry::Impl {
    QRhi* rhi = nullptr;
    quint32 capacity = 0;
    quint64 revision = std::numeric_limits<quint64>::max();
    std::unique_ptr<QRhiBuffer> descriptions, instances, uniform;
    std::unique_ptr<QRhiShaderResourceBindings> bindings;
    std::unique_ptr<QRhiComputePipeline> pipeline;
    quint32 textCapacity = 0;
    std::unique_ptr<QRhiBuffer> textSource, textOutput, textUniform;
    std::unique_ptr<QRhiShaderResourceBindings> textBindings;
    std::unique_ptr<QRhiComputePipeline> textPipeline;
};
GpuGraphGeometry::GpuGraphGeometry() : d(std::make_unique<Impl>()) {}
GpuGraphGeometry::~GpuGraphGeometry() = default;
QRhiBuffer* GpuGraphGeometry::instanceBuffer() const { return d->instances.get(); }
bool GpuGraphGeometry::record(QRhi* rhi, QRhiCommandBuffer* cb, QRhiBuffer* state,
                              const std::vector<Instance>& descriptions, const float* viewport, quint64 revision,
                              const HoverStyle* hover)
{
    static_assert(sizeof(Instance) == 48);
    if (!state || descriptions.empty() || !rhi->isFeatureSupported(QRhi::Instancing)) return false;
    if (d->rhi != rhi) { d = std::make_unique<Impl>(); d->rhi = rhi; }
    const quint32 bytes = static_cast<quint32>(descriptions.size() * sizeof(Instance));
    if (!d->instances || bytes > d->capacity) {
        d->pipeline.reset();
        d->bindings.reset();
        d->capacity = (bytes + 65535u) & ~65535u;
        d->descriptions.reset(rhi->newBuffer(QRhiBuffer::Static, QRhiBuffer::StorageBuffer, d->capacity));
        d->instances.reset(rhi->newBuffer(QRhiBuffer::Static, QRhiBuffer::StorageBuffer | QRhiBuffer::VertexBuffer, d->capacity));
        if (!d->descriptions->create() || !d->instances->create()) return false;
        d->revision = std::numeric_limits<quint64>::max();
    }
    if (!d->uniform) {
        d->uniform.reset(rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, 96));
        if (!d->uniform->create()) return false;
    }
    if (!d->bindings) d->bindings.reset(rhi->newShaderResourceBindings());
    using B = QRhiShaderResourceBinding;
    d->bindings->setBindings({B::uniformBuffer(0, B::ComputeStage, d->uniform.get()),
        B::bufferLoad(1, B::ComputeStage, state), B::bufferLoad(2, B::ComputeStage, d->descriptions.get()),
        B::bufferStore(3, B::ComputeStage, d->instances.get())});
    if (!d->bindings->create()) return false;
    if (!d->pipeline) {
        QFile file(":/forceview/shaders/shaders/force_instances.comp.qsb");
        if (!file.open(QIODevice::ReadOnly)) return false;
        d->pipeline.reset(rhi->newComputePipeline());
        d->pipeline->setShaderResourceBindings(d->bindings.get());
        d->pipeline->setShaderStage({QRhiShaderStage::Compute, QShader::fromSerialized(file.readAll())});
        if (!d->pipeline->create()) { d->pipeline.reset(); return false; }
    }
    auto* updates = rhi->nextResourceUpdateBatch();
    if (revision != d->revision) {
        updates->uploadStaticBuffer(d->descriptions.get(), 0, bytes, descriptions.data());
        d->revision = revision;
    }
    struct alignas(16) Params {
        float viewport[4];
        quint32 counts[4] = {};
        float animation[4] = {};
        float dimColor[4] = {}, edgeDimColor[4] = {}, highlightColor[4] = {};
    } params;
    static_assert(sizeof(Params) == 96);
    std::copy_n(viewport, 4, params.viewport);
    params.counts[0] = static_cast<quint32>(descriptions.size());
    if (hover) {
        params.animation[0] = hover->amount;
        params.animation[1] = hover->radiusScale;
        std::copy_n(hover->dimColor, 4, params.dimColor);
        std::copy_n(hover->edgeDimColor, 4, params.edgeDimColor);
        std::copy_n(hover->highlightColor, 4, params.highlightColor);
    }
    updates->updateDynamicBuffer(d->uniform.get(), 0, sizeof(params), &params);
    cb->beginComputePass(updates);
    cb->setComputePipeline(d->pipeline.get());
    cb->setShaderResources(d->bindings.get());
    cb->dispatch((params.counts[0] + 127) / 128, 1, 1);
    cb->endComputePass();
    return true;
}

QRhiBuffer* GpuGraphGeometry::textBuffer() const { return d->textOutput.get(); }
bool GpuGraphGeometry::recordText(QRhiCommandBuffer* cb, QRhiBuffer* state, const float* vertices, quint32 count,
                                 const float* screen)
{
    if (!d->rhi || !state || !count) return false;
    const quint32 bytes = count * 48;
    if (!d->textOutput || bytes > d->textCapacity) {
        d->textPipeline.reset(); d->textBindings.reset();
        d->textCapacity = (bytes + 65535u) & ~65535u;
        d->textSource.reset(d->rhi->newBuffer(QRhiBuffer::Static, QRhiBuffer::StorageBuffer, d->textCapacity));
        d->textOutput.reset(d->rhi->newBuffer(QRhiBuffer::Static, QRhiBuffer::StorageBuffer | QRhiBuffer::VertexBuffer, d->textCapacity));
        if (!d->textSource->create() || !d->textOutput->create()) return false;
    }
    if (!d->textUniform) {
        d->textUniform.reset(d->rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, 48));
        if (!d->textUniform->create()) return false;
    }
    if (!d->textBindings) d->textBindings.reset(d->rhi->newShaderResourceBindings());
    using B = QRhiShaderResourceBinding;
    d->textBindings->setBindings({B::uniformBuffer(0, B::ComputeStage, d->textUniform.get()),
        B::bufferLoad(1, B::ComputeStage, state), B::bufferLoad(2, B::ComputeStage, d->textSource.get()),
        B::bufferStore(3, B::ComputeStage, d->textOutput.get())});
    if (!d->textBindings->create()) return false;
    if (!d->textPipeline) {
        QFile file(":/forceview/shaders/shaders/force_text_positions.comp.qsb");
        if (!file.open(QIODevice::ReadOnly)) return false;
        d->textPipeline.reset(d->rhi->newComputePipeline());
        d->textPipeline->setShaderResourceBindings(d->textBindings.get());
        d->textPipeline->setShaderStage({QRhiShaderStage::Compute, QShader::fromSerialized(file.readAll())});
        if (!d->textPipeline->create()) { d->textPipeline.reset(); return false; }
    }
    auto* updates = d->rhi->nextResourceUpdateBatch();
    struct alignas(16) Parameters {
        quint32 counts[4] = {};
        float view[4] = {1, 1, 0, 0};
        float center[4] = {};
    } params;
    params.counts[0] = count;
    if (screen) {
        std::copy_n(screen, 4, params.view);
        params.center[0] = screen[4]; params.center[1] = screen[5];
    }
    updates->updateDynamicBuffer(d->textUniform.get(), 0, 48, &params);
    updates->uploadStaticBuffer(d->textSource.get(), 0, bytes, vertices);
    cb->beginComputePass(updates);
    cb->setComputePipeline(d->textPipeline.get()); cb->setShaderResources(d->textBindings.get());
    cb->dispatch((count + 127) / 128, 1, 1); cb->endComputePass();
    return true;
}
