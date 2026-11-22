#pragma once

#include "render3d/RhiSceneWidget.h"
#include "render3d/ModelAsset.h"
#include "dvd_scene/CoverTextureCache.h"
#include <QHash>
#include <QWidget>

class QMenu;

namespace darkeye {

class ShopSceneWidget final : public QWidget {
    Q_OBJECT
public:
    explicit ShopSceneWidget(QWidget *parent = nullptr);
    void setCoverPaths(const QStringList &paths);
    void setOutlineColor(const QColor &color);
    int coverCapacity() const;
    QStringList coverPaths() const { return m_paths.mid(0, coverCapacity()); }
    render3d::RhiSceneWidget *canvas() const { return m_canvas; }
signals:
    void coverClicked(int index);
    void coverCapacityChanged();
    void sceneError(const QString &message);
protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
private:
    void updateCamera();
    void rebuildRows();
    void updateCoverVisibility();
    void requestCovers();
    void updateTextures(bool keepExisting = true);
    void updateHover(const QPointF &position);
    int coverAt(const QPointF &position) const;
    render3d::RhiSceneWidget *m_canvas;
    QWidget *m_outline;
    QMenu *m_contextMenu;
    std::shared_ptr<render3d::Scene> m_scene;
    render3d::ModelNode m_coverModel;
    render3d::ModelAsset m_shelfModel, m_backModel;
    dvd_scene::CoverTextureCache m_covers;
    QHash<render3d::NodeId, int> m_indices;
    QList<render3d::NodeId> m_roots;
    QStringList m_paths, m_keys;
    int m_rows = 0;
    int m_coverPixels = 0;
    render3d::Camera m_camera;
    QPointF m_lastPosition;
    bool m_rotating = false;
    bool m_rightDragged = false;
    int m_hovered = -1;
};

} // namespace darkeye
