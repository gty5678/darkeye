#include "render3d/Camera.h"
#include <QQuaternion>
#include <QtMath>
#include <algorithm>
#include <cmath>

namespace darkeye::render3d {
QVector3D Camera::position() const
{
    if(rotationMode==RotationMode::InPlace) return target+QVector3D(0,0,distance);
    return target + QQuaternion::fromEulerAngles(pitch, yaw, 0).rotatedVector({0, 0, distance});
}

QMatrix4x4 Camera::viewMatrix() const
{
    QMatrix4x4 result;
    const auto up = QQuaternion::fromEulerAngles(pitch, yaw, 0).rotatedVector({0, 1, 0});
    const auto lookAt=rotationMode==RotationMode::InPlace
        ?position()+QQuaternion::fromEulerAngles(pitch,yaw,0).rotatedVector({0,0,-1})
        :target;
    result.lookAt(position(), lookAt, up);
    return result;
}

QMatrix4x4 Camera::projectionMatrix(float aspect) const
{
    QMatrix4x4 result;
    if (aspect > 0 && std::isfinite(aspect) && verticalFov > 0 && verticalFov < 180
        && nearPlane > 0 && farPlane > nearPlane)
        result.perspective(verticalFov, aspect, nearPlane, farPlane);
    return result;
}

std::optional<Ray> Camera::ray(const QPointF &point, const QSize &viewport) const
{
    if (viewport.isEmpty() || !std::isfinite(point.x()) || !std::isfinite(point.y()))
        return std::nullopt;
    const float x = float(2 * point.x() / viewport.width() - 1);
    const float y = float(1 - 2 * point.y() / viewport.height());
    bool invertible = false;
    const auto inverse = (projectionMatrix(float(viewport.width()) / viewport.height())
                          * viewMatrix()).inverted(&invertible);
    if (!invertible)
        return std::nullopt;
    auto nearPoint = inverse * QVector4D(x, y, -1, 1);
    auto farPoint = inverse * QVector4D(x, y, 1, 1);
    if (qFuzzyIsNull(nearPoint.w()) || qFuzzyIsNull(farPoint.w()))
        return std::nullopt;
    const auto origin = nearPoint.toVector3DAffine();
    const auto direction = (farPoint.toVector3DAffine() - origin).normalized();
    if (direction.isNull())
        return std::nullopt;
    return Ray{origin, direction};
}

std::optional<QPointF> Camera::project(const QVector3D &world, const QSize &viewport) const
{
    if (viewport.isEmpty())
        return std::nullopt;
    const auto clip = projectionMatrix(float(viewport.width()) / viewport.height())
        * viewMatrix() * QVector4D(world, 1);
    if (clip.w() <= 0)
        return std::nullopt;
    const auto ndc = clip.toVector3DAffine();
    return QPointF((ndc.x() + 1) * viewport.width() / 2,
                   (1 - ndc.y()) * viewport.height() / 2);
}

void Camera::orbit(float deltaYaw, float deltaPitch)
{
    if (!std::isfinite(deltaYaw) || !std::isfinite(deltaPitch))
        return;
    yaw = std::remainder(yaw + deltaYaw, 360.0f);
    pitch = std::clamp(pitch + deltaPitch, -85.0f, 85.0f);
}

void Camera::zoom(float steps)
{
    if (std::isfinite(steps))
        distance = std::clamp(distance * std::exp(-steps * 0.12f), nearPlane * 4, farPlane * 0.5f);
}

void Camera::pan(float horizontal, float vertical)
{
    if (!std::isfinite(horizontal) || !std::isfinite(vertical))
        return;
    const auto rotation = QQuaternion::fromEulerAngles(pitch, yaw, 0);
    target += rotation.rotatedVector({horizontal, vertical, 0});
}
} // namespace darkeye::render3d
