#include "render3d/Scene.h"
#include <algorithm>
#include <limits>

namespace darkeye::render3d {

NodeId Scene::createNode(NodeId parent, const QString &name)
{
    if ((parent && !m_nodes.contains(parent)) || m_nextId == std::numeric_limits<NodeId>::max())
        return 0;
    const auto id = m_nextId++;
    Entry entry;
    entry.node.id = id;
    entry.node.parent = parent;
    entry.node.name = name;
    m_nodes.emplace(id, std::move(entry));
    if (parent)
        m_nodes.at(parent).children.push_back(id);
    ++m_revision;
    return id;
}

void Scene::markDirty(NodeId id)
{
    auto &entry = m_nodes.at(id);
    entry.dirty = true;
    for (auto child : entry.children)
        markDirty(child);
}

bool Scene::setParent(NodeId id, NodeId parent)
{
    if (!m_nodes.contains(id) || (parent && !m_nodes.contains(parent)))
        return false;
    for (auto ancestor = parent; ancestor; ancestor = m_nodes.at(ancestor).node.parent)
        if (ancestor == id)
            return false;
    auto &entry = m_nodes.at(id);
    if (entry.node.parent == parent)
        return true;
    if (entry.node.parent)
        std::erase(m_nodes.at(entry.node.parent).children, id);
    entry.node.parent = parent;
    if (parent)
        m_nodes.at(parent).children.push_back(id);
    markDirty(id);
    ++m_revision;
    return true;
}

void Scene::removeSubtree(NodeId id)
{
    const auto children = m_nodes.at(id).children;
    for (auto child : children)
        removeSubtree(child);
    m_nodes.erase(id);
}

bool Scene::removeNode(NodeId id)
{
    const auto it = m_nodes.find(id);
    if (it == m_nodes.end())
        return false;
    const auto parent = it->second.node.parent;
    if (parent)
        std::erase(m_nodes.at(parent).children, id);
    removeSubtree(id);
    ++m_revision;
    return true;
}

bool Scene::setTransform(NodeId id, const Transform &transform)
{
    const auto it = m_nodes.find(id);
    if (it == m_nodes.end() || !transform.isFinite())
        return false;
    auto &current = it->second.node.transform;
    if (current.translation == transform.translation && current.rotation == transform.rotation
        && current.scale == transform.scale)
        return true;
    current = transform;
    current.rotation.normalize();
    markDirty(id);
    ++m_revision;
    return true;
}

QString Scene::validateMesh(const MeshHandle &mesh)
{
    if (!mesh)
        return {};
    const auto cached = m_validatedMeshes.find(mesh.get());
    if (cached != m_validatedMeshes.end() && !cached->second.expired()
        && !cached->second.owner_before(mesh) && !mesh.owner_before(cached->second))
        return {};
    const auto error = mesh->validationError();
    if (!error.isEmpty())
        return error;

    if (m_validatedMeshes.size() >= m_meshCacheCleanupSize) {
        std::erase_if(m_validatedMeshes, [](const auto &entry) { return entry.second.expired(); });
        m_meshCacheCleanupSize = std::max<std::size_t>(64, m_validatedMeshes.size() * 2);
    }
    m_validatedMeshes.insert_or_assign(mesh.get(), mesh);
    return {};
}

bool Scene::setMesh(NodeId id, MeshHandle mesh, MaterialHandle material, QString *error)
{
    const auto it = m_nodes.find(id);
    const auto failure = it == m_nodes.end()
        ? QStringLiteral("Unknown scene node %1").arg(id) : validateMesh(mesh);
    if (error)
        *error = failure;
    if (!failure.isEmpty())
        return false;
    it->second.node.mesh = std::move(mesh);
    it->second.node.material = std::move(material);
    ++m_revision;
    return true;
}

bool Scene::setVisible(NodeId id, bool visible)
{
    const auto it = m_nodes.find(id);
    if (it == m_nodes.end())
        return false;
    if (it->second.node.visible != visible) {
        it->second.node.visible = visible;
        ++m_revision;
    }
    return true;
}

void Scene::clear()
{
    m_nodes.clear();
    m_validatedMeshes.clear();
    m_meshCacheCleanupSize = 64;
    ++m_revision;
}

const Node *Scene::node(NodeId id) const
{
    const auto it = m_nodes.find(id);
    return it == m_nodes.end() ? nullptr : &it->second.node;
}

std::optional<QMatrix4x4> Scene::worldMatrix(NodeId id) const
{
    const auto it = m_nodes.find(id);
    if (it == m_nodes.end())
        return std::nullopt;
    return worldMatrix(it->second);
}

const QMatrix4x4 &Scene::worldMatrix(const Entry &entry) const
{
    if (entry.dirty) {
        entry.world = entry.node.transform.matrix();
        if (entry.node.parent)
            entry.world = worldMatrix(m_nodes.at(entry.node.parent)) * entry.world;
        entry.dirty = false;
    }
    return entry.world;
}

bool Scene::effectivelyVisible(NodeId id) const
{
    for (auto current = id; current;) {
        const auto &node = m_nodes.at(current).node;
        if (!node.visible)
            return false;
        current = node.parent;
    }
    return true;
}

std::vector<RenderItem> Scene::renderItems() const
{
    std::vector<RenderItem> result;
    result.reserve(m_nodes.size());
    for (const auto &[id, entry] : m_nodes)
        if (entry.node.mesh && !entry.node.mesh->indices.empty() && effectivelyVisible(id))
            result.push_back({id, worldMatrix(entry), entry.node.mesh, entry.node.material});
    return result;
}

} // namespace darkeye::render3d
