#include "render3d/GltfLoader.h"
#include "cgltf.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QBuffer>
#include <QLoggingCategory>
#include <QUrl>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <map>
#include <set>

Q_LOGGING_CATEGORY(gltfLog,"darkeye.render3d.assets")

namespace darkeye::render3d {
namespace {
constexpr qint64 maxFileBytes = 256 * 1024 * 1024;
constexpr std::size_t maxVertices = 2000000;

struct Dependency {
    QString path;
    qint64 size = -1;
    qint64 modified = 0;
};

struct IoContext {
    std::vector<Dependency> dependencies;
    qint64 totalBytes = 0;
    QString error;

    void depend(const QString &path)
    {
        const QFileInfo info(path);
        dependencies.push_back({info.absoluteFilePath(), info.size(), info.lastModified().toMSecsSinceEpoch()});
    }

    QByteArray read(const QString &path)
    {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly)) {
            error = QStringLiteral("Cannot read %1: %2").arg(path, file.errorString());
            depend(path); // a missing texture appearing later invalidates cache
            return {};
        }
        if (file.size() > maxFileBytes - totalBytes) {
            error = QStringLiteral("Asset exceeds the 256 MiB input budget: %1").arg(path);
            return {};
        }
        depend(path);
        totalBytes += file.size();
        return file.readAll();
    }
};

cgltf_result readFile(const cgltf_memory_options *memory, const cgltf_file_options *files,
                      const char *path, cgltf_size *size, void **data)
{
    auto &context = *static_cast<IoContext *>(files->user_data);
    const auto bytes = context.read(QString::fromUtf8(path));
    if (!context.error.isEmpty())
        return cgltf_result_io_error;
    const auto required = *size ? *size : static_cast<cgltf_size>(bytes.size());
    if (required > static_cast<cgltf_size>(bytes.size()))
        return cgltf_result_data_too_short;
    void *result = memory->alloc_func ? memory->alloc_func(memory->user_data, required)
                                     : std::malloc(required);
    if (!result)
        return cgltf_result_out_of_memory;
    std::memcpy(result, bytes.constData(), required);
    *data = result;
    *size = required;
    return cgltf_result_success;
}

void releaseFile(const cgltf_memory_options *memory, const cgltf_file_options *, void *data)
{
    if (memory->free_func)
        memory->free_func(memory->user_data, data);
    else
        std::free(data);
}

QString resultName(cgltf_result result)
{
    switch (result) {
    case cgltf_result_success: return QStringLiteral("success");
    case cgltf_result_data_too_short: return QStringLiteral("truncated buffer/file");
    case cgltf_result_unknown_format: return QStringLiteral("unknown format");
    case cgltf_result_invalid_json: return QStringLiteral("invalid JSON");
    case cgltf_result_invalid_gltf: return QStringLiteral("invalid glTF structure/accessor/index");
    case cgltf_result_file_not_found: return QStringLiteral("file not found");
    case cgltf_result_io_error: return QStringLiteral("file I/O error");
    case cgltf_result_out_of_memory: return QStringLiteral("out of memory");
    case cgltf_result_legacy_gltf: return QStringLiteral("glTF 1.x is unsupported");
    default: return QStringLiteral("parser error %1").arg(int(result));
    }
}

