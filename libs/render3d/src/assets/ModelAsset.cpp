#include "render3d/ModelAsset.h"
#include <functional>

namespace darkeye::render3d {
std::vector<NodeId> ModelAsset::instantiate(Scene &scene, NodeId parent) const
{
    if (parent && !scene.node(parent))
        return {};
    // Validate the complete hierarchy before touching the destination scene.
    std::vector<int> state(nodes.size(), 0);
    std::function<bool(std::size_t)> validate = [&](std::size_t i) {
        if (state[i] == 1)
            return false;
        if (state[i] == 2)
            return true;
        state[i] = 1;
        const auto &node = nodes[i];
        if (!node.transform.isFinite() || node.parent < -1
            || node.parent >= static_cast<int>(nodes.size()))
            return false;
        if (node.parent >= 0 && !validate(static_cast<std::size_t>(node.parent)))
            return false;
        for (const auto &primitive : node.primitives)
            if (!primitive.mesh || !primitive.mesh->validationError().isEmpty())
                return false;
        state[i] = 2;
        return true;
    };
    for (std::size_t i = 0; i < nodes.size(); ++i)
        if (!validate(i))
            return {};
    std::vector<NodeId> instances(nodes.size(), 0);
    std::function<NodeId(std::size_t)> create = [&](std::size_t i) {
        if (instances[i])
            return instances[i];
        const auto &node = nodes[i];
        const auto nodeParent = node.parent >= 0 ? create(static_cast<std::size_t>(node.parent)) : parent;
        const auto id = scene.createNode(nodeParent, node.name);
        scene.setTransform(id, node.transform);
        instances[i] = id;
        for (const auto &primitive : node.primitives) {
            const auto part = scene.createNode(id);
            scene.setMesh(part, primitive.mesh, primitive.material);
        }
        return id;
    };
    for (std::size_t i = 0; i < nodes.size(); ++i)
        create(i);
    return instances;
}
} // namespace darkeye::render3d
