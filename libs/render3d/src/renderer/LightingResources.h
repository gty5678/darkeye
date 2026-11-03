#pragma once
#include "render3d/Lighting.h"
#include <rhi/qrhi.h>
#include <functional>
#include <memory>

namespace darkeye::render3d {
// Owns environment textures and auxiliary passes for exactly one QRhi.
class LightingResources {
public:
    ~LightingResources();
    struct Target {
        std::unique_ptr<QRhiTexture> color;
        std::unique_ptr<QRhiRenderBuffer> depth;
        std::unique_ptr<QRhiRenderPassDescriptor> pass;
        std::unique_ptr<QRhiTextureRenderTarget> target;
        void clear() { target.reset(); pass.reset(); depth.reset(); color.reset(); }
    };
    bool resourcesWillChange(QSize size,const LightingSettings &settings,const std::shared_ptr<const EnvironmentMap> &environment) const;
    bool prepare(QRhi *rhi,QSize size,const LightingSettings &settings,
                 std::shared_ptr<const EnvironmentMap> environment,QRhiCommandBuffer *cb,QString &error);
    bool ensurePipelines(QRhiShaderResourceBindings *geometryLayout,QRhiRenderPassDescriptor *scenePass,
                         int samples,QString &error);
    void beforeScene(QRhiCommandBuffer *cb,const std::function<void(QRhiGraphicsPipeline *, bool cameraVisibleOnly)> &geometry);
    void background(QRhiCommandBuffer *cb);
    std::vector<QRhiShaderResourceBinding> bindings() const;
    QRhiBuffer *uniform() const { return m_uniform.get(); }
    void invalidateGeometryPipelines() { m_shadowPipeline.reset(); m_depthPipeline.reset(); }
    quint64 revision=0,textureBytes=0;
    int drawCalls=0;
    const LightingSettings &settings() const { return m_settings; }
private:
    bool makeTarget(Target &target,QSize size,QRhiTexture::Format format,bool depth,QString &error);
    bool makeTexture(std::unique_ptr<QRhiTexture> &texture,const std::vector<QImage> &images,
                     QRhiResourceUpdateBatch *updates,QString &error);
    QRhi *m_rhi=nullptr;
    QSize m_size;
    LightingSettings m_settings;
    std::shared_ptr<const EnvironmentMap> m_environment;
    std::unique_ptr<QRhiBuffer> m_uniform;
    std::unique_ptr<QRhiSampler> m_linear,m_environmentSampler,m_mipSampler,m_nearest;
    std::unique_ptr<QRhiTexture> m_white,m_radiance,m_diffuse,m_specular,m_brdf;
    Target m_shadow,m_normalDepth,m_ao;
    std::unique_ptr<QRhiShaderResourceBindings> m_aoBindings,m_backgroundBindings;
    std::unique_ptr<QRhiGraphicsPipeline> m_shadowPipeline,m_depthPipeline,m_aoPipeline,m_backgroundPipeline;
    std::unique_ptr<QRhiRenderPassDescriptor> m_backgroundPass;
    bool m_pipelinesDirty=true;
    bool m_initialized=false;
};
}