bool nodeTransform(const cgltf_node &node, Transform &result, QString &error)
{
    if (!node.has_matrix) {
        if (node.has_translation)
            result.translation = {node.translation[0], node.translation[1], node.translation[2]};
        if (node.has_rotation)
            result.rotation = {node.rotation[3], node.rotation[0], node.rotation[1], node.rotation[2]};
        if (node.has_scale)
            result.scale = {node.scale[0], node.scale[1], node.scale[2]};
        if (result.isFinite())
            return true;
        error = QStringLiteral("Node contains invalid/non-finite TRS");
        return false;
    }
    QMatrix4x4 matrix;
    std::copy_n(node.matrix, 16, matrix.data());
    if(std::abs(matrix(3,0))>1e-6f || std::abs(matrix(3,1))>1e-6f
        || std::abs(matrix(3,2))>1e-6f || std::abs(matrix(3,3)-1)>1e-6f) {
        error=QStringLiteral("Node matrix must be affine TRS");
        return false;
    }
    QVector3D axes[3] = {matrix.column(0).toVector3D(), matrix.column(1).toVector3D(),
                         matrix.column(2).toVector3D()};
    result.translation = matrix.column(3).toVector3D();
    result.scale = {axes[0].length(), axes[1].length(), axes[2].length()};
    if (matrix.determinant() < 0)
        result.scale.setX(-result.scale.x());
    for (int axis = 0; axis < 3; ++axis) {
        if (qFuzzyIsNull(result.scale[axis])) {
            error = QStringLiteral("Degenerate node matrix cannot be decomposed into TRS");
            return false;
        }
        axes[axis] /= result.scale[axis];
    }
    if (std::abs(QVector3D::dotProduct(axes[0], axes[1])) > 1e-4f
        || std::abs(QVector3D::dotProduct(axes[1], axes[2])) > 1e-4f
        || std::abs(QVector3D::dotProduct(axes[0], axes[2])) > 1e-4f) {
        error = QStringLiteral("Sheared node matrices are unsupported");
        return false;
    }
    QMatrix3x3 rotation;
    for (int row = 0; row < 3; ++row)
        for (int column = 0; column < 3; ++column)
            rotation(row, column) = axes[column][row];
    result.rotation = QQuaternion::fromRotationMatrix(rotation);
    if (!result.isFinite()) {
        error = QStringLiteral("Non-finite node matrix");
        return false;
    }
    return true;
}

struct Converter {
    IoContext &io;
    QDir directory;
    ModelAsset &asset;
    QString error{};
    std::size_t verticesRead = 0;
    quint64 decodedImageBytes=0;
    std::map<const cgltf_primitive *, MeshHandle> meshes{};
    std::map<const cgltf_material *, MaterialHandle> materials{};
    std::map<std::pair<const cgltf_texture *, bool>, std::shared_ptr<const TextureData>> textures{};

