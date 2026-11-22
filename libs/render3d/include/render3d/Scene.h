#pragma once

#include "Material.h"
#include "Mesh.h"
#include "Transform.h"

#include <QString>
#include <cstdint>
#include <map>
#include <memory>
#include <vector>

namespace darkeye::render3d {

using NodeId = std::uint64_t;
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
    bool setMesh(NodeId id, MeshHandle mesh, MaterialHandle material = {});
    bool setVisible(NodeId id, bool visible);
    void clear();
    [[nodiscard]] const Node *node(NodeId id) const;
    [[nodiscard]] QMatrix4x4 worldMatrix(NodeId id) const;
    [[nodiscard]] std::vector<RenderItem> renderItems() const;
    [[nodiscard]] std::size_t size() const { return m_nodes.size(); }
    [[nodiscard]] std::uint64_t revision() const { return m_revision; }

private:
    struct Entry {
        Node node;
        std::vector<NodeId> children;
        mutable QMatrix4x4 world;
        mutable bool dirty = true;
    };
    void markDirty(NodeId id);
    void removeSubtree(NodeId id);
    [[nodiscard]] bool effectivelyVisible(NodeId id) const;
    std::map<NodeId, Entry> m_nodes;
    NodeId m_nextId = 1;
    std::uint64_t m_revision = 0;
};

} // namespace darkeye::render3d
