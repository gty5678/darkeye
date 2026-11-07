#include "Renderer.h"

#include <QFile>
#include <QLoggingCategory>
#include <QResource>
#include <QRectF>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <set>

static void initializeShaderResources()
{
    static const bool initialized = [] { Q_INIT_RESOURCE(render3d_shaders); return true; }();
    Q_UNUSED(initialized);
}
Q_LOGGING_CATEGORY(render3dLog, "darkeye.render3d")

namespace darkeye::render3d {
namespace {
constexpr quint32 uniformBytes = 336;

std::array<std::shared_ptr<const TextureData>,8> materialTextures(const Material &material)
{
    return {material.baseColorTexture,material.metallicRoughnessTexture,material.normalTexture,
        material.occlusionTexture,material.emissiveTexture,material.transmissionTexture,material.thicknessTexture,
        std::shared_ptr<const TextureData>{}};
}
std::array<const TextureData *,7> materialKey(const Material &material)
{
    return {material.baseColorTexture.get(),material.metallicRoughnessTexture.get(),material.normalTexture.get(),
        material.occlusionTexture.get(),material.emissiveTexture.get(),material.transmissionTexture.get(),material.thicknessTexture.get()};
}

bool transmissive(const Material &material)
{
    return material.shading==Material::Shading::MetallicRoughness && material.transmission>0 && material.metallic<1;
}

QShader shader(const QString &name, QString &error)
{
    QFile file(QStringLiteral(":/render3d/shaders/%1.qsb").arg(name));
    if (!file.open(QIODevice::ReadOnly)) {
        error = QStringLiteral("Cannot open embedded shader %1: %2").arg(file.fileName(), file.errorString());
        return {};
    }
    auto result = QShader::fromSerialized(file.readAll());
    if (!result.isValid())
        error = QStringLiteral("Invalid embedded QShader: %1").arg(file.fileName());
    return result;
}

const Material &materialOf(const RenderItem &item)
{
    static const Material fallback;
    return item.material ? *item.material : fallback;
}

QRhiSampler::AddressMode wrap(TextureData::Wrap mode)
{
    switch (mode) {
    case TextureData::Wrap::ClampToEdge: return QRhiSampler::ClampToEdge;
    case TextureData::Wrap::MirroredRepeat: return QRhiSampler::Mirror;
    default: return QRhiSampler::Repeat;
    }
}

float linearChannel(float display)
{
    return display <= 0.04045f ? display / 12.92f : std::pow((display + 0.055f) / 1.055f, 2.4f);
}

quint64 textureCost(QSize size,bool mipmapped)
{
    quint64 bytes=0;
    do {
        bytes+=quint64(size.width())*size.height()*4;
        if(!mipmapped || (size.width()==1 && size.height()==1)) break;
        size={std::max(1,size.width()/2),std::max(1,size.height()/2)};
    } while(true);
    return bytes;
}
}

Renderer::~Renderer() { release(); }

void Renderer::clearTransmissionTargets()
{
    m_bindings.clear(); m_opaqueBindings.clear(); m_reverseBindings.clear();
    m_copyPipeline.reset(); m_transmissionPresentBindings.reset();
    m_transmissionTarget.reset();
    m_transmissionResolved.reset(); m_transmissionColor.reset();
}

bool Renderer::ensureTransmissionTargets(bool enabled,QString &error)
{
    if(!enabled) { if(m_transmissionTarget) clearTransmissionTargets(); return true; }
    if(m_transmissionTarget && m_copyPipeline && m_transmissionPresentBindings) return true;
    clearTransmissionTargets();
    m_transmissionColor.reset(m_rhi->newTexture(QRhiTexture::RGBA16F,m_size,m_samples,QRhiTexture::RenderTarget));
    if(!m_transmissionColor->create()) { error="Cannot allocate transmission color"; return false; }
    QRhiColorAttachment attachment(m_transmissionColor.get());
    if(m_samples>1) {
        m_transmissionResolved.reset(m_rhi->newTexture(QRhiTexture::RGBA16F,m_size,1,QRhiTexture::RenderTarget));
        if(!m_transmissionResolved->create()) { error="Cannot allocate transmission resolve"; return false; }
        attachment.setResolveTexture(m_transmissionResolved.get());
    }
    m_transmissionTarget.reset(m_rhi->newTextureRenderTarget(QRhiTextureRenderTargetDescription(attachment,m_depth.get())));
    m_transmissionTarget->setRenderPassDescriptor(m_scenePass.get());
    if(!m_transmissionTarget->create()) { error="Cannot create transmission framebuffer"; return false; }
    if(!m_transmissionSampler) {
        m_transmissionSampler.reset(m_rhi->newSampler(QRhiSampler::Linear,QRhiSampler::Linear,QRhiSampler::None,
            QRhiSampler::ClampToEdge,QRhiSampler::ClampToEdge));
        if(!m_transmissionSampler->create()) { error="Cannot create transmission sampler"; return false; }
    }
    m_transmissionPresentBindings.reset(m_rhi->newShaderResourceBindings());
    m_transmissionPresentBindings->setBindings({QRhiShaderResourceBinding::sampledTexture(0,
        QRhiShaderResourceBinding::FragmentStage,
        m_transmissionResolved?m_transmissionResolved.get():m_transmissionColor.get(),m_presentSampler.get()),
        QRhiShaderResourceBinding::uniformBuffer(1,QRhiShaderResourceBinding::VertexStage|QRhiShaderResourceBinding::FragmentStage,
            m_presentUniform.get())});
    if(!m_transmissionPresentBindings->create()) { error="Cannot create transmission presentation bindings"; return false; }
    const auto vert=shader(QStringLiteral("present.vert"),error),frag=shader(QStringLiteral("copylinear.frag"),error);
    if(!error.isEmpty()) return false;
    m_copyPipeline.reset(m_rhi->newGraphicsPipeline());
    m_copyPipeline->setShaderStages({{QRhiShaderStage::Vertex,vert},{QRhiShaderStage::Fragment,frag}});
    m_copyPipeline->setShaderResourceBindings(m_presentBindings.get());
    m_copyPipeline->setRenderPassDescriptor(m_scenePass.get());
    m_copyPipeline->setSampleCount(m_samples);
    if(!m_copyPipeline->create()) { error="Cannot create linear composition copy pipeline"; return false; }
    return true;
}

void Renderer::release()
{
    clearTransmissionTargets(); m_transmissionSampler.reset();
    m_presentPipeline.reset();
    m_presentPass.reset();
    m_presentBindings.reset();
    m_presentUniform.reset();
    m_presentSampler.reset();
    m_pipelines.clear();
    m_layoutBindings.reset();
    m_bindings.clear(); m_opaqueBindings.clear(); m_reverseBindings.clear();
    m_auxBindings.clear(); m_auxLayout.reset();
    m_lighting.reset();
    m_uniform.reset();
    m_sceneTarget.reset();
    m_scenePass.reset();
    m_depth.reset();
    m_resolved.reset();
    m_color.reset();
    m_textures.clear();
    m_geometry.clear();
    m_items.clear();
    m_rhi = nullptr;
    m_uniformStride = 0;
    m_uniformCapacity = 0;
    m_samples = 0;
    m_size = {};
    m_targetsReady=false;
    m_textureBudgetDirty=true;
}

bool Renderer::ensureTargets(QRhiRenderTarget *target, QString &error)
{
    if (m_targetsReady && m_sceneTarget && m_size == target->pixelSize() && m_samples == target->sampleCount())
        return true;
    m_targetsReady=false;
    clearTransmissionTargets();
    m_sceneTarget.reset();
    m_depth.reset();
    m_resolved.reset();
    m_color.reset();
    const auto size = target->pixelSize();
    const auto samples = target->sampleCount();
    auto color = std::unique_ptr<QRhiTexture>(m_rhi->newTexture(
        QRhiTexture::RGBA16F, size, samples, QRhiTexture::RenderTarget));
    auto depth = std::unique_ptr<QRhiRenderBuffer>(m_rhi->newRenderBuffer(
        QRhiRenderBuffer::DepthStencil, size, samples));
    if (!color->create() || !depth->create()) {
        error = QStringLiteral("Failed to allocate linear scene color/depth targets");
        return false;
    }
    QRhiColorAttachment attachment(color.get());
    std::unique_ptr<QRhiTexture> resolved;
    if (samples > 1) {
        resolved.reset(m_rhi->newTexture(QRhiTexture::RGBA16F, size, 1, QRhiTexture::RenderTarget));
        if (!resolved->create()) {
            error = QStringLiteral("Failed to allocate scene MSAA resolve texture");
            return false;
        }
        attachment.setResolveTexture(resolved.get());
    }
    QRhiTextureRenderTargetDescription description(attachment, depth.get());
    auto framebuffer = std::unique_ptr<QRhiTextureRenderTarget>(m_rhi->newTextureRenderTarget(description));
    auto pass = std::unique_ptr<QRhiRenderPassDescriptor>(framebuffer->newCompatibleRenderPassDescriptor());
    if (!m_scenePass || !m_scenePass->isCompatible(pass.get())) {
        m_pipelines.clear();
        m_scenePass = std::move(pass);
    }
    framebuffer->setRenderPassDescriptor(m_scenePass.get());
    if (!framebuffer->create()) {
        error = QStringLiteral("Failed to create linear scene render target");
        return false;
    }
    m_color = std::move(color);
    m_resolved = std::move(resolved);
    m_depth = std::move(depth);
    m_sceneTarget = std::move(framebuffer);
    m_size = size;
    m_samples = samples;
    // Reuse the SRB object/layout across resize. The presentation pipeline does
    // not need to be rebuilt when only the sampled render-target size changes.
    if (!m_presentSampler) {
        m_presentSampler.reset(m_rhi->newSampler(QRhiSampler::Nearest, QRhiSampler::Nearest,
            QRhiSampler::None, QRhiSampler::ClampToEdge, QRhiSampler::ClampToEdge));
        if (!m_presentSampler->create()) {
            m_presentSampler.reset();
            error = QStringLiteral("Failed to create presentation sampler");
            return false;
        }
    }
    if (!m_presentBindings)
        m_presentBindings.reset(m_rhi->newShaderResourceBindings());
    if (!m_presentUniform) {
        m_presentUniform.reset(m_rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, 16));
        if (!m_presentUniform->create()) {
            m_presentUniform.reset();
            error = QStringLiteral("Failed to create presentation uniform");
            return false;
        }
    }
    m_presentBindings->setBindings({QRhiShaderResourceBinding::sampledTexture(
        0, QRhiShaderResourceBinding::FragmentStage,
        m_resolved ? m_resolved.get() : m_color.get(), m_presentSampler.get()),
        QRhiShaderResourceBinding::uniformBuffer(1, QRhiShaderResourceBinding::VertexStage|QRhiShaderResourceBinding::FragmentStage,
            m_presentUniform.get())});
    if (!m_presentBindings->create()) {
        error = QStringLiteral("Failed to create presentation bindings");
        return false;
    }
    m_targetsReady=true;
    return true;
}

