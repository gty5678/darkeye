#pragma once

#include <QImage>
#include <QString>
#include <QVector4D>
#include <QVector3D>
#include <memory>

namespace darkeye::render3d {

// CPU asset. GPU textures are owned separately by each renderer/QRhi.
struct TextureData {
    enum class Filter { Nearest, Linear };
    enum class Wrap { Repeat, ClampToEdge, MirroredRepeat };
    enum class Mipmap { None, Nearest, Linear };
    QImage image;
    QString source;
    Filter minFilter = Filter::Linear;
    Filter magFilter = Filter::Linear;
    Mipmap mipmap = Mipmap::None;
    int maxAnisotropy = 1; // portable shader filtering, clamped to 1..8 taps
    Wrap wrapU = Wrap::Repeat;
    Wrap wrapV = Wrap::Repeat;
    bool srgb = true;
};

struct Material {
    enum class Shading { Unlit, MetallicRoughness };
    enum class AlphaMode { Opaque, Mask, Blend };
    QString name;
    QVector4D baseColor{1, 1, 1, 1}; // glTF factors are linear, not display sRGB.
    std::shared_ptr<const TextureData> baseColorTexture;
    std::shared_ptr<const TextureData> metallicRoughnessTexture;
    std::shared_ptr<const TextureData> normalTexture;
    std::shared_ptr<const TextureData> occlusionTexture;
    std::shared_ptr<const TextureData> emissiveTexture;
    float normalScale = 1;
    float occlusionStrength = 1;
    QVector3D emissiveFactor{0,0,0}; // linear; does not depend on lighting/AO
    float emissiveStrength = 1;
    QVector4D sparkle{0,0,0,0}; // time, seed, enabled, reserved
    // glTF transmission/IOR/volume. Thickness is in model-space length units.
    float transmission = 0;
    float ior = 1.5f;
    float thickness = 0;
    QVector3D attenuationColor{1,1,1};
    float attenuationDistance = 0; // 0 means infinite (no absorption).
    std::shared_ptr<const TextureData> transmissionTexture; // linear R
    std::shared_ptr<const TextureData> thicknessTexture; // linear G
    float metallic = 0;
    float roughness = 0.9f;
    float alphaCutoff = 0.5f;
    Shading shading = Shading::Unlit;
    AlphaMode alphaMode = AlphaMode::Opaque;
    bool doubleSided = false;
};

} // namespace darkeye::render3d
