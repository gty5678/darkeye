#pragma once

#include "render3d/RhiSceneWidget.h"
#include "LightingResources.h"
#include <rhi/qrhi.h>
#include <map>
#include <tuple>
#include <algorithm>
#include <array>

namespace darkeye::render3d {

// One GPU cache per QRhi. Render targets never escape the callback/cache.
class Renderer {
public:
    ~Renderer();
    void release();
    void setTextureBudget(quint64 bytes) {
        const auto budget=std::max<quint64>(65536,bytes);
        if(m_textureBudget!=budget) { m_textureBudget=budget; m_textureBudgetDirty=true; }
    }
    void setLighting(const LightingSettings &settings) { m_lightingSettings=settings; }
    LightingSettings lighting() const { return m_lightingSettings; }
    void setEnvironment(std::shared_ptr<const EnvironmentMap> environment) { m_environment=std::move(environment); }
    bool prepare(QRhi *rhi, QRhiRenderTarget *target,
                 const std::vector<RenderItem> &items, QRhiCommandBuffer *cb,
                 RenderStatistics &statistics, QString &error);
    void draw(QRhiRenderTarget *target, QRhiCommandBuffer *cb,
              const Camera &camera, const QQuaternion &rotation,
              const QColor &clearColor, RenderStatistics &statistics);

private:
    struct Geometry {
        MeshHandle source;
        std::unique_ptr<QRhiBuffer> vertices, indices;
        quint32 indexCount = 0;
        quint64 bytes = 0;
        QVector3D lower,upper;
    };
    struct Texture {
        std::shared_ptr<const TextureData> source;
        QSize sourceSize;
        std::unique_ptr<QRhiTexture> texture;
        std::unique_ptr<QRhiSampler> sampler;
        quint64 bytes = 0;
    };
    using BindingKey = std::array<const TextureData *,7>;
    using PipelineKey = std::tuple<Material::AlphaMode, bool, bool, bool>;
    void clearTransmissionTargets();
    bool ensureTransmissionTargets(bool enabled, QString &error);
    bool ensureTargets(QRhiRenderTarget *target, QString &error);
    bool ensurePresentation(QRhiRenderTarget *target, RenderStatistics &stats, QString &error);
    QRhi *m_rhi = nullptr;
    std::vector<RenderItem> m_items;
    std::map<const MeshData *, Geometry> m_geometry;
    std::map<const TextureData *, Texture> m_textures;
    std::map<BindingKey, std::unique_ptr<QRhiShaderResourceBindings>> m_bindings;
    std::map<BindingKey, std::unique_ptr<QRhiShaderResourceBindings>> m_opaqueBindings;
    std::map<BindingKey, std::unique_ptr<QRhiShaderResourceBindings>> m_reverseBindings;
    std::map<BindingKey, std::unique_ptr<QRhiShaderResourceBindings>> m_auxBindings;
    std::unique_ptr<QRhiShaderResourceBindings> m_auxLayout;
    std::map<PipelineKey, std::unique_ptr<QRhiGraphicsPipeline>> m_pipelines;
    std::unique_ptr<QRhiBuffer> m_uniform;
    std::unique_ptr<QRhiShaderResourceBindings> m_layoutBindings;
    quint32 m_uniformStride = 0;
    quint32 m_uniformCapacity = 0;
    std::unique_ptr<QRhiRenderPassDescriptor> m_scenePass;
    std::unique_ptr<QRhiTexture> m_color, m_resolved;
    std::unique_ptr<QRhiRenderBuffer> m_depth;
    std::unique_ptr<QRhiTextureRenderTarget> m_sceneTarget;
    // Alternate scene target for bounded back-to-front transmission composition.
    std::unique_ptr<QRhiTextureRenderTarget> m_transmissionTarget;
    std::unique_ptr<QRhiTexture> m_transmissionColor, m_transmissionResolved;
    std::unique_ptr<QRhiSampler> m_transmissionSampler;
    std::unique_ptr<QRhiShaderResourceBindings> m_transmissionPresentBindings;
    std::unique_ptr<QRhiGraphicsPipeline> m_copyPipeline;
    std::unique_ptr<QRhiSampler> m_presentSampler;
    std::unique_ptr<QRhiBuffer> m_presentUniform;
    std::unique_ptr<QRhiShaderResourceBindings> m_presentBindings;
    std::unique_ptr<QRhiRenderPassDescriptor> m_presentPass;
    std::unique_ptr<QRhiGraphicsPipeline> m_presentPipeline;
    QSize m_size;
    int m_samples = 0;
    bool m_targetsReady=false;
    quint64 m_textureBudget=32*1024*1024;
    bool m_textureBudgetDirty=true;
    LightingSettings m_lightingSettings;
    std::shared_ptr<const EnvironmentMap> m_environment;
    std::unique_ptr<LightingResources> m_lighting;
};

} // namespace darkeye::render3d