bool Renderer::ensurePresentation(QRhiRenderTarget *target, RenderStatistics &stats, QString &error)
{
    if (m_presentPipeline && m_presentPipeline->sampleCount() == target->sampleCount()
        && m_presentPass->isCompatible(target->renderPassDescriptor()))
        return true;
    m_presentPipeline.reset();
    m_presentPass.reset(target->renderPassDescriptor()->newCompatibleRenderPassDescriptor());
    auto vert = shader(QStringLiteral("present.vert"), error);
    auto frag = shader(QStringLiteral("present.frag"), error);
    if (!error.isEmpty())
        return false;
    auto pipeline = std::unique_ptr<QRhiGraphicsPipeline>(m_rhi->newGraphicsPipeline());
    pipeline->setShaderStages({{QRhiShaderStage::Vertex, vert}, {QRhiShaderStage::Fragment, frag}});
    pipeline->setShaderResourceBindings(m_presentBindings.get());
    pipeline->setRenderPassDescriptor(m_presentPass.get());
    pipeline->setSampleCount(target->sampleCount());
    if (!pipeline->create()) {
        error = QStringLiteral("Failed to create linear-to-display presentation pipeline");
        return false;
    }
    m_presentPipeline = std::move(pipeline);
    ++stats.pipelineGenerations;
    return true;
}

