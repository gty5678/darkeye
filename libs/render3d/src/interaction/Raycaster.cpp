#include "render3d/Raycaster.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <map>

namespace darkeye::render3d {
std::optional<RayHit> raycast(const Scene &scene, const Ray &ray, RaycastMode mode)
{
    if (ray.direction.isNull() || !std::isfinite(ray.direction.lengthSquared())
        || !std::isfinite(ray.origin.lengthSquared()))
        return std::nullopt;
    std::optional<RayHit> result;
    struct CachedBounds {
        std::weak_ptr<const MeshData> source;
        std::pair<QVector3D,QVector3D> extent;
    };
    // Scene mesh handles are immutable assets. Weak ownership avoids retaining
    // removed models; per-thread storage also keeps independent callers safe.
    thread_local std::map<const MeshData *,CachedBounds> bounds;
    std::erase_if(bounds, [](const auto &entry) { return entry.second.source.expired(); });
    for (const auto &item : scene.renderItems()) {
        bool invertible = false;
        const auto inverse = item.world.inverted(&invertible);
        if (!invertible)
            continue;
        const auto origin = inverse.map(ray.origin);
        // Keep direction unnormalized: the slab parameter remains in world
        // ray units despite nonuniform scale.
        const auto direction = inverse.mapVector(ray.direction.normalized());
        if(!bounds.contains(item.mesh.get())) {
            QVector3D lower(std::numeric_limits<float>::max(), std::numeric_limits<float>::max(),
                            std::numeric_limits<float>::max());
            QVector3D upper=-lower;
            for(const auto &vertex:item.mesh->vertices)
                for(int axis=0;axis<3;++axis) {
                    lower[axis]=std::min(lower[axis],vertex.position[axis]);
                    upper[axis]=std::max(upper[axis],vertex.position[axis]);
                }
            bounds.emplace(item.mesh.get(),CachedBounds{item.mesh,{lower,upper}});
        }
        const auto &[lower,upper]=bounds.at(item.mesh.get()).extent;
        float enter = 0;
        float exit = std::numeric_limits<float>::max();
        bool miss = false;
        for (int axis = 0; axis < 3; ++axis) {
            if (std::abs(direction[axis]) < 1e-8f) {
                if (origin[axis] < lower[axis] || origin[axis] > upper[axis]) {
                    miss = true;
                    break;
                }
                continue;
            }
            float first = (lower[axis] - origin[axis]) / direction[axis];
            float last = (upper[axis] - origin[axis]) / direction[axis];
            if (first > last)
                std::swap(first, last);
            enter = std::max(enter, first);
            exit = std::min(exit, last);
            if (enter > exit) {
                miss = true;
                break;
            }
        }
        if(miss || (result && enter>result->distance)) continue;
        if(mode==RaycastMode::Triangles) {
            const auto &mesh=*item.mesh;
            const auto count=mesh.indices.empty()?mesh.vertices.size():mesh.indices.size();
            const auto position=[&](std::size_t index) {
                const auto &p=mesh.vertices[mesh.indices.empty()?index:mesh.indices[index]].position;
                return QVector3D(p[0],p[1],p[2]);
            };
            for(std::size_t i=0;i+2<count;i+=3) {
                const auto a=position(i);
                const auto edge1=position(i+1)-a,edge2=position(i+2)-a;
                const auto p=QVector3D::crossProduct(direction,edge2);
                const float determinant=QVector3D::dotProduct(edge1,p);
                if(std::abs(determinant)<=1e-7f*edge1.length()*p.length()) continue;
                const float inverseDeterminant=1/determinant;
                const auto offset=origin-a;
                const float u=QVector3D::dotProduct(offset,p)*inverseDeterminant;
                if(u<0 || u>1) continue;
                const auto q=QVector3D::crossProduct(offset,edge1);
                const float v=QVector3D::dotProduct(direction,q)*inverseDeterminant;
                if(v<0 || u+v>1) continue;
                const float distance=QVector3D::dotProduct(edge2,q)*inverseDeterminant;
                if(distance>=0 && (!result || distance<result->distance))
                    result=RayHit{item.node,distance,ray.origin+ray.direction.normalized()*distance};
            }
        } else if(!result || enter<result->distance) {
            result=RayHit{item.node,enter,ray.origin+ray.direction.normalized()*enter};
        }
    }
    return result;
}
} // namespace darkeye::render3d
