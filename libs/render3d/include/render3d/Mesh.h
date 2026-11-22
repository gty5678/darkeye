#pragma once

#include <QString>
#include <array>
#include <cstdint>
#include <vector>

namespace darkeye::render3d {

// CPU data only. Positions use right-handed world space. vertexColorsSrgb
// distinguishes authored display colors from glTF's linear vertex factors.
struct Vertex {
    std::array<float, 3> position;
    std::array<float, 3> color;
    std::array<float, 3> normal{0, 0, 1};
    std::array<float, 2> uv{0, 0};
    std::array<float, 4> tangent{1,0,0,1}; // xyz tangent, w bitangent handedness
};

struct MeshData {
    std::vector<Vertex> vertices;
    std::vector<std::uint32_t> indices;
    bool vertexColorsSrgb = true;
    bool hasTangents = false;

    // An entirely empty mesh is a valid clear-only scene.
    [[nodiscard]] QString validationError() const;
};

[[nodiscard]] MeshData coloredTriangle();
[[nodiscard]] MeshData coloredCube();

} // namespace darkeye::render3d
