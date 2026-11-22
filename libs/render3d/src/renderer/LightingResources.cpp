#include "LightingResources.h"
#include "render3d/Mesh.h"
#include <QFile>
#include <algorithm>

namespace darkeye::render3d {
namespace {
QShader loadShader(const char *name,QString &error)
{
    QFile file(QStringLiteral(":/render3d/shaders/%1.qsb").arg(QLatin1String(name)));
    if(!file.open(QIODevice::ReadOnly)) { error="Cannot load lighting shader "+file.fileName(); return {}; }
    const auto result=QShader::fromSerialized(file.readAll());
    if(!result.isValid()) error="Invalid lighting shader "+file.fileName();
    return result;
}
QRhiVertexInputLayout meshLayout()
{
    QRhiVertexInputLayout layout;
    layout.setBindings({{sizeof(Vertex)}});
    layout.setAttributes({{0,0,QRhiVertexInputAttribute::Float3,offsetof(Vertex,position)},
        {0,1,QRhiVertexInputAttribute::Float3,offsetof(Vertex,color)},
        {0,2,QRhiVertexInputAttribute::Float3,offsetof(Vertex,normal)},
        {0,3,QRhiVertexInputAttribute::Float2,offsetof(Vertex,uv)}});
    return layout;
}
}
LightingResources::~LightingResources()
{
    m_shadowPipeline.reset(); m_depthPipeline.reset(); m_aoPipeline.reset(); m_backgroundPipeline.reset();
    m_backgroundPass.reset(); m_aoBindings.reset(); m_backgroundBindings.reset();
}
bool LightingResources::resourcesWillChange(QSize size,const LightingSettings &settings,
    const std::shared_ptr<const EnvironmentMap> &environment) const
{
    return !m_initialized || environment!=m_environment || settings.shadows!=m_settings.shadows
        || settings.ambientOcclusion!=m_settings.ambientOcclusion || (settings.ambientOcclusion && size!=m_size);
}
bool LightingResources::makeTarget(Target &target,QSize size,QRhiTexture::Format format,bool depth,QString &error)
{
    target.target.reset(); target.pass.reset(); target.depth.reset(); target.color.reset();
    target.color.reset(m_rhi->newTexture(format,size,1,QRhiTexture::RenderTarget));
    if(!target.color->create()) { error="Cannot create lighting color target"; return false; }
    if(depth) {
        target.depth.reset(m_rhi->newRenderBuffer(QRhiRenderBuffer::DepthStencil,size,1));
        if(!target.depth->create()) { error="Cannot create lighting depth target"; return false; }
    }
    target.target.reset(m_rhi->newTextureRenderTarget(
        QRhiTextureRenderTargetDescription(QRhiColorAttachment(target.color.get()),target.depth.get())));
    target.pass.reset(target.target->newCompatibleRenderPassDescriptor());
    target.target->setRenderPassDescriptor(target.pass.get());
    if(!target.target->create()) { error="Cannot create lighting framebuffer"; return false; }
    return true;
}
bool LightingResources::makeTexture(std::unique_ptr<QRhiTexture> &texture,const std::vector<QImage> &images,
                                    QRhiResourceUpdateBatch *updates,QString &error)
{
    if(images.empty() || images.front().isNull()) { error="Empty environment image"; return false; }
    texture.reset(m_rhi->newTexture(QRhiTexture::RGBA16F,images.front().size(),1,
        images.size()>1?QRhiTexture::MipMapped:QRhiTexture::Flags{}));
    if(!texture->create()) { error="Cannot create HDR environment texture"; return false; }
    QRhiTextureUploadDescription upload;
    std::vector<QRhiTextureUploadEntry> entries;
    for(std::size_t mip=0;mip<images.size();++mip) {
        const auto image=images[mip].convertToFormat(QImage::Format_RGBA16FPx4);
        const QByteArray bytes(reinterpret_cast<const char *>(image.constBits()),image.sizeInBytes());
        entries.emplace_back(0,int(mip),QRhiTextureSubresourceUploadDescription(bytes));
    }
    upload.setEntries(entries.begin(),entries.end());
    updates->uploadTexture(texture.get(),upload);
    return true;
}
bool LightingResources::prepare(QRhi *rhi,QSize size,const LightingSettings &settings,
    std::shared_ptr<const EnvironmentMap> environment,QRhiCommandBuffer *cb,QString &error)
{
    m_rhi=rhi;
    bool changed=false;
    if(!m_initialized) {
        m_uniform.reset(rhi->newBuffer(QRhiBuffer::Dynamic,QRhiBuffer::UniformBuffer,400));
        if(!m_uniform->create()) { error="Cannot create lighting frame uniform"; return false; }
        const auto sampler=[&](std::unique_ptr<QRhiSampler> &out,QRhiSampler::Filter filter,
                               QRhiSampler::Filter mip,QRhiSampler::AddressMode u) {
            out.reset(rhi->newSampler(filter,filter,mip,u,QRhiSampler::ClampToEdge));
            return out->create();
        };
        if(!sampler(m_linear,QRhiSampler::Linear,QRhiSampler::None,QRhiSampler::ClampToEdge)
            || !sampler(m_environmentSampler,QRhiSampler::Linear,QRhiSampler::None,QRhiSampler::Repeat)
            || !sampler(m_mipSampler,QRhiSampler::Linear,QRhiSampler::Linear,QRhiSampler::Repeat)
            || !sampler(m_nearest,QRhiSampler::Nearest,QRhiSampler::None,QRhiSampler::ClampToEdge)) {
            error="Cannot create lighting samplers"; return false;
        }
        auto *updates=rhi->nextResourceUpdateBatch();
        QImage white(1,1,QImage::Format_RGBA32FPx4); white.fill(Qt::white);
        const bool success=makeTexture(m_white,{white},updates,error);
        cb->resourceUpdate(updates);
        if(!success) return false;
        m_initialized=true;
        changed=true;
    }
    if(environment!=m_environment) {
        // Pipelines/bindings referencing old textures must be destroyed first.
        m_backgroundPipeline.reset(); m_backgroundBindings.reset();
        m_radiance.reset(); m_diffuse.reset(); m_specular.reset(); m_brdf.reset();
        if(environment) {
            auto *updates=rhi->nextResourceUpdateBatch();
            const bool success=makeTexture(m_radiance,{environment->radiance},updates,error)
                && makeTexture(m_diffuse,{environment->irradiance},updates,error)
                && makeTexture(m_specular,environment->specular,updates,error)
                && makeTexture(m_brdf,{environment->brdf},updates,error);
            cb->resourceUpdate(updates);
            if(!success) return false;
        }
        m_environment=std::move(environment); changed=true;
    }
    const bool resize=size!=m_size;
    if((resize && settings.ambientOcclusion) || settings.shadows!=m_settings.shadows || settings.ambientOcclusion!=m_settings.ambientOcclusion) {
        m_shadowPipeline.reset(); m_depthPipeline.reset(); m_aoPipeline.reset(); m_aoBindings.reset();
        if(settings.shadows && !m_shadow.target) {
            if(!makeTarget(m_shadow,{2048,2048},QRhiTexture::R32F,true,error)) return false;
        } else if(!settings.shadows) m_shadow.clear();
        if(settings.ambientOcclusion) {
            QSize half(std::max(1,size.width()/2),std::max(1,size.height()/2));
            if(half.width()>960 || half.height()>960) half=half.scaled(960,960,Qt::KeepAspectRatio);
            if(!makeTarget(m_normalDepth,half,QRhiTexture::RGBA16F,true,error)
                || !makeTarget(m_ao,half,QRhiTexture::RGBA8,false,error)) return false;
        } else { m_normalDepth.clear(); m_ao.clear(); }
        changed=true;
    }
    m_size=size; m_settings=settings;
    if(changed) { ++revision; m_pipelinesDirty=true; }
    textureBytes=8;
    for(const auto *texture:{m_radiance.get(),m_diffuse.get(),m_brdf.get()})
        if(texture) textureBytes+=quint64(texture->pixelSize().width())*texture->pixelSize().height()*8;
    if(m_specular) for(const auto &image:m_environment->specular) textureBytes+=quint64(image.width())*image.height()*8;
    if(m_shadow.color) textureBytes+=quint64(2048)*2048*8;
    if(m_normalDepth.color) textureBytes+=quint64(m_normalDepth.color->pixelSize().width())*m_normalDepth.color->pixelSize().height()*16;
    return true;
}
std::vector<QRhiShaderResourceBinding> LightingResources::bindings() const
{
    const auto frag=QRhiShaderResourceBinding::FragmentStage;
    const auto texture=[&](int slot,QRhiTexture *image,QRhiSampler *sampler) {
        return QRhiShaderResourceBinding::sampledTexture(slot,frag,image?image:m_white.get(),sampler);
    };
    return {texture(3,m_diffuse.get(),m_environmentSampler.get()),
        texture(4,m_specular.get(),m_specular?m_mipSampler.get():m_linear.get()),
        texture(5,m_brdf.get(),m_linear.get()),texture(6,m_shadow.color.get(),m_nearest.get()),
        QRhiShaderResourceBinding::uniformBuffer(7,QRhiShaderResourceBinding::VertexStage|frag,m_uniform.get()),
        texture(8,m_ao.color.get(),m_linear.get()),texture(9,m_radiance.get(),m_environmentSampler.get())};
}
bool LightingResources::ensurePipelines(QRhiShaderResourceBindings *geometryLayout,
    QRhiRenderPassDescriptor *scenePass,int samples,QString &error)
{
    if(m_pipelinesDirty) {
        m_shadowPipeline.reset(); m_depthPipeline.reset(); m_aoPipeline.reset();
        m_aoBindings.reset(); m_backgroundPipeline.reset(); m_backgroundBindings.reset();
        m_pipelinesDirty=false;
    }
    const auto geometryPipeline=[&](std::unique_ptr<QRhiGraphicsPipeline> &pipeline,Target &target,const char *vert,const char *frag) {
        if(pipeline || !target.target || !geometryLayout) return true;
        auto vs=loadShader(vert,error),fs=loadShader(frag,error);
        if(!error.isEmpty()) return false;
        pipeline.reset(m_rhi->newGraphicsPipeline());
        pipeline->setShaderStages({{QRhiShaderStage::Vertex,vs},{QRhiShaderStage::Fragment,fs}});
        pipeline->setVertexInputLayout(meshLayout());
        pipeline->setShaderResourceBindings(geometryLayout);
        pipeline->setRenderPassDescriptor(target.pass.get());
        pipeline->setDepthTest(true); pipeline->setDepthWrite(true);
        pipeline->setDepthOp(QRhiGraphicsPipeline::Less); pipeline->setCullMode(QRhiGraphicsPipeline::None);
        if(!pipeline->create()) { pipeline.reset(); error="Cannot create geometry lighting pipeline"; return false; }
        return true;
    };
    if(!geometryPipeline(m_shadowPipeline,m_shadow,"shadow.vert","shadow.frag")
        || !geometryPipeline(m_depthPipeline,m_normalDepth,"depthnormal.vert","depthnormal.frag")) return false;
    const auto fullscreen=[&](std::unique_ptr<QRhiGraphicsPipeline> &pipeline,QRhiShaderResourceBindings *bindings,
                              QRhiRenderPassDescriptor *pass,int count,const char *frag) {
        auto vs=loadShader("lightingfullscreen.vert",error),fs=loadShader(frag,error);
        if(!error.isEmpty()) return false;
        pipeline.reset(m_rhi->newGraphicsPipeline());
        pipeline->setShaderStages({{QRhiShaderStage::Vertex,vs},{QRhiShaderStage::Fragment,fs}});
        pipeline->setShaderResourceBindings(bindings); pipeline->setRenderPassDescriptor(pass);
        pipeline->setSampleCount(count);
        if(!pipeline->create()) { pipeline.reset(); error="Cannot create fullscreen lighting pipeline"; return false; }
        return true;
    };
    if(m_ao.target && !m_aoPipeline) {
        m_aoBindings.reset(m_rhi->newShaderResourceBindings());
        m_aoBindings->setBindings({QRhiShaderResourceBinding::sampledTexture(0,QRhiShaderResourceBinding::FragmentStage,
            m_normalDepth.color.get(),m_nearest.get()),
            QRhiShaderResourceBinding::uniformBuffer(7,QRhiShaderResourceBinding::VertexStage|QRhiShaderResourceBinding::FragmentStage,m_uniform.get())});
        if(!m_aoBindings->create()) { error="Cannot create SSAO bindings"; return false; }
        if(!fullscreen(m_aoPipeline,m_aoBindings.get(),m_ao.pass.get(),1,"ssao.frag")) return false;
    }
    if(!m_backgroundPipeline || !m_backgroundPass->isCompatible(scenePass) || m_backgroundPipeline->sampleCount()!=samples) {
        m_backgroundPipeline.reset(); m_backgroundPass.reset(scenePass->newCompatibleRenderPassDescriptor());
        if(!m_backgroundBindings) {
            m_backgroundBindings.reset(m_rhi->newShaderResourceBindings());
            m_backgroundBindings->setBindings({QRhiShaderResourceBinding::uniformBuffer(7,
                QRhiShaderResourceBinding::VertexStage|QRhiShaderResourceBinding::FragmentStage,m_uniform.get()),
                QRhiShaderResourceBinding::sampledTexture(9,QRhiShaderResourceBinding::FragmentStage,
                    m_radiance?m_radiance.get():m_white.get(),m_environmentSampler.get())});
            if(!m_backgroundBindings->create()) { error="Cannot create background bindings"; return false; }
        }
        if(!fullscreen(m_backgroundPipeline,m_backgroundBindings.get(),scenePass,samples,"environment.frag")) return false;
    }
    return true;
}
void LightingResources::beforeScene(QRhiCommandBuffer *cb,const std::function<void(QRhiGraphicsPipeline *, bool cameraVisibleOnly)> &geometry)
{
    drawCalls=0;
    if(m_settings.shadows && m_shadow.target) {
        cb->beginPass(m_shadow.target.get(),Qt::white,{1,0});
        cb->setViewport({0,0,2048,2048}); if(m_shadowPipeline) geometry(m_shadowPipeline.get(),false); cb->endPass();
    }
    if(m_settings.ambientOcclusion && m_normalDepth.target) {
        const auto size=m_normalDepth.color->pixelSize();
        cb->beginPass(m_normalDepth.target.get(),Qt::transparent,{1,0});
        cb->setViewport({0,0,float(size.width()),float(size.height())}); if(m_depthPipeline) geometry(m_depthPipeline.get(),true); cb->endPass();
    }
    if(m_settings.ambientOcclusion && m_aoPipeline) {
        const auto size=m_ao.color->pixelSize();
        cb->beginPass(m_ao.target.get(),Qt::white,{1,0});
        cb->setGraphicsPipeline(m_aoPipeline.get()); cb->setShaderResources(m_aoBindings.get());
        cb->setViewport({0,0,float(size.width()),float(size.height())}); cb->draw(3); cb->endPass(); ++drawCalls;
    }
}
void LightingResources::background(QRhiCommandBuffer *cb)
{
    if(!m_settings.environmentBackground || !m_environment) return;
    cb->setGraphicsPipeline(m_backgroundPipeline.get()); cb->setShaderResources(m_backgroundBindings.get());
    cb->draw(3); ++drawCalls;
}
}
