#pragma once

#include "darkeye_ui/theme/ThemeService.h"
#include "render3d/Lighting.h"

#include <QDebug>

namespace darkeye {

inline QVector3D sceneKeyLightDirection(ThemeId theme)
{
    return {theme == ThemeId::Dark ? 0.5f : -0.5f, 0.8f, 1.0f};
}

inline std::shared_ptr<const render3d::EnvironmentMap> sceneEnvironment(ThemeId theme)
{
    const auto load = [](const QString &path) {
        QString error;
        auto environment = render3d::EnvironmentMap::loadHdr(path, &error);
        if (!environment)
            qWarning().noquote() << path << error;
        return environment;
    };

    if (theme == ThemeId::Dark) {
        static const auto fireplace = load(QStringLiteral(":/assets/hdr/fireplace_1k.hdr"));
        if (fireplace)
            return fireplace;
    }
    static const auto lebombo = load(QStringLiteral(":/assets/hdr/lebombo_1k.hdr"));
    return lebombo;
}

} // namespace darkeye
