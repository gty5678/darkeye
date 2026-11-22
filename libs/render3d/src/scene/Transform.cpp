#include "render3d/Transform.h"
#include <algorithm>
#include <cmath>

namespace darkeye::render3d {
QMatrix4x4 Transform::matrix() const
{
    QMatrix4x4 result;
    result.translate(translation);
    result.rotate(rotation);
    result.scale(scale);
    return result;
}

bool Transform::isFinite() const
{
    const auto finite = [](const QVector3D &v) {
        return std::isfinite(v.x()) && std::isfinite(v.y()) && std::isfinite(v.z());
    };
    return finite(translation) && finite(scale) && !rotation.isNull()
        && std::isfinite(rotation.lengthSquared());
}

Transform Transform::interpolate(const Transform &from, const Transform &to, float t)
{
    t = std::clamp(t, 0.0f, 1.0f);
    return {from.translation * (1 - t) + to.translation * t,
            QQuaternion::slerp(from.rotation, to.rotation, t),
            from.scale * (1 - t) + to.scale * t};
}
} // namespace darkeye::render3d
