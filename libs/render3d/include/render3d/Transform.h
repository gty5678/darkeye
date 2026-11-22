#pragma once

#include <QMatrix4x4>
#include <QQuaternion>
#include <QVector3D>

namespace darkeye::render3d {

struct Transform {
    QVector3D translation{0, 0, 0};
    QQuaternion rotation;
    QVector3D scale{1, 1, 1};

    [[nodiscard]] QMatrix4x4 matrix() const;
    [[nodiscard]] bool isFinite() const;
    [[nodiscard]] static Transform interpolate(const Transform &from, const Transform &to, float t);
};

} // namespace darkeye::render3d