    std::shared_ptr<const TextureData> texture(const cgltf_texture_view &view, bool srgb)
    {
        if (!view.texture)
            return {};
        if (view.texcoord != 0 || view.has_transform) {
            error = QStringLiteral("Only TEXCOORD_0 without KHR_texture_transform is supported");
            return {};
        }
        const auto key = std::make_pair(view.texture, srgb);
        if (textures.contains(key))
            return textures.at(key);
        const auto *image = view.texture->image;
        if (!image) {
            error = QStringLiteral("Texture has no supported PNG/JPEG image (compressed texture extension?)");
            return {};
        }
        QByteArray bytes;
        QString source;
        if (image->buffer_view) {
            const auto *data = cgltf_buffer_view_data(image->buffer_view);
            if (!data || image->buffer_view->size > maxFileBytes) {
                error = QStringLiteral("Invalid embedded image buffer view");
                return {};
            }
            bytes = QByteArray(reinterpret_cast<const char *>(data), qsizetype(image->buffer_view->size));
            source = QStringLiteral("embedded:%1").arg(QString::fromUtf8(image->name ? image->name : "image"));
        } else if (image->uri) {
            const QString uri = QString::fromUtf8(image->uri);
            if (uri.startsWith("data:")) {
                const auto comma = uri.indexOf(',');
                if (comma < 0 || !uri.left(comma).endsWith(";base64")) {
                    error = QStringLiteral("Unsupported image data URI");
                    return {};
                }
                bytes = QByteArray::fromBase64(uri.mid(comma + 1).toLatin1(), QByteArray::AbortOnBase64DecodingErrors);
                source = QStringLiteral("data:image");
            } else {
                const QUrl url(uri);
                if (!url.isRelative()) {
                    error = QStringLiteral("Remote/absolute image URI is unsupported: %1").arg(uri);
                    return {};
                }
                source = directory.absoluteFilePath(QUrl::fromPercentEncoding(uri.toUtf8()));
                bytes = io.read(source);
                if (!io.error.isEmpty()) {
                    asset.warnings.append(io.error + QStringLiteral("; using magenta fallback texture"));
                    io.error.clear();
                }
            }
        }
        auto result = std::make_shared<TextureData>();
        result->source = source;
        result->srgb = srgb;
        QBuffer buffer(&bytes);
        buffer.open(QIODevice::ReadOnly);
        QImageReader reader(&buffer);
        const auto dimensions=reader.size();
        const quint64 decodedBytes=dimensions.isValid()?quint64(dimensions.width())*dimensions.height()*4:0;
        if(dimensions.width()>8192 || dimensions.height()>8192
            || decodedBytes>128*1024*1024-decodedImageBytes) {
            error=QStringLiteral("Texture dimension/128 MiB decoded-image budget exceeded: %1").arg(source);
            return {};
        }
        result->image = reader.read();
        if (result->image.isNull()) {
            asset.warnings.append(QStringLiteral("Cannot decode %1; using magenta fallback texture").arg(source));
            result->image = QImage(2, 2, QImage::Format_RGBA8888);
            result->image.fill(Qt::magenta);
        } else decodedImageBytes+=decodedBytes;
        result->image = result->image.convertToFormat(QImage::Format_RGBA8888);
        // Qt images and glTF UVs both use top-left image origin. No flip here.
        if (const auto *sampler = view.texture->sampler) {
            const auto nearest = [](cgltf_filter_type filter) {
                return filter == cgltf_filter_type_nearest
                    || filter == cgltf_filter_type_nearest_mipmap_nearest
                    || filter == cgltf_filter_type_nearest_mipmap_linear;
            };
            result->minFilter = nearest(sampler->min_filter) ? TextureData::Filter::Nearest : TextureData::Filter::Linear;
            result->magFilter = nearest(sampler->mag_filter) ? TextureData::Filter::Nearest : TextureData::Filter::Linear;
            switch(sampler->min_filter) {
            case cgltf_filter_type_nearest_mipmap_nearest:
            case cgltf_filter_type_linear_mipmap_nearest: result->mipmap=TextureData::Mipmap::Nearest; break;
            case cgltf_filter_type_nearest_mipmap_linear:
            case cgltf_filter_type_linear_mipmap_linear: result->mipmap=TextureData::Mipmap::Linear; break;
            default:result->mipmap=TextureData::Mipmap::None;break;
            }
            const auto wrap = [](cgltf_wrap_mode mode) {
                return mode == cgltf_wrap_mode_clamp_to_edge ? TextureData::Wrap::ClampToEdge
                    : mode == cgltf_wrap_mode_mirrored_repeat ? TextureData::Wrap::MirroredRepeat : TextureData::Wrap::Repeat;
            };
            result->wrapU = wrap(sampler->wrap_s);
            result->wrapV = wrap(sampler->wrap_t);
        }
        textures[key] = result;
        return result;
    }

