#pragma once
#include <QImage>
#include <QString>
#include <QVector3D>
#include <memory>
#include <vector>

namespace darkeye::render3d {
// Linear scene-referred radiance. CPU data survives GPU/window recreation.
struct EnvironmentMap {
    QImage radiance;
    QImage irradiance; // cosine convolution / pi
    std::vector<QImage> specular; // GGX prefilter, roughness = mip/(count-1)
    QImage brdf; // split-sum scale/bias
    QString source;
    static std::shared_ptr<const EnvironmentMap> loadHdr(const QString &path,QString *error=nullptr);
};
struct LightingSettings {
    bool environmentBackground=false;
    bool imageBasedLighting=false;
    bool ambientOcclusion=false;
    bool shadows=false;
    bool toneMapping=false;
    float exposure=1;
    float environmentIntensity=1;
    float environmentRotation=0; // degrees around +Y
    QVector3D keyLightDirection{0.5f,1,0.6f}; // surface toward light source
    float keyLightIntensity=2.4f;
    bool keyLightCameraRelative=false;
    float aoRadius=0.025f; // world units (DVD is 0.19m tall)
    float aoStrength=1;
    int transmissionPassLimit=8; // bounded screen-space layers, clamped to [1,16]
    bool displayClearColor=false; // composite an sRGB clear color after tone mapping
};
} // namespace darkeye::render3d
