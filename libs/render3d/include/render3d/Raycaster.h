#pragma once

#include "Camera.h"
#include "Scene.h"
#include <optional>

namespace darkeye::render3d {
struct RayHit {
    NodeId node = 0;
    float distance = 0;
    QVector3D position;
};

enum class RaycastMode { Bounds, Triangles };
// Bounds is the inexpensive shelf broad phase. Triangles resolves overlapping
// boxes and holes against the actual surface, in world ray distance units.
[[nodiscard]] std::optional<RayHit> raycast(const Scene &scene, const Ray &ray,
                                         RaycastMode mode=RaycastMode::Bounds);
} // namespace darkeye::render3d