    MaterialHandle material(const cgltf_material *source)
    {
        if (materials.contains(source))
            return materials.at(source);
        auto result = std::make_shared<Material>();
        result->shading = Material::Shading::MetallicRoughness;
        result->metallic = 1;
        result->roughness = 1;
        if (source) {
            result->name = QString::fromUtf8(source->name ? source->name : "");
            result->shading = source->unlit ? Material::Shading::Unlit : Material::Shading::MetallicRoughness;
            const auto &pbr = source->pbr_metallic_roughness;
            result->baseColor = {pbr.base_color_factor[0], pbr.base_color_factor[1], pbr.base_color_factor[2], pbr.base_color_factor[3]};
            result->metallic = pbr.metallic_factor;
            result->roughness = pbr.roughness_factor;
            result->baseColorTexture = texture(pbr.base_color_texture, true);
            result->metallicRoughnessTexture = texture(pbr.metallic_roughness_texture, false);
            result->normalTexture = texture(source->normal_texture,false);
            result->normalScale = source->normal_texture.texture?source->normal_texture.scale:1;
            result->occlusionTexture = texture(source->occlusion_texture,false);
            result->occlusionStrength = source->occlusion_texture.texture?source->occlusion_texture.scale:1;
            result->emissiveTexture = texture(source->emissive_texture,true);
            result->emissiveFactor={source->emissive_factor[0],source->emissive_factor[1],source->emissive_factor[2]};
            result->emissiveStrength=source->has_emissive_strength?source->emissive_strength.emissive_strength:1;
            if(source->has_transmission) {
                result->transmission=source->transmission.transmission_factor;
                result->transmissionTexture=texture(source->transmission.transmission_texture,false);
                if(source->unlit) { error="Transmission cannot be combined with KHR_materials_unlit"; return {}; }
            }
            if(source->has_ior) result->ior=source->ior.ior;
            if(source->has_volume) {
                const auto &volume=source->volume;
                if(volume.attenuation_distance<=0) { error="Volume attenuationDistance must be positive"; return {}; }
                result->thickness=volume.thickness_factor;
                result->thicknessTexture=texture(volume.thickness_texture,false);
                result->attenuationColor={volume.attenuation_color[0],volume.attenuation_color[1],volume.attenuation_color[2]};
                // cgltf represents omitted attenuationDistance by FLT_MAX.
                result->attenuationDistance=volume.attenuation_distance;
            }
            result->doubleSided = source->double_sided;
            result->alphaCutoff = source->alpha_cutoff;
            result->alphaMode = source->alpha_mode == cgltf_alpha_mode_blend ? Material::AlphaMode::Blend
                : source->alpha_mode == cgltf_alpha_mode_mask ? Material::AlphaMode::Mask : Material::AlphaMode::Opaque;
            if (source->has_clearcoat || source->has_pbr_specular_glossiness)
                asset.warnings.append(QStringLiteral("Material %1 has lighting maps/extensions outside the initial material subset").arg(result->name));
        }
        materials[source] = result;
        for(float value:{result->baseColor.x(),result->baseColor.y(),result->baseColor.z(),
            result->baseColor.w(),result->metallic,result->roughness,result->alphaCutoff,result->occlusionStrength,
            result->emissiveFactor.x(),result->emissiveFactor.y(),result->emissiveFactor.z(),result->transmission,
            result->attenuationColor.x(),result->attenuationColor.y(),result->attenuationColor.z()})
            if(!std::isfinite(value) || value<0 || value>1) {
                error=QStringLiteral("Material %1 contains an invalid factor outside [0,1]").arg(result->name);
                return {};
            }
        if(!std::isfinite(result->normalScale) || !std::isfinite(result->emissiveStrength) || result->emissiveStrength<0) {
            error=QStringLiteral("Material %1 has invalid normal scale or emissive strength").arg(result->name);
            return {};
        }
        if(!std::isfinite(result->ior) || (result->ior!=0 && result->ior<1)
            || !std::isfinite(result->thickness) || result->thickness<0
            || !std::isfinite(result->attenuationDistance) || result->attenuationDistance<0) {
            error=QStringLiteral("Material %1 has invalid IOR, thickness or attenuation distance").arg(result->name);
            return {};
        }
        return result;
    }

