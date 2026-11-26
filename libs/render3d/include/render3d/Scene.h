#pragma once

#include "Material.h"
#include "Mesh.h"
#include "Transform.h"

#include <QString>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <vector>

namespace darkeye::render3d {

using NodeId = std::uint64_t;
// Published mesh data must remain immutable while any handle refers to it.
using MeshHandle = std::shared_ptr<const MeshData>;
using MaterialHandle = std::shared_ptr<const Material>;

struct Node {
    NodeId id = 0;
    NodeId parent = 0;
    QString name;
    Transform transform;
    MeshHandle mesh;
    MaterialHandle material;
    bool visible = true;
};

struct RenderItem {
    NodeId node = 0;
    QMatrix4x4 world;
    MeshHandle mesh;
    MaterialHandle material;
};

// A GUI-thread CPU scene, with immutable shared assets and lazily cached world
// transforms. Node IDs are never reused, including after clear().
class Scene {
public:
    [[nodiscard]] NodeId createNode(NodeId parent = 0, const QString &name = {});
    bool removeNode(NodeId id);
    bool setParent(NodeId id, NodeId parent);
    bool setTransform(NodeId id, const Transform &transform);
    bool setMesh(NodeId id, MeshHandle mesh, MaterialHandle material = {}, QString *error = nullptr);
    bool setVisible(NodeId id, bool visible);
    void clear();
    [[nodiscard]] const Node *node(NodeId id) const;
    // A missing node has no world transform. A root node still has its local transform.
    [[nodiscard]] std::optional<QMatrix4x4> worldMatrix(NodeId id) const;
    [[nodiscard]] std::vector<RenderItem> renderItems() const;
    [[nodiscard]] std::size_t size() const { return m_nodes.size(); }
    [[nodiscard]] std::uint64_t revision() const { return m_revision; }

private:
    friend struct ModelAsset;

    struct Entry {
        Node node;
        std::vector<NodeId> children;
        mutable QMatrix4x4 world;
        mutable bool dirty = true;
    };
    void markDirty(NodeId id);
    void removeSubtree(NodeId id);
    [[nodiscard]] QString validateMesh(const MeshHandle &mesh);
    [[nodiscard]] const QMatrix4x4 &worldMatrix(const Entry &entry) const;
    [[nodiscard]] bool effectivelyVisible(NodeId id) const;
    std::map<NodeId, Entry> m_nodes;
    std::map<const MeshData *, std::weak_ptr<const MeshData>> m_validatedMeshes;
    std::size_t m_meshCacheCleanupSize = 64;
    NodeId m_nextId = 1;
    std::uint64_t m_revision = 0;
};

} // namespace darkeye::render3d
