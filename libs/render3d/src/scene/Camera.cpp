#include "render3d/Camera.h"
#include <QQuaternion>
#include <QtMath>
#include <algorithm>
#include <cmath>
#include <limits>

namespace darkeye::render3d {
namespace {
bool finite(const QVector3D &value)
{
    return std::isfinite(value.x()) && std::isfinite(value.y()) && std::isfinite(value.z());
}

bool finite(const QMatrix4x4 &matrix)
{
    return std::all_of(matrix.constData(), matrix.constData() + 16,
                       [](float value) { return std::isfinite(value); });
}

QVector3D eyePosition(const Camera &camera, const QQuaternion &rotation)
{
    const QVector3D offset(0, 0, camera.distance);
    return camera.target + (camera.rotationMode == Camera::RotationMode::InPlace
        ? offset : rotation.rotatedVector(offset));
}
}

QVector3D Camera::position() const
{
    return eyePosition(*this, QQuaternion::fromEulerAngles(pitch, yaw, 0));
}

QString Camera::validationError() const
{
    if (!finite(target) || !std::isfinite(distance) || distance <= 0
        || !std::isfinite(yaw) || !std::isfinite(pitch)
        || (rotationMode != RotationMode::Orbit && rotationMode != RotationMode::InPlace))
        return QStringLiteral("Invalid camera pose");
    if (!std::isfinite(verticalFov) || verticalFov <= 0 || verticalFov >= 180)
        return QStringLiteral("Camera field of view must be between 0 and 180 degrees");
    if (!std::isfinite(nearPlane) || !std::isfinite(farPlane) || nearPlane <= 0 || farPlane <= nearPlane)
        return QStringLiteral("Camera clip planes must satisfy 0 < near < far");
    return {};
}

std::optional<Camera::Matrices> Camera::matrices(float aspect, QString *error) const
{
    const auto fail = [error](const QString &message) -> std::optional<Matrices> {
        if (error)
            *error = message;
        return std::nullopt;
    };
    if (!std::isfinite(aspect) || aspect <= 0)
        return fail(QStringLiteral("Camera aspect ratio must be finite and positive"));
    const auto problem = validationError();
    if (!problem.isEmpty())
        return fail(problem);
    Matrices result;
    const auto rotation = QQuaternion::fromEulerAngles(pitch, yaw, 0);
    result.eye = eyePosition(*this, rotation);
    result.view.rotate(rotation.conjugated());
    result.view.translate(-result.eye);
    result.projection.perspective(verticalFov, aspect, nearPlane, farPlane);
    result.viewProjection = result.projection * result.view;
    const auto &projection = result.projection;
    if (!finite(result.viewProjection) || projection(3, 2) != -1
        || projection(0, 0) == 0 || projection(1, 1) == 0 || projection(2, 3) == 0)
        return fail(QStringLiteral("Camera parameters cannot produce usable matrices"));
    if (error)
        error->clear();
    return result;
}

std::optional<Camera::Matrices> Camera::matrices(const QSize &viewport) const
{
    if (viewport.isEmpty())
        return std::nullopt;
    return matrices(float(viewport.width()) / viewport.height());
}

std::optional<Ray> Camera::ray(const QPointF &point, const QSize &viewport) const
{
    const auto cameraMatrices = matrices(viewport);
    if (!cameraMatrices)
        return std::nullopt;
    const double x = 2 * (point.x() / viewport.width()) - 1;
    const double y = 1 - 2 * (point.y() / viewport.height());
    constexpr auto maxCoordinate = std::numeric_limits<float>::max();
    if (!(std::abs(x) <= maxCoordinate && std::abs(y) <= maxCoordinate))
        return std::nullopt;
    bool invertible = false;
    const auto inverse = cameraMatrices->viewProjection.inverted(&invertible);
    if (!invertible)
        return std::nullopt;
    const auto nearPoint = inverse * QVector4D(float(x), float(y), -1, 1);
    const auto farPoint = inverse * QVector4D(float(x), float(y), 1, 1);
    if (qFuzzyIsNull(nearPoint.w()) || qFuzzyIsNull(farPoint.w()))
        return std::nullopt;
    const auto origin = nearPoint.toVector3DAffine();
    const auto direction = (farPoint.toVector3DAffine() - origin).normalized();
    if (!finite(origin) || !finite(direction) || direction.isNull())
        return std::nullopt;
    return Ray{origin, direction};
}

std::optional<QPointF> Camera::project(const QVector3D &world, const QSize &viewport) const
{
    const auto cameraMatrices = matrices(viewport);
    return cameraMatrices ? cameraMatrices->project(world, viewport) : std::nullopt;
}

std::optional<QPointF> Camera::Matrices::project(const QVector3D &world, const QSize &viewport) const
{
    if (viewport.isEmpty())
        return std::nullopt;
    const auto clip = viewProjection * QVector4D(world, 1);
    if (!std::isfinite(clip.w()) || clip.w() <= 0)
        return std::nullopt;
    const auto ndc = clip.toVector3DAffine();
    if (!finite(ndc))
        return std::nullopt;
    return QPointF((double(ndc.x()) + 1) * viewport.width() / 2,
                   (1 - double(ndc.y())) * viewport.height() / 2);
}

bool Camera::orbit(float deltaYaw, float deltaPitch)
{
    const double nextYaw = double(yaw) + deltaYaw;
    const double nextPitch = double(pitch) + deltaPitch;
    if (!std::isfinite(nextYaw) || !std::isfinite(nextPitch))
        return false;
    yaw = float(std::remainder(nextYaw, 360.0));
    pitch = float(std::clamp(nextPitch, -85.0, 85.0));
    return true;
}

bool Camera::zoom(float steps)
{
    const double minimum = nearPlane * 4.0;
    const double maximum = farPlane * 0.5;
    if (!std::isfinite(steps) || !std::isfinite(maximum) || !(minimum > 0 && minimum <= maximum)
        || !std::isfinite(distance) || distance <= 0)
        return false;
    distance = float(std::clamp(distance * std::exp(-steps * 0.12), minimum, maximum));
    return true;
}

bool Camera::pan(float horizontal, float vertical)
{
    const auto rotation = QQuaternion::fromEulerAngles(pitch, yaw, 0);
    const auto next = target + rotation.rotatedVector({horizontal, vertical, 0});
    if (!finite(next))
        return false;
    target = next;
    return true;
}
} // namespace darkeye::render3d