    MeshHandle mesh(const cgltf_primitive &primitive)
    {
        if (meshes.contains(&primitive))
            return meshes.at(&primitive);
        if (primitive.type != cgltf_primitive_type_triangles || primitive.has_draco_mesh_compression
            || primitive.targets_count) {
            error = QStringLiteral("Only static triangle primitives without Draco/morph targets are supported");
            return {};
        }
        const cgltf_accessor *positions = nullptr, *normals = nullptr, *uv = nullptr, *colors = nullptr, *tangents = nullptr;
        for (cgltf_size i = 0; i < primitive.attributes_count; ++i) {
            const auto &attribute = primitive.attributes[i];
            if (attribute.type == cgltf_attribute_type_position) positions = attribute.data;
            if (attribute.type == cgltf_attribute_type_normal) normals = attribute.data;
            if (attribute.type == cgltf_attribute_type_tangent) tangents = attribute.data;
            if (attribute.type == cgltf_attribute_type_texcoord && attribute.index == 0) uv = attribute.data;
            if (attribute.type == cgltf_attribute_type_color && attribute.index == 0) colors = attribute.data;
        }
        if (!positions || positions->type != cgltf_type_vec3 || positions->count > maxVertices - verticesRead) {
            error = QStringLiteral("Missing/invalid POSITION or the two-million-vertex budget was exceeded");
            return {};
        }
        const auto count = positions->count;
        if ((normals && (normals->count != count || normals->type != cgltf_type_vec3))
            || (uv && (uv->count != count || uv->type != cgltf_type_vec2))
            || (tangents && (tangents->count != count || tangents->type != cgltf_type_vec4))
            || (colors && (colors->count != count || (colors->type != cgltf_type_vec3 && colors->type != cgltf_type_vec4)))) {
            error = QStringLiteral("Primitive attribute dimensions/counts do not match POSITION");
            return {};
        }
        auto result = std::make_shared<MeshData>();
        result->vertexColorsSrgb = false;
        result->hasTangents = tangents!=nullptr;
        result->vertices.resize(count);
        const auto unpack = [&](const cgltf_accessor *accessor, int components, auto assign) {
            if (!accessor)
                return true;
            std::vector<float> values(count * components);
            if (cgltf_accessor_unpack_floats(accessor, values.data(), values.size()) != values.size())
                return false;
            for (std::size_t i = 0; i < count; ++i)
                assign(result->vertices[i], values.data() + i * components);
            return true;
        };
        if (!unpack(positions, 3, [](Vertex &v, const float *p) { std::copy_n(p, 3, v.position.begin()); })
            || !unpack(normals, 3, [](Vertex &v, const float *p) { std::copy_n(p, 3, v.normal.begin()); })
            || !unpack(uv, 2, [](Vertex &v, const float *p) { std::copy_n(p, 2, v.uv.begin()); })
            || !unpack(tangents, 4, [](Vertex &v, const float *p) { std::copy_n(p, 4, v.tangent.begin()); })
            || !unpack(colors, colors && colors->type == cgltf_type_vec4 ? 4 : 3,
                [&](Vertex &v, const float *p) {
                    std::copy_n(p, 3, v.color.begin());
                    if (colors->type == cgltf_type_vec4 && p[3] != 1)
                        error = QStringLiteral("Vertex alpha other than one is not supported yet");
                })) {
            error = QStringLiteral("Cannot unpack primitive attributes");
            return {};
        }
        if (!colors)
            for (auto &vertex : result->vertices)
                vertex.color = {1, 1, 1};
        const auto indexCount = primitive.indices ? primitive.indices->count : count;
        if (indexCount > maxVertices * 6 || indexCount % 3) {
            error = QStringLiteral("Invalid/excessive triangle index count");
            return {};
        }
        result->indices.resize(indexCount);
        if (primitive.indices) {
            if (cgltf_accessor_unpack_indices(primitive.indices, result->indices.data(),
                    sizeof(std::uint32_t), indexCount) != indexCount) {
                error = QStringLiteral("Cannot unpack indices (sparse indices are unsupported)");
                return {};
            }
        } else {
            for (std::uint32_t i = 0; i < count; ++i)
                result->indices[i] = i;
        }
        if (!error.isEmpty())
            return {};
        // Validate decoded data before normal generation indexes the vertex array.
        error = result->validationError();
        if (!error.isEmpty())
            return {};
        if(normals) {
            bool repaired=false;
            for(auto &vertex:result->vertices) {
                const QVector3D normal(vertex.normal[0],vertex.normal[1],vertex.normal[2]);
                const auto normalized=normal.isNull()?QVector3D(0,0,1):normal.normalized();
                repaired|=normal.isNull();
                vertex.normal={normalized.x(),normalized.y(),normalized.z()};
            }
            if(repaired) asset.warnings.append(QStringLiteral("Zero-length normals replaced by (0,0,1)"));
        } else {
            asset.warnings.append(QStringLiteral("Primitive has no NORMAL; generating area-weighted vertex normals"));
            std::vector<QVector3D> generated(count);
            for (std::size_t i = 0; i < indexCount; i += 3) {
                const auto position = [&](std::uint32_t index) {
                    const auto &p = result->vertices[index].position;
                    return QVector3D(p[0], p[1], p[2]);
                };
                const auto a = result->indices[i], b = result->indices[i+1], c = result->indices[i+2];
                const auto normal = QVector3D::crossProduct(position(b) - position(a), position(c) - position(a));
                generated[a] += normal; generated[b] += normal; generated[c] += normal;
            }
            for (std::size_t i = 0; i < count; ++i) {
                const auto n = generated[i].isNull() ? QVector3D(0,0,1) : generated[i].normalized();
                result->vertices[i].normal = {n.x(), n.y(), n.z()};
            }
        }
        if (!uv)
            asset.warnings.append(QStringLiteral("Primitive has no TEXCOORD_0; using (0,0)"));
        if(tangents) for(auto &vertex:result->vertices) {
            const auto t=QVector3D(vertex.tangent[0],vertex.tangent[1],vertex.tangent[2]).normalized();
            vertex.tangent[0]=t.x(); vertex.tangent[1]=t.y(); vertex.tangent[2]=t.z();
        }
        verticesRead += count;
        meshes[&primitive] = result;
        return result;
    }
};
}

