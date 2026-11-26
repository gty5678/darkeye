#pragma once

#include <QMatrix4x4>
#include <QPointF>
#include <QSize>
#include <QString>
#include <QVector3D>
#include <optional>

namespace darkeye::render3d {

struct Ray {
    QVector3D origin;
    QVector3D direction;
};

struct Camera {
    enum class RotationMode { Orbit, InPlace };
    struct Matrices {
        QMatrix4x4 view;
        QMatrix4x4 projection;
        QMatrix4x4 viewProjection;
        QVector3D eye;
        [[nodiscard]] std::optional<QPointF> project(const QVector3D &world, const QSize &viewport) const;
    };
    QVector3D target{0, 0, 0};
    float distance = 6;
    float yaw = 0;   // degrees; yaw=0 looks down -Z from positive Z.
    float pitch = 0;
    float verticalFov = 45;
    float nearPlane = 0.01f;
    float farPlane = 1000;
    RotationMode rotationMode = RotationMode::Orbit;

    [[nodiscard]] QVector3D position() const;
    // CPU projection is OpenGL NDC (-1..1 Z). Only the GPU renderer applies
    // QRhi's clip-space correction. Picking stays backend-independent.
    [[nodiscard]] std::optional<Matrices> matrices(float aspect, QString *error = nullptr) const;
    [[nodiscard]] std::optional<Matrices> matrices(const QSize &viewport) const;
    [[nodiscard]] std::optional<Ray> ray(const QPointF &logicalPosition,
                                       const QSize &logicalViewport) const;
    [[nodiscard]] std::optional<QPointF> project(const QVector3D &world,
                                               const QSize &logicalViewport) const;
    bool orbit(float deltaYaw, float deltaPitch);
    bool zoom(float steps);
    bool pan(float horizontal, float vertical);

private:
    [[nodiscard]] QString validationError() const;
};

} // namespace darkeye::render3d
