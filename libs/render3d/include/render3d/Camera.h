#pragma once

#include <QMatrix4x4>
#include <QPointF>
#include <QSize>
#include <QVector3D>
#include <optional>

namespace darkeye::render3d {

struct Ray {
    QVector3D origin;
    QVector3D direction;
};

struct Camera {
    enum class RotationMode { Orbit, InPlace };
    QVector3D target{0, 0, 0};
    float distance = 6;
    float yaw = 0;   // degrees; yaw=0 looks down -Z from positive Z.
    float pitch = 0;
    float verticalFov = 45;
    float nearPlane = 0.01f;
    float farPlane = 1000;
    RotationMode rotationMode = RotationMode::Orbit;

    [[nodiscard]] QVector3D position() const;
    [[nodiscard]] QMatrix4x4 viewMatrix() const;
    // CPU projection is OpenGL NDC (-1..1 Z). Only the GPU renderer applies
    // QRhi's clip-space correction. Picking stays backend-independent.
    [[nodiscard]] QMatrix4x4 projectionMatrix(float aspect) const;
    [[nodiscard]] std::optional<Ray> ray(const QPointF &logicalPosition,
                                       const QSize &logicalViewport) const;
    [[nodiscard]] std::optional<QPointF> project(const QVector3D &world,
                                               const QSize &logicalViewport) const;
    void orbit(float deltaYaw, float deltaPitch);
    void zoom(float steps);
    void pan(float horizontal, float vertical);
};

} // namespace darkeye::render3d
