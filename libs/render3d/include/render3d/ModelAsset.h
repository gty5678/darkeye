#pragma once

#include "Scene.h"
#include <QStringList>

namespace darkeye::render3d {

struct ModelNode {
    QString name;
    int parent = -1;
    Transform transform;
    // One glTF node can hold several primitives with independent materials.
    struct Primitive { MeshHandle mesh; MaterialHandle material; };
    std::vector<Primitive> primitives;
};

struct ModelAsset {
    QString sourcePath;
    std::vector<ModelNode> nodes;
    QStringList warnings;

    // Instances share the exact same CPU assets, not deep copies.
    [[nodiscard]] std::vector<NodeId> instantiate(Scene &scene, NodeId parent = 0) const;
};

} // namespace darkeye::render3d
