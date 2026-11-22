#pragma once

#include "Mesh.h"
#include "Camera.h"
#include "Scene.h"
#include "Lighting.h"

#include <QColor>
#include <QQuaternion>
#include <QRhiWidget>
#include <memory>

class QAction;
class QShowEvent;
class QHideEvent;

namespace darkeye::render3d {

class Renderer;

struct RenderStatistics {
    QString backend;
    QSize pixelSize;
    int samples = 1;
    quint64 submittedFrames = 0;
    quint64 resourceGenerations = 0;
    quint64 pipelineGenerations = 0;
    quint64 geometryUploads = 0;
    quint64 materialBindingBuilds = 0;
    quint64 textureUploads = 0;
    quint64 textureUploadBytes = 0;
    // Only this renderer's buffers; excludes Qt render targets/driver overhead.
    quint64 bufferBytes = 0;
    quint64 textureBytes = 0; // source texels + renderer-owned linear targets
    quint64 sourceTextureBytes = 0; // excludes Qt targets, padding and driver allocations
    int geometryCount = 0;
    int textureCount = 0;
    int drawCalls = 0;
    int visibleItems = 0;
    int transmissionPasses = 0;
    int transmissionFallbackItems = 0; // distant overlaps sharing a merged snapshot
    double cpuFrameMs = 0;
};

class RhiSceneWidget final : public QRhiWidget {
    Q_OBJECT
    Q_PROPERTY(bool statisticsVisible READ statisticsVisible WRITE setStatisticsVisible NOTIFY statisticsVisibleChanged)
public:
    explicit RhiSceneWidget(QWidget *parent = nullptr);
    ~RhiSceneWidget() override;

    // Invalid CPU data is rejected without replacing the current scene.
    bool setMesh(MeshData mesh);
    void setScene(std::shared_ptr<Scene> scene);
    [[nodiscard]] std::shared_ptr<Scene> scene() const { return m_scene; }
    void setCamera(const Camera &camera);
    [[nodiscard]] Camera camera() const { return m_camera; }
    void setRotation(const QQuaternion &rotation);
    void setCameraDistance(float distance);
    void setClearColor(const QColor &color);
    // Source texture budget excludes render targets and driver allocations.
    void setTextureBudget(quint64 bytes);
    void setEnvironment(std::shared_ptr<const EnvironmentMap> environment);
    void setLighting(const LightingSettings &settings);
    [[nodiscard]] LightingSettings lighting() const;
    [[nodiscard]] const RenderStatistics &statistics() const;
    [[nodiscard]] QString lastError() const;
    // Optional overlay, disabled by default. The action can be shared with a host menu.
    void setStatisticsVisible(bool visible);
    [[nodiscard]] bool statisticsVisible() const { return m_statisticsVisible; }
    [[nodiscard]] QAction *statisticsAction() const { return m_statisticsAction; }

signals:
    void renderingError(const QString &message);
    void renderingRecovered();
    void statisticsVisibleChanged(bool visible);

protected:
    bool event(QEvent *event) override;
    bool eventFilter(QObject *object, QEvent *event) override;
    void initialize(QRhiCommandBuffer *cb) override;
    void render(QRhiCommandBuffer *cb) override;
    void releaseResources() override;
    void showEvent(QShowEvent *event) override;
    void hideEvent(QHideEvent *event) override;

private:
    void reportError(const QString &message);

    // CPU state survives releaseResources(); Renderer owns all GPU resources.
    std::shared_ptr<Scene> m_scene;
    QQuaternion m_rotation;
    Camera m_camera;
    QColor m_clearColor{QStringLiteral("#151c2b")};
    std::unique_ptr<Renderer> m_renderer;
    RenderStatistics m_statistics;
    QString m_lastError;
    struct StatisticsOverlay;
    std::unique_ptr<StatisticsOverlay> m_statisticsOverlay;
    QAction *m_statisticsAction = nullptr;
    bool m_statisticsVisible = false;
    quint64 m_visibilityGeneration = 0;
};

} // namespace darkeye::render3d
