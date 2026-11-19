#pragma once

#include "ui/layouts/myads/DockTheme.h"
#include "ui/layouts/myads/LayoutTree.h"
#include "ui/layouts/myads/PaneWidget.h"

#include <QHash>
#include <QJsonObject>
#include <QPointer>
#include <QWidget>
#include <functional>
#include <optional>

namespace darkeye::myads {

enum class Placement { Left, Right, Top, Bottom };
enum class DropZone {
    Center, Left, Right, Top, Bottom,
    RootLeft, RootRight, RootTop, RootBottom
};

class ContentConfig final
{
public:
    explicit ContentConfig(QString contentId = {});
    ContentConfig &setWidget(QWidget *widget);
    ContentConfig &setWindowTitle(const QString &title);
    ContentConfig &setIcon(const QIcon &icon);
    ContentConfig &setCloseable(bool closeable);
    ContentConfig &setClosePolicy(ClosePolicy policy);

    QString contentId;
    QWidget *widget = nullptr;
    QString title;
    QIcon icon;
    bool closeable = true;
    ClosePolicy closePolicy = ClosePolicy::DeleteLater;
};

class WorkspaceWidget final : public QWidget
{
    Q_OBJECT
public:
    using ContentDescriptor = std::function<QJsonObject(const PaneWidget *, const QString &)>;
    using PaneMetadata = std::function<QJsonObject(const PaneWidget *)>;
    using ContentFactory = std::function<std::optional<ContentConfig>(const QJsonObject &)>;

    explicit WorkspaceWidget(QWidget *parent = nullptr,
                             const DockTheme &theme = {});
    ~WorkspaceWidget() override;
    [[nodiscard]] const LayoutTree &layoutTree() const noexcept;
    [[nodiscard]] PaneWidget *rootPane() const;
    [[nodiscard]] PaneWidget *activePane() const;
    void setActivePane(PaneWidget *pane);
    [[nodiscard]] PaneWidget *pane(const QString &paneId) const;
    [[nodiscard]] PaneWidget *findPaneByContentId(const QString &contentId) const;
    [[nodiscard]] QList<PaneWidget *> panes() const;

    ContentConfig createContentConfig(const QString &contentId = {});
    bool fillPane(PaneWidget *pane, const ContentConfig &content);
    void beginLayoutUpdate();
    void endLayoutUpdate();
    PaneWidget *split(PaneWidget *pane, Placement placement, int percent = 50);
    bool moveContent(const QString &sourcePaneId, const QString &contentId,
                     const QString &targetPaneId, DropZone zone, int insertionIndex = -1);
    void resetToSingleEmptyPane();

    // Compatibility API used by the first C++ migration.
    QString addPanel(const QString &title, QWidget *content,
                     const QString &paneId = {});
    QString splitPane(const QString &paneId, Qt::Orientation orientation,
                      bool insertBefore, int percent = 50);
    bool save(const QString &path, QString *errorMessage = nullptr) const;
    bool load(const QString &path, QString *errorMessage = nullptr);

    bool saveLayout(const QString &path,
                    const ContentDescriptor &descriptor = {},
                    const PaneMetadata &metadata = {},
                    QString *errorMessage = nullptr) const;
    bool loadLayout(const QString &path, const ContentFactory &factory = {},
                    QString *errorMessage = nullptr);
    void applyTheme(const DockTheme &theme);
    [[nodiscard]] DockTheme theme() const;
    [[nodiscard]] static DropZone hitTest(const QRect &paneRect,
                                          const QPoint &position);
    [[nodiscard]] static QRect previewRectForZone(const QRect &area,
                                                  DropZone zone);
    void showDropPreview(PaneWidget *target, DropZone zone);
    void hideDropPreview();

signals:
    void contentClosed(const QString &contentId);
    void layoutChanged();
    void contentMoved(const QString &contentId, const QString &paneId);
    void activeContentChanged();
    void activePaneChanged(const QString &paneId);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    class PreviewOverlay;
    void rebuild(bool preserveContents = true);
    QWidget *buildNode(const std::shared_ptr<LayoutNode> &node);
    PaneWidget *createPane(const QString &paneId);
    void connectPane(PaneWidget *pane);
    void connectSplitters(QWidget *widget, const std::shared_ptr<LayoutNode> &node);
    QString nextPaneId() const;
    QString nextContentId();
    void handleDragMove(PaneWidget *target, const QPoint &globalPosition);
    void handleDrop(PaneWidget *target, const QPoint &globalPosition,
                    const QString &sourcePaneId, const QString &contentId);
    DropZone dropZone(PaneWidget *target, const QPoint &globalPosition) const;
    QRect previewRect(PaneWidget *target, DropZone zone) const;
    void removeEmptyPane(const QString &paneId);

    LayoutTree m_tree;
    QWidget *m_rootWidget = nullptr;
    QHash<QString, PaneWidget *> m_panes;
    QHash<QString, PaneWidget *> m_reusablePanes;
    PreviewOverlay *m_preview = nullptr;
    QPointer<QWidget> m_dockDragTarget;
    DockTheme m_theme;
    QString m_activePaneId;
    int m_contentCounter = 0;
    int m_layoutUpdateDepth = 0;
    bool m_layoutRebuildPending = false;
};

} // namespace darkeye::myads

