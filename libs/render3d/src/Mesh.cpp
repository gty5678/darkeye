#include "render3d/Mesh.h"

#include <cmath>
#include <limits>
#include <QVector3D>

namespace darkeye::render3d {

QString MeshData::validationError() const
{
    if (vertices.empty() && indices.empty())
        return {};
    if (vertices.empty() || indices.empty() || indices.size() % 3 != 0)
        return QStringLiteral("Mesh requires vertices and triangle indices (a multiple of three)");
    constexpr auto maxBufferSize = std::numeric_limits<int>::max();
    if (vertices.size() > maxBufferSize / sizeof(Vertex)
        || indices.size() > maxBufferSize / sizeof(std::uint32_t))
        return QStringLiteral("Mesh exceeds QRhi buffer size limits");
    for (const auto &vertex : vertices) {
        for (float value : vertex.position)
            if (!std::isfinite(value))
                return QStringLiteral("Mesh contains a non-finite position");
        for (float value : vertex.color)
            if (!std::isfinite(value) || value < 0.0f || value > 1.0f)
                return QStringLiteral("Mesh colors must be finite values in [0, 1]");
        for (float value : vertex.normal)
            if (!std::isfinite(value))
                return QStringLiteral("Mesh contains a non-finite normal");
        for (float value : vertex.uv)
            if (!std::isfinite(value))
                return QStringLiteral("Mesh contains non-finite UV coordinates");
        if(hasTangents) {
            for(float value:vertex.tangent)
                if(!std::isfinite(value)) return QStringLiteral("Mesh contains a non-finite tangent");
            if(std::abs(vertex.tangent[3])!=1.0f)
                return QStringLiteral("Tangent handedness must be +1 or -1");
            if(QVector3D(vertex.tangent[0],vertex.tangent[1],vertex.tangent[2]).lengthSquared()<1e-12f)
                return QStringLiteral("Mesh contains a zero-length tangent");
        }
    }
    for (auto index : indices)
        if (index >= vertices.size())
            return QStringLiteral("Mesh index %1 is outside %2 vertices")
                .arg(index).arg(vertices.size());
    return {};
}

MeshData coloredTriangle()
{
    return {{{{0.0f, 1.0f, 0.0f}, {1.0f, 0.2f, 0.3f}},
             {{-1.0f, -0.8f, 0.0f}, {0.2f, 1.0f, 0.5f}},
             {{1.0f, -0.8f, 0.0f}, {0.2f, 0.4f, 1.0f}}}, {0, 1, 2}};
}

MeshData coloredCube()
{
    MeshData mesh;
    // Separate face vertices give every side an unmistakable solid color.
    const auto face = [&mesh](std::array<float, 3> color,
                             std::array<std::array<float, 3>, 4> corners) {
        const auto first = static_cast<std::uint32_t>(mesh.vertices.size());
        const auto vector=[](const std::array<float,3> &p) { return QVector3D(p[0],p[1],p[2]); };
        const auto normal=QVector3D::crossProduct(vector(corners[1])-vector(corners[0]),
            vector(corners[2])-vector(corners[0])).normalized();
        const std::array<std::array<float,2>,4> uv{{{0,1},{1,1},{1,0},{0,0}}};
        for(std::size_t i=0;i<corners.size();++i)
            mesh.vertices.push_back({corners[i],color,{normal.x(),normal.y(),normal.z()},uv[i]});
        for (auto offset : {0u, 1u, 2u, 0u, 2u, 3u})
            mesh.indices.push_back(first + offset);
    };
    // Front is deliberately submitted before back: depth testing must keep
    // the later, hidden back face from overwriting it.
    face({0.20f, 0.72f, 0.95f}, {{{-1,-1,1}, {1,-1,1}, {1,1,1}, {-1,1,1}}});
    face({0.95f, 0.30f, 0.40f}, {{{1,-1,-1}, {-1,-1,-1}, {-1,1,-1}, {1,1,-1}}});
    face({0.45f, 0.80f, 0.45f}, {{{1,-1,1}, {1,-1,-1}, {1,1,-1}, {1,1,1}}});
    face({0.65f, 0.45f, 0.95f}, {{{-1,-1,-1}, {-1,-1,1}, {-1,1,1}, {-1,1,-1}}});
    face({0.98f, 0.75f, 0.28f}, {{{-1,1,1}, {1,1,1}, {1,1,-1}, {-1,1,-1}}});
    face({0.20f, 0.40f, 0.65f}, {{{-1,-1,-1}, {1,-1,-1}, {1,-1,1}, {-1,-1,1}}});
    return mesh;
}

} // namespace darkeye::render3d
