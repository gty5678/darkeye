#pragma once

#include <QJsonObject>
#include <QList>
#include <QSet>
#include <QString>
#include <Qt>
#include <memory>

namespace darkeye::myads {

struct LayoutNode
{
    enum class Kind { Pane, Split };
    Kind kind = Kind::Pane;
    QString paneId;
    Qt::Orientation orientation = Qt::Horizontal;
    QList<std::shared_ptr<LayoutNode>> children;
    QList<int> sizes;
};

class LayoutTree final
{
public:
    static constexpr int SchemaVersion = 1;
    static constexpr int SizeBase = 1000;

    LayoutTree();
    explicit LayoutTree(std::shared_ptr<LayoutNode> root);
    [[nodiscard]] const std::shared_ptr<LayoutNode> &root() const noexcept;
    [[nodiscard]] QList<QString> paneIds() const;
    [[nodiscard]] QJsonObject toJson() const;
    [[nodiscard]] static LayoutTree fromJson(const QJsonObject &object);

    void addPaneToRoot(const QString &paneId);
    void split(const QString &paneId, Qt::Orientation orientation,
               bool insertBefore, const QString &newPaneId,
               int newPanePercent = 50);
    void splitRoot(Qt::Orientation orientation, bool insertBefore,
                   const QString &newPaneId, int newPanePercent = 50);
    [[nodiscard]] bool removePane(const QString &paneId);
    void setSizes(const QString &splitPath, const QList<int> &sizes);
    void normalize();

private:
    std::shared_ptr<LayoutNode> m_root;
    [[nodiscard]] static std::shared_ptr<LayoutNode> findParent(
        const std::shared_ptr<LayoutNode> &node, const QString &paneId);
    [[nodiscard]] static std::shared_ptr<LayoutNode> nodeAtPath(
        const std::shared_ptr<LayoutNode> &root, const QString &path);
    static bool removeFrom(const std::shared_ptr<LayoutNode> &parent,
                           const QString &paneId);
    static void normalizeNode(const std::shared_ptr<LayoutNode> &node);
    static void validate(const std::shared_ptr<LayoutNode> &node,
                         QSet<QString> &seen, int depth);
    static QJsonObject nodeToJson(const std::shared_ptr<LayoutNode> &node);
    static std::shared_ptr<LayoutNode> nodeFromJson(const QJsonObject &object,
                                                    QSet<QString> &seen,
                                                    int depth);
};

bool saveLayoutAtomic(const QString &path, const QJsonObject &data,
                      QString *errorMessage = nullptr);
QJsonObject loadLayoutFile(const QString &path,
                           QString *errorMessage = nullptr);

} // namespace darkeye::myads