bool Renderer::prepare(QRhi *rhi, QRhiRenderTarget *target,
                       const std::vector<RenderItem> &items, QRhiCommandBuffer *cb,
                       RenderStatistics &stats, QString &error)
{
    if (m_rhi != rhi) {
        release();
        m_rhi = rhi;
        ++stats.resourceGenerations;
        stats.backend = QString::fromLatin1(rhi->backendName());
        qCInfo(render3dLog) << "QRhi generation" << stats.resourceGenerations
                           << stats.backend << "device" << rhi->driverInfo().deviceName;
    }
    initializeShaderResources();
    if(!m_lighting) m_lighting=std::make_unique<LightingResources>();
    if(m_lighting->resourcesWillChange(target->pixelSize(),m_lightingSettings,m_environment)) {
        m_pipelines.clear(); m_layoutBindings.reset(); m_bindings.clear(); m_opaqueBindings.clear(); m_reverseBindings.clear();
        m_lighting->invalidateGeometryPipelines(); m_auxBindings.clear(); m_auxLayout.reset();
    }
    if(!m_lighting->prepare(rhi,target->pixelSize(),m_lightingSettings,m_environment,cb,error)) return false;
    if (!ensureTargets(target, error) || !ensurePresentation(target, stats, error))
        return false;
    const bool hadCopyPipeline=bool(m_copyPipeline);
    if(!ensureTransmissionTargets(std::any_of(items.begin(),items.end(),[](const RenderItem &item) {
            return transmissive(materialOf(item)); }),error)) return false;
    if(m_copyPipeline && !hadCopyPipeline) ++stats.pipelineGenerations;
    m_items = items;
    std::set<const MeshData *> activeGeometry;
    std::set<const TextureData *> activeTextures;
    std::set<BindingKey> activeBindings;
    std::set<PipelineKey> activePipelines;
    if(items.empty()) {
        m_lighting->invalidateGeometryPipelines(); m_auxBindings.clear(); m_auxLayout.reset();
        m_pipelines.clear();
        m_layoutBindings.reset();
        m_bindings.clear(); m_opaqueBindings.clear(); m_reverseBindings.clear();
        m_uniform.reset();
        m_uniformCapacity=0;
        m_uniformStride=0;
    }
    std::map<const TextureData *,QSize> desiredSizes;
    quint64 originalTextureBytes=0;
    if(!items.empty()) desiredSizes.emplace(nullptr,QSize(1,1));
    for(const auto &item:items) {
        const auto &material=materialOf(item);
        for(const auto &source:materialTextures(material))
            if(source && !desiredSizes.contains(source.get())) {
                const auto size=source->image.size();
                desiredSizes.emplace(source.get(),size);
                if(!size.isEmpty()) originalTextureBytes+=textureCost(size,source->mipmap!=TextureData::Mipmap::None);
            }
    }
    const double quality=std::min(1.0,std::sqrt(double(m_textureBudget-4)
        /double(std::max<quint64>(1,originalTextureBytes))));
    // A moving shelf promotes/demotes individual covers. Recalculating the
    // size of every resident texture for each promotion caused full-window
    // CPU rescaling, GPU uploads and binding churn on nearly every frame.
    // Retain immutable resident allocations; distribute the current fair share
    // only to new/replaced textures. Restore full quality with spare headroom.
    const bool restoreFullQuality=originalTextureBytes<=((m_textureBudget-4)*3/4);
    for(auto &[source,size]:desiredSizes) {
        if(!source) continue;
        const auto resident=m_textures.find(source);
        if(!m_textureBudgetDirty && !restoreFullQuality && resident!=m_textures.end()
           && resident->second.sourceSize==size)
            size=resident->second.texture->pixelSize();
        else
            size={std::max(1,int(size.width()*quality)),std::max(1,int(size.height()*quality))};
    }
    const auto desiredCost=[&] {
        quint64 total=0;
        for(const auto &[source,size]:desiredSizes)
            total+=textureCost(size,source && source->mipmap!=TextureData::Mipmap::None);
        return total;
    };
    while(desiredCost()>m_textureBudget) {
        // Reclaim only as much as the incoming covers need. Highest relative
        // resolution first keeps quality balanced without resizing the whole
        // window. 7/8 steps leave a little headroom for the next arrival.
        auto candidate=desiredSizes.end();
        double highestQuality=-1;
        quint64 largestCost=0;
        for(auto it=desiredSizes.begin();it!=desiredSizes.end();++it) {
            const auto &[source,size]=*it;
            if(!source || (size.width()<=1 && size.height()<=1)) continue;
            const auto original=source->image.size();
            const double relative=double(size.width())*size.height()
                /std::max(1.0,double(original.width())*original.height());
            const auto cost=textureCost(size,source->mipmap!=TextureData::Mipmap::None);
            if(relative>highestQuality || (relative==highestQuality && cost>largestCost)) {
                candidate=it; highestQuality=relative; largestCost=cost;
            }
        }
        if(candidate==desiredSizes.end()) {
            error=QStringLiteral("Texture budget cannot fit one texel per active texture");
            return false;
        }
        auto &size=candidate->second;
        size={std::max(1,size.width()*7/8),std::max(1,size.height()*7/8)};
    }
    // Evict obsolete textures/bindings before uploading a replacement window.
    // Pipeline layout bindings use a persistent white texture, not these SRBs.
    for(auto it=m_textures.begin();it!=m_textures.end();) {
        const auto desired=desiredSizes.find(it->first);
        if(desired==desiredSizes.end() || desired->second!=it->second.texture->pixelSize()) {
            // Scrolling changes only a few covers at a time. Preserve bindings
            // for every texture that is still resident, and release references
            // to the evicted GPU texture before destroying it.
            const auto referencesTexture=[source=it->first](const auto &entry) {
                return std::find(entry.first.begin(),entry.first.end(),source)!=entry.first.end();
            };
            std::erase_if(m_bindings,referencesTexture);
            std::erase_if(m_opaqueBindings,referencesTexture);
            std::erase_if(m_reverseBindings,referencesTexture);
            std::erase_if(m_auxBindings,referencesTexture);
            it=m_textures.erase(it);
        } else ++it;
    }
    auto *uploads = rhi->nextResourceUpdateBatch();
    const auto fail = [&](const QString &message) {
        // Commit earlier successful uploads even if a later asset failed, so
        // cache entries can never refer to buffers whose upload was discarded.
        cb->resourceUpdate(uploads);
        error = message;
        return false;
    };
    for (const auto &item : items) {
        activeGeometry.insert(item.mesh.get());
        if (!m_geometry.contains(item.mesh.get())) {
            Geometry geometry;
            geometry.source = item.mesh;
            geometry.lower=QVector3D(1e30f,1e30f,1e30f);
            geometry.upper=-geometry.lower;
            for(const auto &vertex:item.mesh->vertices)
                for(int axis=0;axis<3;++axis) {
                    geometry.lower[axis]=std::min(geometry.lower[axis],vertex.position[axis]);
                    geometry.upper[axis]=std::max(geometry.upper[axis],vertex.position[axis]);
                }
            const int vertexBytes = int(item.mesh->vertices.size() * sizeof(Vertex));
            const int indexBytes = int(item.mesh->indices.size() * sizeof(std::uint32_t));
            geometry.vertices.reset(rhi->newBuffer(QRhiBuffer::Immutable, QRhiBuffer::VertexBuffer, vertexBytes));
            geometry.indices.reset(rhi->newBuffer(QRhiBuffer::Immutable, QRhiBuffer::IndexBuffer, indexBytes));
            if (!geometry.vertices->create() || !geometry.indices->create())
                return fail(QStringLiteral("Failed to create shared geometry buffers"));
            uploads->uploadStaticBuffer(geometry.vertices.get(), item.mesh->vertices.data());
            uploads->uploadStaticBuffer(geometry.indices.get(), item.mesh->indices.data());
            geometry.indexCount = quint32(item.mesh->indices.size());
            geometry.bytes = quint64(vertexBytes) + indexBytes;
            m_geometry.emplace(item.mesh.get(), std::move(geometry));
            ++stats.geometryUploads;
        }
        const auto &material = materialOf(item);
        activeBindings.insert(materialKey(material));
        activePipelines.emplace(material.alphaMode, material.doubleSided, item.world.determinant() < 0,transmissive(material));
        for (const auto &source : materialTextures(material)) {
            activeTextures.insert(source.get());
            if (m_textures.contains(source.get()))
                continue;
            QImage image;
            if (source)
                image = source->image.convertToFormat(QImage::Format_RGBA8888);
            else {
                image = QImage(1, 1, QImage::Format_RGBA8888);
                image.fill(Qt::white);
            }
            if (image.isNull())
                return fail(QStringLiteral("Cannot upload an empty CPU texture"));
            const auto desired=desiredSizes.at(source.get());
            if(image.size()!=desired) image=image.scaled(desired,Qt::IgnoreAspectRatio,Qt::SmoothTransformation);
            Texture texture;
            texture.source = source;
            texture.sourceSize = source?source->image.size():QSize(1,1);
            QRhiTexture::Flags flags;
            if(source && source->srgb) flags|=QRhiTexture::sRGB;
            const bool mipmapped=source && source->mipmap!=TextureData::Mipmap::None
                && (image.width()>1 || image.height()>1);
            if(mipmapped) flags|=QRhiTexture::MipMapped|QRhiTexture::UsedWithGenerateMips;
            texture.texture.reset(rhi->newTexture(QRhiTexture::RGBA8, image.size(),1,flags));
            const auto mag = source && source->magFilter == TextureData::Filter::Nearest ? QRhiSampler::Nearest : QRhiSampler::Linear;
            const auto min = source && source->minFilter == TextureData::Filter::Nearest ? QRhiSampler::Nearest : QRhiSampler::Linear;
            const auto mip=mipmapped?(source->mipmap==TextureData::Mipmap::Nearest
                ?QRhiSampler::Nearest:QRhiSampler::Linear):QRhiSampler::None;
            texture.sampler.reset(rhi->newSampler(mag, min, mip,
                source ? wrap(source->wrapU) : QRhiSampler::Repeat,
                source ? wrap(source->wrapV) : QRhiSampler::Repeat));
            if (!texture.texture->create() || !texture.sampler->create())
                return fail(QStringLiteral("Failed to create texture/sampler: %1").arg(source ? source->source : "white fallback"));
            uploads->uploadTexture(texture.texture.get(), image);
            ++stats.textureUploads;
            stats.textureUploadBytes+=quint64(image.sizeInBytes());
            if(mipmapped) uploads->generateMips(texture.texture.get());
            texture.bytes = textureCost(image.size(),mipmapped);
            m_textures.emplace(source.get(), std::move(texture));
        }
    }
    cb->resourceUpdate(uploads);
    // Bindings must die before the textures/buffers they reference.
    std::erase_if(m_bindings, [&](const auto &entry) { return !activeBindings.contains(entry.first); });
    std::erase_if(m_opaqueBindings, [&](const auto &entry) { return !activeBindings.contains(entry.first); });
    std::erase_if(m_reverseBindings, [&](const auto &entry) { return !activeBindings.contains(entry.first); });
    std::erase_if(m_auxBindings, [&](const auto &entry) { return !activeBindings.contains(entry.first); });
    std::erase_if(m_textures, [&](const auto &entry) { return !activeTextures.contains(entry.first); });
    std::erase_if(m_geometry, [&](const auto &entry) { return !activeGeometry.contains(entry.first); });
    if (!items.empty()) {
        const auto stride = quint32(rhi->ubufAligned(uniformBytes));
        if (!m_uniform || m_uniformCapacity < items.size()) {
            m_lighting->invalidateGeometryPipelines(); m_auxBindings.clear(); m_auxLayout.reset();
            m_pipelines.clear();
            m_layoutBindings.reset();
            m_bindings.clear(); m_opaqueBindings.clear(); m_reverseBindings.clear();
            m_uniform.reset();
            quint32 capacity = 1;
            while (capacity < items.size()) capacity *= 2;
            auto uniform = std::unique_ptr<QRhiBuffer>(rhi->newBuffer(QRhiBuffer::Dynamic,
                QRhiBuffer::UniformBuffer, int(stride * capacity)));
            if (!uniform->create()) {
                error = QStringLiteral("Failed to allocate dynamic per-object uniforms");
                return false;
            }
            m_uniform = std::move(uniform);
            m_uniformStride = stride;
            m_uniformCapacity = capacity;
        }
        if(!m_layoutBindings) {
            auto layout=std::unique_ptr<QRhiShaderResourceBindings>(rhi->newShaderResourceBindings());
            const auto &white=m_textures.at(nullptr);
            std::vector<QRhiShaderResourceBinding> layoutEntries{
                QRhiShaderResourceBinding::uniformBufferWithDynamicOffset(0,
                    QRhiShaderResourceBinding::VertexStage|QRhiShaderResourceBinding::FragmentStage,
                    m_uniform.get(),uniformBytes),
                QRhiShaderResourceBinding::sampledTexture(1,QRhiShaderResourceBinding::FragmentStage,
                    white.texture.get(),white.sampler.get()),
                QRhiShaderResourceBinding::sampledTexture(2,QRhiShaderResourceBinding::FragmentStage,
                    white.texture.get(),white.sampler.get())};
            const auto lightingBindings=m_lighting->bindings();
            layoutEntries.insert(layoutEntries.end(),lightingBindings.begin(),lightingBindings.end());
            for(int slot=10;slot<=15;++slot)
                layoutEntries.push_back(QRhiShaderResourceBinding::sampledTexture(slot,QRhiShaderResourceBinding::FragmentStage,
                    white.texture.get(),white.sampler.get()));
            layout->setBindings(layoutEntries.begin(),layoutEntries.end());
            if(!layout->create()) { error=QStringLiteral("Failed to create persistent pipeline layout bindings"); return false; }
            m_layoutBindings=std::move(layout);
        }
        for (const auto &key : activeBindings) {
            if (m_bindings.contains(key))
                continue;
            auto bindings = std::unique_ptr<QRhiShaderResourceBindings>(rhi->newShaderResourceBindings());
            ++stats.materialBindingBuilds;
            const auto &base = m_textures.at(key[0]);
            const auto &mr = m_textures.at(key[1]);
            std::vector<QRhiShaderResourceBinding> entries{
                QRhiShaderResourceBinding::uniformBufferWithDynamicOffset(0,
                    QRhiShaderResourceBinding::VertexStage | QRhiShaderResourceBinding::FragmentStage,
                    m_uniform.get(), uniformBytes),
                QRhiShaderResourceBinding::sampledTexture(1, QRhiShaderResourceBinding::FragmentStage,
                    base.texture.get(), base.sampler.get()),
                QRhiShaderResourceBinding::sampledTexture(2, QRhiShaderResourceBinding::FragmentStage,
                    mr.texture.get(), mr.sampler.get())};
            const auto lightingBindings=m_lighting->bindings();
            entries.insert(entries.end(),lightingBindings.begin(),lightingBindings.end());
            for(int i=2;i<7;++i) {
                const auto &texture=m_textures.at(key[i]);
                entries.push_back(QRhiShaderResourceBinding::sampledTexture(i<5?8+i:9+i,QRhiShaderResourceBinding::FragmentStage,
                    texture.texture.get(),texture.sampler.get()));
            }
            const auto &white=m_textures.at(nullptr);
            entries.push_back(QRhiShaderResourceBinding::sampledTexture(13,QRhiShaderResourceBinding::FragmentStage,
                white.texture.get(),white.sampler.get()));
            if(m_transmissionTarget) {
                auto opaque=std::unique_ptr<QRhiShaderResourceBindings>(rhi->newShaderResourceBindings());
                opaque->setBindings(entries.begin(),entries.end());
                if(!opaque->create()) { error="Cannot create opaque snapshot bindings"; return false; }
                m_opaqueBindings.emplace(key,std::move(opaque));
                entries.back()=QRhiShaderResourceBinding::sampledTexture(13,QRhiShaderResourceBinding::FragmentStage,
                    m_resolved?m_resolved.get():m_color.get(),m_transmissionSampler.get());
                auto reverse=std::unique_ptr<QRhiShaderResourceBindings>(rhi->newShaderResourceBindings());
                reverse->setBindings(entries.begin(),entries.end());
                if(!reverse->create()) { error="Cannot create reverse transmission bindings"; return false; }
                m_reverseBindings.emplace(key,std::move(reverse));
                entries.back()=QRhiShaderResourceBinding::sampledTexture(13,QRhiShaderResourceBinding::FragmentStage,
                    m_transmissionResolved?m_transmissionResolved.get():m_transmissionColor.get(),m_transmissionSampler.get());
            }
            bindings->setBindings(entries.begin(),entries.end());
            if (!bindings->create()) {
                error = QStringLiteral("Failed to create material resource bindings");
                return false;
            }
            m_bindings.emplace(key, std::move(bindings));
        }
        if(!m_auxLayout) {
            m_auxLayout.reset(rhi->newShaderResourceBindings());
            const auto &white=m_textures.at(nullptr);
            m_auxLayout->setBindings({QRhiShaderResourceBinding::uniformBufferWithDynamicOffset(0,
                QRhiShaderResourceBinding::VertexStage|QRhiShaderResourceBinding::FragmentStage,m_uniform.get(),uniformBytes),
                QRhiShaderResourceBinding::sampledTexture(1,QRhiShaderResourceBinding::FragmentStage,white.texture.get(),white.sampler.get()),
                QRhiShaderResourceBinding::uniformBuffer(7,QRhiShaderResourceBinding::VertexStage|QRhiShaderResourceBinding::FragmentStage,m_lighting->uniform())});
            if(!m_auxLayout->create()) { error="Cannot create lighting geometry layout"; return false; }
        }
        for(const auto &key:activeBindings) {
            if(m_auxBindings.contains(key)) continue;
            auto bindings=std::unique_ptr<QRhiShaderResourceBindings>(rhi->newShaderResourceBindings());
            const auto &base=m_textures.at(key[0]);
            bindings->setBindings({QRhiShaderResourceBinding::uniformBufferWithDynamicOffset(0,
                QRhiShaderResourceBinding::VertexStage|QRhiShaderResourceBinding::FragmentStage,m_uniform.get(),uniformBytes),
                QRhiShaderResourceBinding::sampledTexture(1,QRhiShaderResourceBinding::FragmentStage,base.texture.get(),base.sampler.get()),
                QRhiShaderResourceBinding::uniformBuffer(7,QRhiShaderResourceBinding::VertexStage|QRhiShaderResourceBinding::FragmentStage,m_lighting->uniform())});
            if(!bindings->create()) { error="Cannot create lighting geometry bindings"; return false; }
            m_auxBindings.emplace(key,std::move(bindings));
        }
        for (const auto &key : activePipelines) {
            if (m_pipelines.contains(key))
                continue;
            auto vert = shader(QStringLiteral("unlit.vert"), error);
            auto frag = shader(QStringLiteral("unlit.frag"), error);
            if (!error.isEmpty())
                return false;
            auto pipeline = std::unique_ptr<QRhiGraphicsPipeline>(rhi->newGraphicsPipeline());
            pipeline->setShaderStages({{QRhiShaderStage::Vertex, vert}, {QRhiShaderStage::Fragment, frag}});
            QRhiVertexInputLayout layout;
            layout.setBindings({{sizeof(Vertex)}});
            layout.setAttributes({
                {0, 0, QRhiVertexInputAttribute::Float3, offsetof(Vertex, position)},
                {0, 1, QRhiVertexInputAttribute::Float3, offsetof(Vertex, color)},
                {0, 2, QRhiVertexInputAttribute::Float3, offsetof(Vertex, normal)},
                {0, 3, QRhiVertexInputAttribute::Float2, offsetof(Vertex, uv)},
                {0, 4, QRhiVertexInputAttribute::Float4, offsetof(Vertex, tangent)}});
            pipeline->setVertexInputLayout(layout);
            pipeline->setShaderResourceBindings(m_layoutBindings.get());
            pipeline->setRenderPassDescriptor(m_scenePass.get());
            pipeline->setSampleCount(m_samples);
            pipeline->setDepthTest(true);
            pipeline->setDepthWrite(std::get<0>(key) != Material::AlphaMode::Blend && !std::get<3>(key));
            pipeline->setDepthOp(QRhiGraphicsPipeline::Less);
            pipeline->setCullMode(std::get<1>(key) ? QRhiGraphicsPipeline::None : QRhiGraphicsPipeline::Back);
            pipeline->setFrontFace(std::get<2>(key) ? QRhiGraphicsPipeline::CW : QRhiGraphicsPipeline::CCW);
            if (std::get<0>(key) == Material::AlphaMode::Blend) {
                QRhiGraphicsPipeline::TargetBlend blend;
                blend.enable = true;
                blend.srcColor = QRhiGraphicsPipeline::SrcAlpha;
                blend.dstColor = QRhiGraphicsPipeline::OneMinusSrcAlpha;
                pipeline->setTargetBlends({blend});
            }
            if (!pipeline->create()) {
                error = QStringLiteral("Failed to create surface pipeline (alpha=%1, samples=%2)")
                    .arg(int(std::get<0>(key))).arg(m_samples);
                return false;
            }
            m_pipelines.emplace(key, std::move(pipeline));
            ++stats.pipelineGenerations;
            qCInfo(render3dLog) << "Surface pipeline generation" << stats.pipelineGenerations << "samples" << m_samples;
        }
    }
    if(!m_lighting->ensurePipelines(m_auxLayout.get(),m_scenePass.get(),m_samples,error)) return false;
    stats.bufferBytes = 416 + (m_uniform ? quint64(m_uniformStride) * m_uniformCapacity : 0);
    for (const auto &[_, geometry] : m_geometry) stats.bufferBytes += geometry.bytes;
    stats.textureBytes = quint64(m_size.width()) * m_size.height()
        * quint64(8 * m_samples + 4 * m_samples + (m_samples > 1 ? 8 : 0));
    if(m_transmissionTarget) stats.textureBytes+=quint64(m_size.width())*m_size.height()*(8*m_samples+(m_samples>1?8:0));
    stats.sourceTextureBytes=0;
    for (const auto &[_, texture] : m_textures) stats.sourceTextureBytes += texture.bytes;
    stats.textureBytes+=stats.sourceTextureBytes;
    stats.textureBytes+=m_lighting->textureBytes;
    stats.geometryCount = int(m_geometry.size());
    stats.textureCount = int(m_textures.size());
    stats.visibleItems = int(items.size());
    m_textureBudgetDirty=false;
    return true;
}

