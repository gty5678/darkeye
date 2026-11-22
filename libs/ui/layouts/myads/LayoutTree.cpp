#include "ui/layouts/myads/LayoutTree.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>
#include <algorithm>
#include <cmath>
#include <functional>
#include <numeric>
#include <stdexcept>

namespace darkeye::myads {
namespace {
std::shared_ptr<LayoutNode> makePane(const QString &id)
{
    auto node = std::make_shared<LayoutNode>();
    node->paneId = id;
    return node;
}

QList<int> equalSizes(int count, int total = LayoutTree::SizeBase)
{
    if (count < 2) return {};
    QList<int> result(count, total / count);
    result.last() += total - std::accumulate(result.cbegin(), result.cend(), 0);
    return result;
}

QList<int> scaledSizes(const QList<int> &source, int count, int total)
{
    const int sourceTotal = std::accumulate(source.cbegin(), source.cend(), 0);
    if (source.size() != count || sourceTotal <= 0) return equalSizes(count, total);
    QList<int> result;
    for (int size : source)
        result.append(static_cast<int>(static_cast<qint64>(total) * size / sourceTotal));
    result.last() += total - std::accumulate(result.cbegin(), result.cend(), 0);
    return result;
}
} // namespace

LayoutTree::LayoutTree() : m_root(std::make_shared<LayoutNode>())
{
    m_root->kind = LayoutNode::Kind::Split;
}

LayoutTree::LayoutTree(std::shared_ptr<LayoutNode> root) : m_root(std::move(root))
{
    QSet<QString> seen;
    validate(m_root, seen, 0);
}

const std::shared_ptr<LayoutNode> &LayoutTree::root() const noexcept { return m_root; }

QList<QString> LayoutTree::paneIds() const
{
    QList<QString> result;
    std::function<void(const std::shared_ptr<LayoutNode> &)> visit =
        [&](const std::shared_ptr<LayoutNode> &node) {
            if (node->kind == LayoutNode::Kind::Pane) result.append(node->paneId);
            else for (const auto &child : node->children) visit(child);
        };
    visit(m_root);
    return result;
}

void LayoutTree::addPaneToRoot(const QString &paneId)
{
    if (paneId.trimmed().isEmpty() || paneIds().contains(paneId))
        throw std::invalid_argument("invalid or duplicate pane id");
    m_root->children.append(makePane(paneId));
    m_root->sizes = equalSizes(m_root->children.size());
}

std::shared_ptr<LayoutNode> LayoutTree::findParent(
    const std::shared_ptr<LayoutNode> &node, const QString &paneId)
{
    if (!node || node->kind != LayoutNode::Kind::Split) return {};
    for (const auto &child : node->children) {
        if (child->kind == LayoutNode::Kind::Pane && child->paneId == paneId) return node;
        if (auto result = findParent(child, paneId)) return result;
    }
    return {};
}

void LayoutTree::split(const QString &paneId, Qt::Orientation orientation,
                       bool before, const QString &newPaneId, int percent)
{
    if (percent <= 0 || percent >= 100 || newPaneId.trimmed().isEmpty() ||
        paneIds().contains(newPaneId))
        throw std::invalid_argument("invalid split arguments");
    auto parent = findParent(m_root, paneId);
    if (!parent) throw std::invalid_argument("pane not found");
    int index = -1;
    for (int i = 0; i < parent->children.size(); ++i) {
        const auto &child = parent->children.at(i);
        if (child->kind == LayoutNode::Kind::Pane && child->paneId == paneId) {
            index = i;
            break;
        }
    }
    if (index < 0) throw std::invalid_argument("pane not found");

    const auto newPane = makePane(newPaneId);
    if (parent->orientation == orientation) {
        QList<int> sizes = parent->sizes;
        if (sizes.size() != parent->children.size()) {
            sizes = parent->children.size() == 1
                ? QList<int>{SizeBase}
                : equalSizes(parent->children.size());
        }
        const int occupied = sizes.isEmpty() ? SizeBase : sizes.at(index);
        const int newSize = occupied * percent / 100;
        const int oldSize = occupied - newSize;
        parent->children.insert(before ? index : index + 1, newPane);
        sizes[index] = before ? newSize : oldSize;
        sizes.insert(index + 1, before ? oldSize : newSize);
        parent->sizes = sizes;
    } else {
        auto branch = std::make_shared<LayoutNode>();
        branch->kind = LayoutNode::Kind::Split;
        branch->orientation = orientation;
        const auto oldPane = parent->children.at(index);
        branch->children = before
            ? QList<std::shared_ptr<LayoutNode>>{newPane, oldPane}
            : QList<std::shared_ptr<LayoutNode>>{oldPane, newPane};
        const int newSize = SizeBase * percent / 100;
        branch->sizes = before ? QList<int>{newSize, SizeBase - newSize}
                               : QList<int>{SizeBase - newSize, newSize};
        parent->children[index] = branch;
    }
    normalize();
}

void LayoutTree::splitRoot(Qt::Orientation orientation, bool before,
                           const QString &newPaneId, int percent)
{
    if (percent <= 0 || percent >= 100 || newPaneId.trimmed().isEmpty() ||
        paneIds().contains(newPaneId))
        throw std::invalid_argument("invalid root split arguments");
    auto root = std::make_shared<LayoutNode>();
    root->kind = LayoutNode::Kind::Split;
    root->orientation = orientation;
    const auto pane = makePane(newPaneId);
    root->children = before ? QList<std::shared_ptr<LayoutNode>>{pane, m_root}
                            : QList<std::shared_ptr<LayoutNode>>{m_root, pane};
    const int newSize = SizeBase * percent / 100;
    root->sizes = before ? QList<int>{newSize, SizeBase - newSize}
                         : QList<int>{SizeBase - newSize, newSize};
    m_root = root;
    normalize();
}

bool LayoutTree::removeFrom(const std::shared_ptr<LayoutNode> &parent,
                            const QString &paneId)
{
    if (!parent || parent->kind != LayoutNode::Kind::Split) return false;
    for (int i = 0; i < parent->children.size(); ++i) {
        const auto child = parent->children.at(i);
        if (child->kind == LayoutNode::Kind::Pane && child->paneId == paneId) {
            parent->children.removeAt(i);
            if (parent->sizes.size() > i) parent->sizes.removeAt(i);
            return true;
        }
        if (removeFrom(child, paneId)) return true;
    }
    return false;
}

bool LayoutTree::removePane(const QString &paneId)
{
    const bool removed = removeFrom(m_root, paneId);
    if (removed) normalize();
    return removed;
}

void LayoutTree::normalizeNode(const std::shared_ptr<LayoutNode> &node)
{
    if (!node || node->kind != LayoutNode::Kind::Split) return;
    for (const auto &child : node->children) normalizeNode(child);
    for (int i = 0; i < node->children.size();) {
        const auto child = node->children.at(i);
        if (child->kind == LayoutNode::Kind::Split &&
            child->orientation == node->orientation) {
            const int slot = node->sizes.size() == node->children.size()
                ? node->sizes.at(i)
                : SizeBase / std::max(1, static_cast<int>(node->children.size()));
            const QList<int> inserted = scaledSizes(child->sizes, child->children.size(), slot);
            node->children.removeAt(i);
            if (node->sizes.size() > i) node->sizes.removeAt(i);
            for (int j = 0; j < child->children.size(); ++j) {
                node->children.insert(i + j, child->children.at(j));
                if (!inserted.isEmpty()) node->sizes.insert(i + j, inserted.at(j));
            }
            continue;
        }
        ++i;
    }
    for (int i = 0; i < node->children.size(); ++i) {
        const auto child = node->children.at(i);
        if (child->kind == LayoutNode::Kind::Split && child->children.size() == 1)
            node->children[i] = child->children.first();
    }
    if (node->children.size() < 2) node->sizes.clear();
    else if (node->sizes.size() != node->children.size())
        node->sizes = equalSizes(node->children.size());
}

void LayoutTree::normalize()
{
    normalizeNode(m_root);
    while (m_root->children.size() == 1 &&
           m_root->children.first()->kind == LayoutNode::Kind::Split) {
        m_root = m_root->children.first();
        normalizeNode(m_root);
    }
}

std::shared_ptr<LayoutNode> LayoutTree::nodeAtPath(
    const std::shared_ptr<LayoutNode> &root, const QString &path)
{
    auto current = root;
    if (path.isEmpty()) return current;
    for (const QString &part : path.split(QLatin1Char('/'))) {
        bool ok = false;
        const int index = part.toInt(&ok);
        if (!ok || !current || current->kind != LayoutNode::Kind::Split ||
            index < 0 || index >= current->children.size()) return {};
        current = current->children.at(index);
    }
    return current;
}

void LayoutTree::setSizes(const QString &path, const QList<int> &sizes)
{
    const auto node = nodeAtPath(m_root, path);
    if (!node || node->kind != LayoutNode::Kind::Split ||
        sizes.size() != node->children.size() ||
        std::any_of(sizes.cbegin(), sizes.cend(), [](int value) { return value < 0; }) ||
        std::accumulate(sizes.cbegin(), sizes.cend(), 0) <= 0) return;
    node->sizes = sizes;
}

QJsonObject LayoutTree::nodeToJson(const std::shared_ptr<LayoutNode> &node)
{
    if (node->kind == LayoutNode::Kind::Pane)
        return {{QStringLiteral("type"), QStringLiteral("pane")},
                {QStringLiteral("pane_id"), node->paneId}};
    QJsonArray children;
    for (const auto &child : node->children) children.append(nodeToJson(child));
    QJsonObject result{{QStringLiteral("type"), QStringLiteral("split")},
                       {QStringLiteral("orientation"), node->orientation == Qt::Horizontal
                            ? QStringLiteral("horizontal") : QStringLiteral("vertical")},
                       {QStringLiteral("children"), children}};
    if (node->sizes.size() == node->children.size() && !node->sizes.isEmpty()) {
        const int total = std::accumulate(node->sizes.cbegin(), node->sizes.cend(), 0);
        QJsonArray ratios;
        for (int size : node->sizes)
            ratios.append(total > 0 ? static_cast<double>(size) / total : 0.0);
        result.insert(QStringLiteral("size_ratios"), ratios);
    }
    return result;
}

QJsonObject LayoutTree::toJson() const { return nodeToJson(m_root); }

std::shared_ptr<LayoutNode> LayoutTree::nodeFromJson(const QJsonObject &object,
                                                     QSet<QString> &seen, int depth)
{
    if (depth > 64) throw std::invalid_argument("layout nesting is too deep");
    const QString type = object.value(QStringLiteral("type")).toString();
    if (type == QStringLiteral("pane")) {
        const QString id = object.value(QStringLiteral("pane_id")).toString();
        if (id.trimmed().isEmpty() || seen.contains(id))
            throw std::invalid_argument("invalid or duplicate pane id");
        seen.insert(id);
        return makePane(id);
    }
    if (type != QStringLiteral("split")) throw std::invalid_argument("unknown node type");
    const QString orientation = object.value(QStringLiteral("orientation")).toString();
    if (orientation != QStringLiteral("horizontal") && orientation != QStringLiteral("vertical"))
        throw std::invalid_argument("invalid split orientation");
    const QJsonArray children = object.value(QStringLiteral("children")).toArray();
    if (children.isEmpty()) throw std::invalid_argument("split has no children");
    auto node = std::make_shared<LayoutNode>();
    node->kind = LayoutNode::Kind::Split;
    node->orientation = orientation == QStringLiteral("horizontal") ? Qt::Horizontal : Qt::Vertical;
    for (const auto &value : children) {
        if (!value.isObject()) throw std::invalid_argument("invalid child node");
        node->children.append(nodeFromJson(value.toObject(), seen, depth + 1));
    }
    QJsonArray ratios = object.value(QStringLiteral("size_ratios")).toArray();
    if (ratios.isEmpty()) ratios = object.value(QStringLiteral("sizes")).toArray();
    if (!ratios.isEmpty()) {
        if (ratios.size() != node->children.size())
            throw std::invalid_argument("layout size count mismatch");
        QList<double> values;
        double total = 0.0;
        for (const auto &value : ratios) {
            const double size = value.toDouble(-1.0);
            if (!std::isfinite(size) || size < 0.0)
                throw std::invalid_argument("layout sizes must be finite and non-negative");
            values.append(size);
            total += size;
        }
        if (total <= 0.0) throw std::invalid_argument("layout size total must be positive");
        for (double value : values) node->sizes.append(static_cast<int>(value / total * SizeBase));
        node->sizes.last() += SizeBase - std::accumulate(node->sizes.cbegin(), node->sizes.cend(), 0);
    }
    return node;
}

void LayoutTree::validate(const std::shared_ptr<LayoutNode> &node,
                          QSet<QString> &seen, int depth)
{
    if (!node || depth > 64) throw std::invalid_argument("invalid layout tree");
    if (node->kind == LayoutNode::Kind::Pane) {
        if (node->paneId.trimmed().isEmpty() || seen.contains(node->paneId))
            throw std::invalid_argument("invalid or duplicate pane id");
        seen.insert(node->paneId);
        return;
    }
    if (node->children.isEmpty()) throw std::invalid_argument("split has no children");
    if (!node->sizes.isEmpty() && node->sizes.size() != node->children.size())
        throw std::invalid_argument("layout size count mismatch");
    for (const auto &child : node->children) validate(child, seen, depth + 1);
}

LayoutTree LayoutTree::fromJson(const QJsonObject &object)
{
    QSet<QString> seen;
    const auto root = nodeFromJson(object, seen, 0);
    if (root->kind != LayoutNode::Kind::Split)
        throw std::invalid_argument("layout root must be a split node");
    return LayoutTree(root);
}

bool saveLayoutAtomic(const QString &path, const QJsonObject &data, QString *error)
{
    if (!QDir().mkpath(QFileInfo(path).absolutePath())) {
        if (error) *error = QStringLiteral("无法创建布局目录");
        return false;
    }
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        if (error) *error = file.errorString();
        return false;
    }
    if (file.write(QJsonDocument(data).toJson(QJsonDocument::Indented)) < 0 || !file.commit()) {
        if (error) *error = file.errorString();
        return false;
    }
    return true;
}

QJsonObject loadLayoutFile(const QString &path, QString *error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error) *error = file.errorString();
        return {};
    }
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        if (error) *error = parseError.errorString();
        return {};
    }
    return document.object();
}
} // namespace darkeye::myads


