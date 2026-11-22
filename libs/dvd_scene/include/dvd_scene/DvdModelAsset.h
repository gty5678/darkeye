#pragma once

#include "render3d/GltfLoader.h"
#include <map>

// Adapt Blender exports to the shelf node/material contract without changing the file.
namespace darkeye::dvd_scene {
inline darkeye::render3d::ModelLoadResult loadDvdModel(
    darkeye::render3d::GltfLoader &loader, const QString &path)
{
    using namespace darkeye::render3d;
    const auto loaded = loader.load(path);
    if (!loaded) return loaded;
    auto model = std::make_shared<ModelAsset>();
    model->sourcePath = loaded.asset->sourcePath;
    model->warnings = loaded.asset->warnings;
    std::vector<int> remap(loaded.asset->nodes.size(), -1);
    std::map<const Material *, MaterialHandle> materials;
    for (std::size_t i = 0; i < loaded.asset->nodes.size(); ++i) {
        const auto &source = loaded.asset->nodes[i];
        const bool child = source.parent >= 0 && remap[std::size_t(source.parent)] >= 0;
        if (!child && source.name != "back" && source.name != "disc") continue;
        auto node = source;
        node.parent = child ? remap[std::size_t(source.parent)] : -1;
        // Reject wrappers rather than silently discarding their transforms.
        if (!child && source.parent >= 0) {
            return {{}, QStringLiteral("DVD model expects back and disc at the scene root; export their parent transforms first.")};
        }
        for (auto &primitive : node.primitives) {
            // The exported disc has a separate printable face, clear rim and
            // reflective underside. Only the printable face takes the artwork.
            if(source.name=="disc" && primitive.material && primitive.material->name=="Material") {
                auto label=std::make_shared<Material>(*primitive.material);
                label->name=QStringLiteral("disc-cover");
                primitive.material=std::move(label);
            }
            if (!primitive.material || primitive.material->name != "pic") continue;
            auto &cover = materials[primitive.material.get()];
            if (!cover) {
                auto copy = std::make_shared<Material>(*primitive.material);
                copy->name = QStringLiteral("cover");
                cover = std::move(copy);
            }
            primitive.material = cover;
        }
        remap[i] = int(model->nodes.size());
        model->nodes.push_back(std::move(node));
    }
    for (const auto &name : {"back", "spine", "front", "disc"}) {
        bool found = false;
        for (const auto &node : model->nodes) found |= node.name == name;
        if (!found) return {{}, QStringLiteral("DVD GLB is missing node: %1").arg(QString::fromLatin1(name))};
    }
    return {std::move(model), {}};
}

} // namespace darkeye::dvd_scene