void Renderer::draw(QRhiRenderTarget *target, QRhiCommandBuffer *cb,
                    const Camera &camera, const QQuaternion &rotation,
                    const QColor &clearColor, RenderStatistics &stats)
{
    QMatrix4x4 root;
    root.rotate(rotation);
    const auto view = camera.viewMatrix();
    const auto cpuProjection=camera.projectionMatrix(float(m_size.width()) / m_size.height());
    const auto projection = m_rhi->clipSpaceCorrMatrix()*cpuProjection;
    // Conservative CPU frustum rejection. Mesh bounds are computed once per
    // shared asset, not once per instance or frame. Offscreen instances retain
    // their shared geometry cache entry to avoid scroll upload churn.
    auto visible=m_items;
    std::erase_if(visible,[&](const RenderItem &item) {
        const auto &geometry=m_geometry.at(item.mesh.get());
        const auto clipMatrix=cpuProjection*view*root*item.world;
        unsigned outside=63;
        for(int corner=0;corner<8;++corner) {
            const QVector3D point(corner&1?geometry.upper.x():geometry.lower.x(),
                corner&2?geometry.upper.y():geometry.lower.y(),
                corner&4?geometry.upper.z():geometry.lower.z());
            const auto clip=clipMatrix*QVector4D(point,1);
            unsigned mask=0;
            if(clip.x() < -clip.w()) mask|=1;
            if(clip.x() > clip.w()) mask|=2;
            if(clip.y() < -clip.w()) mask|=4;
            if(clip.y() > clip.w()) mask|=8;
            if(clip.z() < -clip.w()) mask|=16;
            if(clip.z() > clip.w()) mask|=32;
            outside&=mask;
        }
        return outside!=0;
    });
    stats.visibleItems=int(visible.size());
    std::set<NodeId> visibleNodes;
    for(const auto &item:visible) visibleNodes.insert(item.node);
    // Opaque/mask first, then transparent objects far-to-near in camera space.
    std::stable_sort(m_items.begin(), m_items.end(), [&](const RenderItem &a, const RenderItem &b) {
        const bool transparentA = materialOf(a).alphaMode == Material::AlphaMode::Blend || transmissive(materialOf(a));
        const bool transparentB = materialOf(b).alphaMode == Material::AlphaMode::Blend || transmissive(materialOf(b));
        if (transparentA != transparentB) return !transparentA;
        if (!transparentA) return false;
        const auto center=[&](const RenderItem &item) {
            const auto &geometry=m_geometry.at(item.mesh.get());
            return (view*root*item.world).map((geometry.lower+geometry.upper)*0.5f).z();
        };
        return center(a)<center(b);
    });
    auto *updates=m_rhi->nextResourceUpdateBatch();
    std::array<float,100> frame{};
    const auto inverseProjection=cpuProjection.inverted();
    const auto inverseViewProjection=(cpuProjection*view).inverted();
    QVector3D lower(1e30f,1e30f,1e30f),upper=-lower;
    for(const auto &item:m_items) {
        const auto &geometry=m_geometry.at(item.mesh.get());
        for(int corner=0;corner<8;++corner) {
            const auto point=(root*item.world).map({corner&1?geometry.upper.x():geometry.lower.x(),
                corner&2?geometry.upper.y():geometry.lower.y(),corner&4?geometry.upper.z():geometry.lower.z()});
            for(int axis=0;axis<3;++axis) { lower[axis]=std::min(lower[axis],point[axis]); upper[axis]=std::max(upper[axis],point[axis]); }
        }
    }
    if(m_items.empty()) { lower={-1,-1,-1}; upper={1,1,1}; }
    const auto center=(lower+upper)*0.5f;
    const float radius=std::max(0.1f,(upper-lower).length()*0.5f);
    auto keyLight=m_lightingSettings.keyLightDirection.normalized();
    if(m_lightingSettings.keyLightCameraRelative)
        keyLight=QQuaternion::fromEulerAngles(camera.pitch,camera.yaw,0).rotatedVector(keyLight);
    QMatrix4x4 lightView;
    lightView.lookAt(center+keyLight*(radius+1),center,std::abs(keyLight.y())>0.99f?QVector3D(0,0,1):QVector3D(0,1,0));
    QVector3D lightLower(1e30f,1e30f,1e30f),lightUpper=-lightLower;
    for(int corner=0;corner<8;++corner) {
        const auto p=lightView.map({corner&1?upper.x():lower.x(),corner&2?upper.y():lower.y(),corner&4?upper.z():lower.z()});
        for(int axis=0;axis<3;++axis) { lightLower[axis]=std::min(lightLower[axis],p[axis]); lightUpper[axis]=std::max(lightUpper[axis],p[axis]); }
    }
    QMatrix4x4 lightProjection;
    const float padding=std::max(0.01f,radius*0.03f);
    lightProjection.ortho(lightLower.x()-padding,lightUpper.x()+padding,lightLower.y()-padding,lightUpper.y()+padding,
        std::max(0.001f,-lightUpper.z()-padding),-lightLower.z()+padding);
    const auto lightVp=m_rhi->clipSpaceCorrMatrix()*lightProjection*lightView;
    for(const auto &[offset,matrix]:std::array<std::pair<int,QMatrix4x4>,5>{
            std::pair{0,view},{16,cpuProjection},{32,inverseProjection},{48,inverseViewProjection},{64,lightVp}})
        std::copy_n(matrix.constData(),16,frame.begin()+offset);
    frame[80]=m_lightingSettings.imageBasedLighting && m_environment?1.0f:0;
    frame[81]=m_lightingSettings.environmentBackground && m_environment?1.0f:0;
    frame[82]=m_lightingSettings.ambientOcclusion?1.0f:0;
    frame[83]=m_lightingSettings.shadows?1.0f:0;
    frame[84]=m_lightingSettings.environmentIntensity;
    frame[85]=m_lightingSettings.environmentRotation*3.14159265359f/180;
    frame[86]=m_environment?float(m_environment->specular.size()-1):0;
    frame[87]=m_rhi->isClipDepthZeroToOne()?1.0f:0;
    frame[88]=float(m_size.width()); frame[89]=float(m_size.height());
    frame[90]=m_rhi->isYUpInFramebuffer()?1.0f:0;
    frame[91]=m_rhi->isYUpInNDC()!=m_rhi->isYUpInFramebuffer()?1.0f:0;
    frame[92]=m_lightingSettings.aoRadius; frame[93]=m_lightingSettings.aoStrength;
    frame[94]=m_lightingSettings.toneMapping?1.0f:0; frame[95]=m_lightingSettings.exposure;
    frame[96]=keyLight.x(); frame[97]=keyLight.y(); frame[98]=keyLight.z();
    frame[99]=m_lightingSettings.keyLightIntensity;
    updates->updateDynamicBuffer(m_lighting->uniform(),0,400,frame.data());
    const std::array<float,4> orientation{m_rhi->isYUpInFramebuffer()?1.0f:0,
        m_lightingSettings.toneMapping?1.0f:0,m_lightingSettings.exposure,0};
    updates->updateDynamicBuffer(m_presentUniform.get(),0,16,orientation.data());
    if (!m_items.empty()) {
        QByteArray bytes(qsizetype(m_uniformStride) * m_items.size(), char(0));
        for (std::size_t i = 0; i < m_items.size(); ++i) {
            const auto &item = m_items[i];
            const auto &material = materialOf(item);
            const auto model = root * item.world;
            const auto normal = model.inverted().transposed();
            const auto mvp = projection * view * model;
            std::array<float, 84> uniform{};
            std::copy_n(mvp.constData(), 16, uniform.begin());
            std::copy_n(model.constData(), 16, uniform.begin() + 16);
            std::copy_n(normal.constData(), 16, uniform.begin() + 32);
            auto base = material.baseColor;
            std::copy_n(&base[0], 4, uniform.begin() + 48);
            const auto eye = camera.position();
            uniform[52] = eye.x(); uniform[53] = eye.y(); uniform[54] = eye.z();
            uniform[56] = material.metallic; uniform[57] = material.roughness;
            uniform[58] = material.alphaCutoff;
            uniform[59] = material.shading == Material::Shading::MetallicRoughness ? 1.0f : 0.0f;
            uniform[60] = item.mesh->vertexColorsSrgb ? 1.0f : 0.0f;
            uniform[61] = float(material.alphaMode);
            // sRGB texture views perform the decode before filtering/mipmaps.
            uniform[62] = 0.0f;
            uniform[63] = model.determinant()<0?-1.0f:1.0f;
            uniform[64] = material.normalScale; uniform[65] = material.occlusionStrength;
            uniform[66] = material.normalTexture?1.0f:0.0f; uniform[67] = item.mesh->hasTangents?1.0f:0.0f;
            uniform[68] = material.emissiveFactor.x(); uniform[69] = material.emissiveFactor.y();
            uniform[79] = material.baseColorTexture?float(std::clamp(material.baseColorTexture->maxAnisotropy,1,8)):1.0f;
            uniform[70] = material.emissiveFactor.z(); uniform[71] = material.emissiveStrength;
            uniform[72] = transmissive(material)?std::clamp(material.transmission,0.0f,1.0f):0;
            uniform[73] = material.ior;
            uniform[74] = material.thickness;
            uniform[75] = material.attenuationDistance>0?1.0f/material.attenuationDistance:0;
            uniform[76] = material.attenuationColor.x(); uniform[77] = material.attenuationColor.y();
            uniform[78] = material.attenuationColor.z();
            uniform[80] = material.sparkle.x(); uniform[81] = material.sparkle.y();
            uniform[82] = material.sparkle.z(); uniform[83] = material.sparkle.w();
            std::memcpy(bytes.data() + qsizetype(i) * m_uniformStride, uniform.data(), uniformBytes);
        }
        updates->updateDynamicBuffer(m_uniform.get(), 0, bytes);
    }
    cb->resourceUpdate(updates);
    int auxiliaryDraws=0;
    m_lighting->beforeScene(cb,[&](QRhiGraphicsPipeline *pipeline,bool cameraVisibleOnly) {
        cb->setGraphicsPipeline(pipeline);
        for(std::size_t i=0;i<m_items.size();++i) {
            const auto &item=m_items[i];
            // SSAO uses camera depth. Shadows still need offscreen casters.
            if(cameraVisibleOnly && !visibleNodes.contains(item.node)) continue;
            const auto &material=materialOf(item);
            if(transmissive(material)) continue; // no opaque shadow/AO occluder for clear plastic
            const auto &geometry=m_geometry.at(item.mesh.get());
            const QRhiCommandBuffer::DynamicOffset offset{0,quint32(i)*m_uniformStride};
            cb->setShaderResources(m_auxBindings.at(materialKey(material)).get(),1,&offset);
            const QRhiCommandBuffer::VertexInput input(geometry.vertices.get(),0);
            cb->setVertexInput(0,1,&input,geometry.indices.get(),0,QRhiCommandBuffer::IndexUInt32);
            cb->drawIndexed(geometry.indexCount); ++auxiliaryDraws;
        }
    });
    const float clearAlpha = float(clearColor.alphaF());
    const auto linearClear = QColor::fromRgbF(linearChannel(float(clearColor.redF())) * clearAlpha,
        linearChannel(float(clearColor.greenF())) * clearAlpha,
        linearChannel(float(clearColor.blueF())) * clearAlpha, clearAlpha);
    stats.transmissionPasses=0;
    stats.transmissionFallbackItems=0;
    int sceneDraws=0;
    const auto drawItem=[&](std::size_t i,const auto &bindings) {
        const auto &item=m_items[i];
        const auto &material=materialOf(item);
        const auto &geometry=m_geometry.at(item.mesh.get());
        cb->setGraphicsPipeline(m_pipelines.at({material.alphaMode,material.doubleSided,
            item.world.determinant()<0,transmissive(material)}).get());
        const QRhiCommandBuffer::DynamicOffset offset{0,quint32(i)*m_uniformStride};
        cb->setShaderResources(bindings.at(materialKey(material)).get(),1,&offset);
        const QRhiCommandBuffer::VertexInput input(geometry.vertices.get(),0);
        cb->setVertexInput(0,1,&input,geometry.indices.get(),0,QRhiCommandBuffer::IndexUInt32);
        cb->drawIndexed(geometry.indexCount); ++sceneDraws;
    };
    const auto beginScene=[&](QRhiRenderTarget *sceneTarget) {
        cb->beginPass(sceneTarget,linearClear,{1.0f,0});
        cb->setViewport({0,0,float(m_size.width()),float(m_size.height())});
    };
    bool latestInScene=true;
    if(!m_transmissionTarget) {
        beginScene(m_sceneTarget.get()); m_lighting->background(cb);
        for(std::size_t i=0;i<m_items.size();++i)
            if(visibleNodes.contains(m_items[i].node)) drawItem(i,m_bindings);
        cb->endPass();
    } else {
        // The snapshot for a batch must contain every earlier overlapping surface.
        // Disjoint screen footprints can share a batch. Bounded passes avoid an
        // unbounded number of full-screen copies for a large shelf.
        std::vector<std::size_t> opaque;
        std::vector<std::vector<std::size_t>> batches;
        std::vector<QRectF> written;
        const QRectF viewport(0,0,m_size.width(),m_size.height());
        const auto bounds=[&](const RenderItem &item,float padding) {
            const auto &geometry=m_geometry.at(item.mesh.get());
            const auto model=root*item.world;
            const float scale=std::max({model.column(0).toVector3D().length(),
                model.column(1).toVector3D().length(),model.column(2).toVector3D().length()});
            const QVector3D expansion(padding*scale,padding*scale,padding*scale);
            QVector3D low(1e30f,1e30f,1e30f),high=-low;
            for(int corner=0;corner<8;++corner) {
                const auto point=model.map({corner&1?geometry.upper.x():geometry.lower.x(),
                    corner&2?geometry.upper.y():geometry.lower.y(),corner&4?geometry.upper.z():geometry.lower.z()});
                for(int axis=0;axis<3;++axis) { low[axis]=std::min(low[axis],point[axis]); high[axis]=std::max(high[axis],point[axis]); }
            }
            low-=expansion; high+=expansion;
            float left=1e30f,top=1e30f,right=-left,bottom=-top;
            for(int corner=0;corner<8;++corner) {
                const auto clip=cpuProjection*view*QVector4D(corner&1?high.x():low.x(),corner&2?high.y():low.y(),corner&4?high.z():low.z(),1);
                if(clip.w()<=camera.nearPlane) return viewport;
                const float x=(clip.x()/clip.w()*0.5f+0.5f)*m_size.width();
                const float y=(0.5f-clip.y()/clip.w()*0.5f)*m_size.height();
                left=std::min(left,x); right=std::max(right,x); top=std::min(top,y); bottom=std::max(bottom,y);
            }
            return QRectF(QPointF(left,top),QPointF(right,bottom)).intersected(viewport);
        };
        const int limit=std::clamp(m_lightingSettings.transmissionPassLimit,1,16);
        for(std::size_t i=0;i<m_items.size();++i) {
            const auto &item=m_items[i];
            if(!visibleNodes.contains(item.node)) continue;
            const auto &material=materialOf(item);
            if(!transmissive(material) && material.alphaMode!=Material::AlphaMode::Blend) { opaque.push_back(i); continue; }
            if(batches.empty()) batches.emplace_back();
            const auto drawn=bounds(item,0).adjusted(-1,-1,1,1);
            const float roughness=std::clamp(material.roughness,0.045f,1.0f);
            const float ior=material.ior==0?10000.0f:std::max(1.0f,material.ior);
            const float samplingRadius=1+12*roughness*roughness*std::clamp(ior-1,0.0f,1.0f);
            const auto sampled=bounds(item,std::max(0.0f,material.thickness)).adjusted(-samplingRadius,-samplingRadius,samplingRadius,samplingRadius);
            if(transmissive(material) && std::any_of(written.begin(),written.end(),[&](const QRectF &rect) { return sampled.intersects(rect); })) {
                batches.emplace_back(); written.clear();
            }
            batches.back().push_back(i); written.push_back(drawn);
        }
        if(int(batches.size())>limit) {
            // Spend the available passes on the nearest layers (e.g. the focused
            // DVD), rather than exhausting them on distant shelf surfaces.
            const auto mergedCount=batches.size()-std::size_t(limit)+1;
            for(std::size_t group=1;group<mergedCount;++group) {
                for(const auto i:batches[group])
                    if(transmissive(materialOf(m_items[i]))) ++stats.transmissionFallbackItems;
                batches.front().insert(batches.front().end(),batches[group].begin(),batches[group].end());
            }
            batches.erase(batches.begin()+1,batches.begin()+qsizetype(mergedCount));
        }
        beginScene(m_sceneTarget.get()); m_lighting->background(cb);
        for(const auto i:opaque) drawItem(i,m_opaqueBindings);
        cb->endPass();
        for(const auto &batch:batches) {
            auto *destination=latestInScene?m_transmissionTarget.get():m_sceneTarget.get();
            beginScene(destination);
            // Rebuild opaque depth instead of relying on non-portable preservation
            // of MSAA color and renderbuffer depth across render passes.
            for(const auto i:opaque) drawItem(i,m_opaqueBindings);
            cb->setGraphicsPipeline(m_copyPipeline.get());
            cb->setShaderResources(latestInScene?m_presentBindings.get():m_transmissionPresentBindings.get());
            cb->draw(3); ++sceneDraws;
            for(const auto i:batch) drawItem(i,latestInScene?m_reverseBindings:m_bindings);
            cb->endPass(); latestInScene=!latestInScene;
            ++stats.transmissionPasses;
        }
    }
    cb->beginPass(target,Qt::black,{1.0f,0});
    cb->setGraphicsPipeline(m_presentPipeline.get());
    cb->setViewport({0,0,float(m_size.width()),float(m_size.height())});
    cb->setShaderResources(latestInScene?m_presentBindings.get():m_transmissionPresentBindings.get());
    cb->draw(3); cb->endPass();
    stats.drawCalls=sceneDraws+1+auxiliaryDraws+m_lighting->drawCalls;
}
} // namespace darkeye::render3d