struct GltfLoader::Impl {
    struct Entry { std::weak_ptr<const ModelAsset> asset; std::vector<Dependency> dependencies; };
    std::map<QString, Entry> cache;
};

GltfLoader::GltfLoader() : m_impl(std::make_unique<Impl>()) {}
GltfLoader::~GltfLoader() = default;
void GltfLoader::clearCache() { m_impl->cache.clear(); }

ModelLoadResult GltfLoader::load(const QString &path)
{
    const QFileInfo info(path);
    const auto absolute = path.startsWith(":/") ? path
        : (info.exists() ? info.canonicalFilePath() : info.absoluteFilePath());
    const auto fail = [&](const QString &message) {
        return ModelLoadResult{{}, QStringLiteral("%1: %2").arg(absolute, message)};
    };
    if (const auto it = m_impl->cache.find(absolute); it != m_impl->cache.end()) {
        bool current = true;
        for (const auto &dependency : it->second.dependencies) {
            const QFileInfo file(dependency.path);
            if (file.size() != dependency.size || file.lastModified().toMSecsSinceEpoch() != dependency.modified) {
                current = false;
                break;
            }
        }
        if (current)
            if (auto asset = it->second.asset.lock())
                return {asset, {}};
    }
    IoContext io;
    const auto bytes = io.read(absolute);
    if (!io.error.isEmpty())
        return fail(io.error);
    cgltf_options options{};
    options.file.read = readFile;
    options.file.release = releaseFile;
    options.file.user_data = &io;
    cgltf_data *raw = nullptr;
    auto result = cgltf_parse(&options, bytes.constData(), cgltf_size(bytes.size()), &raw);
    if (result != cgltf_result_success)
        return fail(resultName(result));
    std::unique_ptr<cgltf_data, decltype(&cgltf_free)> data(raw, cgltf_free);
    result = cgltf_load_buffers(&options, raw, absolute.toUtf8().constData());
    if (result != cgltf_result_success)
        return fail(io.error.isEmpty() ? resultName(result) : io.error);
    result = cgltf_validate(raw);
    if (result != cgltf_result_success)
        return fail(resultName(result));
    for (cgltf_size i = 0; i < raw->extensions_required_count; ++i)
        if (QString::fromUtf8(raw->extensions_required[i]) != "KHR_materials_unlit"
            && QString::fromUtf8(raw->extensions_required[i]) != "KHR_materials_emissive_strength"
            && QString::fromUtf8(raw->extensions_required[i]) != "KHR_materials_transmission"
            && QString::fromUtf8(raw->extensions_required[i]) != "KHR_materials_ior"
            && QString::fromUtf8(raw->extensions_required[i]) != "KHR_materials_volume")
            return fail(QStringLiteral("Unsupported required extension: %1").arg(QString::fromUtf8(raw->extensions_required[i])));
    for (cgltf_size i = 0; i < raw->buffer_views_count; ++i)
        if (raw->buffer_views[i].has_meshopt_compression)
            return fail(QStringLiteral("EXT_meshopt_compression is unsupported"));
    if (raw->nodes_count > 100000)
        return fail(QStringLiteral("Node budget exceeded"));
    auto asset = std::make_shared<ModelAsset>();
    asset->sourcePath = absolute;
    if (raw->animations_count)
        asset->warnings.append(QStringLiteral("glTF animations ignored; only the static pose is imported"));
    Converter converter{io, QFileInfo(absolute).absoluteDir(), *asset};
    std::set<const cgltf_node *> visited;
    std::function<bool(const cgltf_node *, int, int)> visit = [&](const cgltf_node *node, int parent, int depth) {
        if (!node || depth > 128 || !visited.insert(node).second) {
            converter.error = QStringLiteral("Repeated/cyclic node or hierarchy depth exceeds 128");
            return false;
        }
        if (node->skin || node->has_mesh_gpu_instancing) {
            converter.error = QStringLiteral("Skinning and glTF instancing extensions are unsupported");
            return false;
        }
        ModelNode output;
        output.name = QString::fromUtf8(node->name ? node->name : "");
        output.parent = parent;
        if (!nodeTransform(*node, output.transform, converter.error))
            return false;
        if (node->mesh)
            for (cgltf_size i = 0; i < node->mesh->primitives_count; ++i) {
                const auto &primitive = node->mesh->primitives[i];
                auto mesh = converter.mesh(primitive);
                if (!mesh || !converter.error.isEmpty())
                    return false;
                auto material = converter.material(primitive.material);
                if (!converter.error.isEmpty())
                    return false;
                output.primitives.push_back({std::move(mesh), std::move(material)});
            }
        const int index = int(asset->nodes.size());
        asset->nodes.push_back(std::move(output));
        for (cgltf_size i = 0; i < node->children_count; ++i)
            if (!visit(node->children[i], index, depth + 1))
                return false;
        return true;
    };
    const auto *scene = raw->scene ? raw->scene : (raw->scenes_count ? &raw->scenes[0] : nullptr);
    if (scene) {
        for (cgltf_size i = 0; i < scene->nodes_count; ++i)
            if (!visit(scene->nodes[i], -1, 0))
                return fail(converter.error);
    } else {
        for (cgltf_size i = 0; i < raw->nodes_count; ++i)
            if (!raw->nodes[i].parent && !visit(&raw->nodes[i], -1, 0))
                return fail(converter.error);
    }
    m_impl->cache[absolute] = {asset, std::move(io.dependencies)};
    asset->warnings.removeDuplicates();
    for(const auto &warning:asset->warnings)
        qCWarning(gltfLog).noquote()<<absolute<<warning;
    return {asset, {}};
}
} // namespace darkeye::render3d
