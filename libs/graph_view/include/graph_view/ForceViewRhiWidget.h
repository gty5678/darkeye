#ifndef FORCEVIEWRHIWIDGET_H
#define FORCEVIEWRHIWIDGET_H

#include <QColor>
#include <QEvent>
#include <QMouseEvent>
#include <QPointF>
#include <QRectF>
#include <QStringList>
#include <QTimer>
#include <QVariant>
#include <QVector>
#include <QWidget>

#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <vector>

#include "PhysicsState.h"
#include "Simulation.h"
#include "MsdfFontAtlas.h"
#include "GpuGraphGeometry.h"

class ForceRhiCanvas;
class QRhi;
class QRhiBuffer;
class QRhiCommandBuffer;
class QRhiGraphicsPipeline;
class QRhiShaderResourceBindings;
class QRhiSampler;
class QRhiTexture;
class GpuSimulation;
class ForcePerformanceOverlay;
class QMenu;
class QAction;

class ForceViewRhiWidget : public QWidget
{
    Q_OBJECT

public:
    // 构造 QRhi 力导向图控件，初始化画布、字体图集和默认渲染状态。
    explicit ForceViewRhiWidget(QWidget* parent = nullptr);
    // 析构控件，停止仿真线程并释放 QRhi/字体图集相关资源。
    ~ForceViewRhiWidget() override;

    // 设置整张图的数据，包括节点、边、初始位置、标签、半径和颜色。
    void setGraph(int nNodes,
                  const QVector<int>& edges,
                  const QVector<float>& pos,
                  const QStringList& id,
                  const QStringList& labels,
                  const QVector<float>& radii,
                  const QVector<QColor>& nodeColors = QVector<QColor>());

    // 暂停力导向仿真线程。
    void pauseSimulation();
    // 恢复力导向仿真线程。
    void resumeSimulation();
    // 重启当前图的力导向仿真。
    void restartSimulation();
    // 设置仿真后端：auto / gpu / cpu；设备不支持时回退 CPU。
    void setSimulationBackendMode(const QString& mode);
    // 返回用户请求的仿真后端模式。
    QString simulationBackendMode() const;
    // 返回当前实际生效的仿真后端。
    QString activeSimulationBackend() const;
    void setGpuProfilingEnabled(bool enabled);
    // 创建默认隐藏的性能表及右键菜单；首次右键时自动调用。
    void enablePerformanceDiagnostics();
    // 显示或隐藏性能统计；正式界面和 Demo 均支持右键切换。
    void setPerformanceOverlayVisible(bool visible);
    // 返回当前图请求的性能表可见状态。
    bool isPerformanceOverlayVisible() const;

    // 设置多体斥力强度。
    void setManyBodyStrength(float value);
    // 设置中心力强度。
    void setCenterStrength(float value);
    // 设置边连接力强度。
    void setLinkStrength(float value);
    // 设置边的目标距离。
    void setLinkDistance(float value);
    // 设置节点碰撞半径。
    void setCollisionRadius(float value);
    // 设置节点碰撞力强度。
    void setCollisionStrength(float value);

    // 设置节点半径缩放因子。
    void setRadiusFactor(float f);
    // 设置边宽缩放因子。
    void setSideWidthFactor(float f);
    // 设置文字显示阈值缩放因子。
    void setTextThresholdFactor(float f);
    // 批量设置节点颜色。
    void setNodeColors(const QVector<QColor>& colors);
    // 设置箭头大小缩放因子。
    void setArrowScale(float f);
    // 设置是否绘制边箭头。
    void setArrowEnabled(bool enabled);
    // 设置 hover 时需要高亮的邻居搜索深度。
    void setNeighborDepth(int depth);
    // 设置 QRhi render pass 清屏背景色。
    void setBackgroundColor(const QColor& color);

    // 设置普通边颜色。
    void setEdgeColor(const QColor& c);
    // 返回普通边颜色。
    QColor edgeColor() const;
    // 设置暗化边颜色。
    void setEdgeDimColor(const QColor& c);
    // 返回暗化边颜色。
    QColor edgeDimColor() const;

    // 设置普通节点颜色的默认值。
    void setBaseColor(const QColor& c);
    // 返回普通节点默认颜色。
    QColor baseColor() const;
    // 设置暗化节点颜色。
    void setDimColor(const QColor& c);
    // 返回暗化节点颜色。
    QColor dimColor() const;
    // 设置 hover 高亮颜色。
    void setHoverColor(const QColor& c);
    // 返回 hover 高亮颜色。
    QColor hoverColor() const;

    // 设置普通文字颜色。
    void setTextColor(const QColor& c);
    // 返回普通文字颜色。
    QColor textColor() const;
    // 设置暗化文字颜色。
    void setTextDimColor(const QColor& c);
    // 返回暗化文字颜色。
    QColor textDimColor() const;

    // 设置 MSDF 字体路径并触发字体图集重建。
    void setFontPath(const QString& path);
    // 返回当前 MSDF 字体路径。
    QString fontPath() const;

    // 设置某个节点是否处于拖拽状态。
    void setDragging(int nodeId, bool dragging);
    // 返回当前图内容的场景坐标包围盒。
    QRectF getContentRect() const;
    // 将视图缩放和平移到完整显示当前图内容。
    void fitViewToContent();

    // 返回当前视图中心 X。
    float getPanX() const { return m_panX; }
    // 返回当前视图中心 Y。
    float getPanY() const { return m_panY; }
    // 返回当前视图缩放比例。
    float getZoom() const { return m_zoom; }
    // 按节点 id 查询当前渲染位置。
    QPointF getNodePosition(const QString& nodeId) const;
    // 返回当前图中的节点 id 列表。
    QStringList getNodeIds() const;

    // 运行时添加一个节点，并同步重建仿真、邻接和文字缓存。
    void add_node_runtime(const QString& nodeId, float x = 0.0f, float y = 0.0f,
                          const QString& label = QString(), float radius = 7.0f,
                          const QColor& color = QColor());
    // 运行时更新一个节点的显示属性，不改变拓扑或当前位置。
    void update_node_runtime(const QString& nodeId, const QString& label,
                             float radius, const QColor& color = QColor());
    // 运行时按 id 删除一个节点。
    void remove_node_runtime(const QString& nodeId);
    // 运行时添加一条边。
    void add_edge_runtime(const QString& uNodeId, const QString& vNodeId);
    // 运行时删除一条边。
    void remove_edge_runtime(const QString& uNodeId, const QString& vNodeId);
    // 批量应用图增删 diff。
    void apply_diff_runtime(const QVariantList& diffList);

    // 同步请求内部 QRhi 画布和外层 QWidget 更新。
    void update();

signals:
    // 左键点击节点时发出节点 id。
    void nodeLeftClicked(const QString& nodeId);
    // 右键点击节点时发出节点 id。
    void nodeRightClicked(const QString& nodeId);
    // hover 节点变化时发出节点 id，离开时为空。
    void nodeHovered(const QString& nodeId);
    // hover 节点变化时发出节点 id、位置、半径、缩放和拖拽状态。
    void nodeHoveredWithInfo(const QString& nodeId, float x, float y, float radius, float scale, bool dragging);
    // 节点被按下时发出节点 id。
    void nodePressed(const QString& nodeId);
    // 节点拖拽移动时发出节点 id。
    void nodeDragged(const QString& nodeId);
    // 节点释放时发出节点 id。
    void nodeReleased(const QString& nodeId);
    // 视图缩放比例变化时发出当前缩放值。
    void scaleChanged(float scale);
    // 仿真 alpha 变化时发出当前 alpha。
    void alphaUpdated(float alpha);
    // 帧率统计更新时发出 FPS。
    void fpsUpdated(float fps);
    // 每帧绘制耗时更新时发出毫秒值。
    void paintTimeUpdated(float ms);
    // QRhi 画布首次提交可用于显示的帧时发出。
    void firstFrameSubmitted();
    // 仿真 tick 耗时更新时发出毫秒值。
    void tickTimeUpdated(float ms);
    // 仿真线程启动时发出。
    void simulationStarted();
    // 仿真线程停止时发出。
    void simulationStopped();
    // 用户请求的仿真后端模式变化时发出。
    void simulationBackendModeChanged(const QString& mode);
    // 实际生效的仿真后端变化时发出。
    void simulationBackendChanged(const QString& backend);
    void performanceDetailsUpdated(const QVariantMap& details);

protected:
    void contextMenuEvent(QContextMenuEvent* event) override;
    // 处理滚轮缩放，并保持鼠标下方场景点稳定。
    void wheelEvent(QWheelEvent* event) override;
    // 处理鼠标按下，用于节点选择、拖拽或视图平移。
    void mousePressEvent(QMouseEvent* event) override;
    // 处理鼠标移动，用于拖拽节点、平移和 hover 检测。
    void mouseMoveEvent(QMouseEvent* event) override;
    // 处理鼠标释放，结束拖拽或平移。
    void mouseReleaseEvent(QMouseEvent* event) override;
    // 处理鼠标离开，清理 hover 状态并发出离开信号。
    void leaveEvent(QEvent* event) override;
    // 处理尺寸变化，在子画布同步绘制前更新视口并请求重绘。
    void resizeEvent(QResizeEvent* event) override;

private slots:
    // QRhiWidget 提交帧后更新 FPS 统计。
    void onFrameSubmitted();

private:
    friend class ForceRhiCanvas;
    friend class GpuSimulationTest;

    struct Vertex {
        float x = 0.0f;
        float y = 0.0f;
        float localX = 0.0f;
        float localY = 0.0f;
        float shape = 0.0f;
        float r = 1.0f;
        float g = 1.0f;
        float b = 1.0f;
        float a = 1.0f;
    };

    struct TextVertex {
        float x = 0.0f;
        float y = 0.0f;
        float u = 0.0f;
        float v = 0.0f;
        float r = 1.0f;
        float g = 1.0f;
        float b = 1.0f;
        float a = 1.0f;
        float rangeScale = 1.0f;
        float node = -1.0f;
        float anchorX = 0.0f, anchorY = 0.0f;
    };

    struct GlyphQuad {
        float x0 = 0.0f;
        float y0 = 0.0f;
        float x1 = 0.0f;
        float y1 = 0.0f;
        float u0 = 0.0f;
        float v0 = 0.0f;
        float u1 = 0.0f;
        float v1 = 0.0f;
    };

    struct LabelLayoutEntry {
        float totalWidth = 0.0f;
        std::vector<GlyphQuad> quads;
    };

    struct MsdfAtlasBuildResult {
        MsdfFontAtlas::AtlasData data;
        bool success = false;
        QString error;
        int buildId = 0;
    };

    static constexpr int kSimTickIntervalMs = 16;
    static constexpr int kGpuQueueStepLimit = 8;
    static constexpr float kGpuMaxStepDisplacement = 15.0f;
    static constexpr int kFitViewDelayMs = 100;
    static constexpr int kCircleSegments = 24;
    static constexpr float kInitialLayoutScaleFactor = 25.0f;
    static constexpr float kInitialLayoutBaseOffset = 50.0f;
    static constexpr float kContentPaddingRatio = 0.1f;
    static constexpr float kContentPaddingAbs = 20.0f;
    static constexpr float kViewPadding = 50.0f;
    static constexpr float kFitViewScaleMargin = 0.9f;
    static constexpr float kFitViewZoomMin = 0.01f;
    static constexpr float kFitViewZoomMax = 10.0f;
    static constexpr float kFitViewMaxNodeRadiusPx = 22.0f;
    static constexpr float kZoomMin = 0.1f;
    static constexpr float kZoomMax = 10.0f;
    static constexpr float kZoomWheelFactor = 1.15f;
    static constexpr float kDefaultNodeRadius = 5.0f;
    static constexpr float kLineMinLength = 1e-3f;
    static constexpr float kHoverRadiusScale = 1.1f;
    static constexpr float kTextThresholdBase = 0.7f;
    static constexpr float kTextThresholdShowMul = 1.5f;
    static constexpr float kManyBodyDistanceLimitSq = 40000.0f;

    // 从 QRhiWidget 初始化回调中取得 QRhi 对象并创建基础资源。
    void initializeRhi(QRhiCommandBuffer* cb);
    // 执行一帧 QRhi 绘制，包含资源上传、图形批次和文字批次绘制。
    void renderRhi(QRhiCommandBuffer* cb);
    // 释放图形和文字相关 QRhi 资源。
    void releaseRhiResources();
    // 确保基础 uniform buffer 和 shader resource binding 可用。
    bool ensureRhiResources();
    // 确保图形顶点缓冲足够容纳当前帧数据。
    bool ensureVertexBuffer(quint32 byteSize);
    // 重建节点/边/箭头使用的图形渲染管线。
    void rebuildPipeline();
    // 释放文字渲染相关 QRhi 资源。
    void releaseTextRhiResources();
    // 确保文字顶点缓冲足够容纳当前帧数据。
    bool ensureTextVertexBuffer(quint32 byteSize);
    // 确保文字 uniform、图集纹理、采样器和 SRB 可用。
    bool ensureTextResources(class QRhiResourceUpdateBatch* batch);
    // 重建 MSDF 文字渲染管线。
    void rebuildTextPipeline();

    // 创建内部 QRhiWidget 画布并加入布局。
    void setupLayout();
    void setupPerformanceOverlay();
    void positionPerformanceOverlay();
    // 启动 CPU 仿真线程。
    void startSimThread();
    // 停止 CPU 仿真线程并等待退出。
    void stopSimThread();
    // CPU 仿真循环，推进物理状态并请求重绘。
    void simLoop();
    void advanceGpuSimulation(QRhiCommandBuffer* cb);
    void tickGpuSimulation();
    // 按当前参数重新创建 Simulation 力模型。
    void rebuildSimulation();
    // 以线程安全方式请求 QRhi 画布更新。
    void requestCanvasUpdate();
    // 应用用户选择的仿真后端模式。
    void applySimulationBackendPreference();

    // 根据半径/边宽因子更新实际显示半径和边宽。
    void updateFactor();
    // 根据边列表重建邻接表。
    void rebuildNeighborsFromEdges();
    // 根据 hover 节点和邻居深度更新高亮邻居掩码。
    void updateNeighborMaskForHover(int hoverIndex);
    // 推进 hover 淡入/淡出动画状态。
    void advanceHover();
    // 将屏幕坐标转换为场景坐标。
    void screenToScene(float sx, float sy, float& outX, float& outY) const;
    // 在场景坐标下拾取命中的节点索引。
    int pickNodeAt(float sceneX, float sceneY) const;
    // 按索引删除节点，并同步边、缓存和仿真状态。
    bool removeNodeInternal(int indexToRemove, bool restartAfterChange = true);
    // 图结构运行时变更后清理选中、拖拽、hover 等临时交互状态。
    void resetInteractionStateForGraphMutation();

    // 构建当前帧的边、箭头和节点顶点批次。
    void prepareFrameVertices();
    bool prepareGpuGeometry(QRhiCommandBuffer* cb);
    // 追加一条带 shader 抗锯齿局部坐标的边四边形。
    void appendLineQuad(std::vector<Vertex>& out, int s, int d, const QColor& color, const float* pos);
    // 追加一条边末端箭头三角形。
    void appendArrow(std::vector<Vertex>& out, int s, int d, const QColor& color, const float* pos);
    // 追加一个带 shader 抗锯齿局部坐标的圆节点。
    void appendCircle(std::vector<Vertex>& out, float x, float y, float radius, const QColor& color);
    // 构建当前帧的 dim/rest/hover 文字顶点批次。
    void prepareTextVertices();
    // 追加单个节点标签的 MSDF 文字顶点。
    void appendTextLabel(std::vector<TextVertex>& out,
                         int index,
                         const QColor& color,
                         float alpha,
                         float fontScale,
                         const float* pos);
    // 因标签集合变化而清空文字布局缓存并重新启动图集构建。
    void invalidateTextLayoutForLabels();
    // 同步重建 MSDF 字体图集。
    void rebuildMsdfAtlas();
    // 异步启动 MSDF 字体图集构建。
    void startMsdfAtlasBuildAsync();
    // 如果异步图集构建完成，则应用结果并重建标签布局。
    void applyMsdfAtlasResultIfReady();
    // 根据当前图集为每个标签生成可复用 glyph 布局。
    void rebuildLabelLayoutCache();

    // 返回指定节点的显示颜色，索引无效时返回默认节点色。
    QColor nodeColorFor(int i) const;
    // 按 t 在两个 QColor 之间线性插值。
    QColor mixColor(const QColor& c1, const QColor& c2, float t) const;
    // 查找当前平台可用的默认字体文件路径。
    static QString detectDefaultFontPath();
    // 根据当前字体路径生成 MSDF 图集配置。
    MsdfFontAtlas::Config makeFontConfig() const;

    ForceRhiCanvas* m_canvas = nullptr;
    ForcePerformanceOverlay* m_performanceOverlay = nullptr;
    QMenu* m_graphContextMenu = nullptr;
    QAction* m_performanceAction = nullptr;

    std::unique_ptr<PhysicsState> m_physicsState;
    std::unique_ptr<Simulation> m_simulation;
    std::thread m_simThread;
    std::atomic<bool> m_simThreadRunning{false};
    std::mutex m_simMutex;
    std::condition_variable m_simCv;
    std::atomic<bool> m_simActive{false};
    std::atomic<bool> m_allowWarmup{false};
    std::unique_ptr<GpuSimulation> m_gpuSimulation;
    std::unique_ptr<GpuGraphGeometry> m_gpuGeometry;
    std::vector<GpuGraphGeometry::Instance> m_gpuDescriptions;
    bool m_usingGpuGeometry = false;
    bool m_usingGpuText = false;
    bool m_gpuDescriptionsDirty = true;
    quint64 m_gpuDescriptionRevision = 0;
    bool m_gpuDescriptionHoverActive = false;
    bool m_gpuProfiling = false;
    double m_lastGpuSnapshot = 0, m_lastPerformanceReport = 0;
    QTimer m_gpuTimer;
    QString m_simulationBackendMode = QStringLiteral("auto");
    QString m_activeSimulationBackend = QStringLiteral("cpu");

    QStringList m_ids;
    QStringList m_labels;
    QVector<float> m_showRadiiBase;
    QVector<float> m_showRadii;
    QVector<QColor> m_nodeColors;
    std::vector<std::vector<int>> m_neighbors;
    std::vector<uint8_t> m_neighborMask;
    std::vector<uint8_t> m_lastNeighborMask;
    std::vector<int> m_lastDimEdges;
    std::vector<int> m_lastHighlightEdges;

    float m_manyBodyStrength = 10000.0f;
    float m_linkStrength = 0.3f;
    float m_linkDistance = 30.0f;
    float m_centerStrength = 0.01f;
    float m_collisionRadius = 10.0f;
    float m_collisionStrength = 50.0f;

    float m_sideWidthBase = 2.0f;
    float m_sideWidthFactor = 1.0f;
    float m_sideWidth = 1.0f;
    float m_radiusFactor = 1.0f;
    float m_arrowScale = 1.0f;
    bool m_arrowEnabled = true;
    float m_textThresholdFactor = 1.0f;
    int m_neighborDepth = 2;

    QColor m_edgeColor = QColor("#D5D5D5");
    QColor m_edgeDimColor = QColor("#F7F7F7");
    QColor m_baseColor = QColor("#5C5C5C");
    QColor m_dimColor = QColor("#F7F7F7");
    QColor m_hoverColor = QColor("#8F6AEE");
    QColor m_textColor = QColor("#5C5C5C");
    QColor m_textDimColor = QColor("#F7F7F7");
    QColor m_backgroundColor = QColor(255, 255, 255);

    int m_hoverIndex = -1;
    int m_lastHoverIndex = -1;
    float m_hoverStep = 0.1f;
    float m_hoverGlobal = 0.0f;
    int m_selectedIndex = -1;
    bool m_dragging = false;
    Qt::MouseButton m_pressedButton = Qt::NoButton;
    QPointF m_rightPressPosition;
    bool m_rightButtonHeld = false;
    bool m_rightGestureMoved = false;
    float m_dragOffsetX = 0.0f;
    float m_dragOffsetY = 0.0f;

    float m_panX = 0.0f;
    float m_panY = 0.0f;
    float m_zoom = 1.0f;
    int m_viewportW = 1;
    int m_viewportH = 1;
    bool m_isPanning = false;
    float m_panStartX = 0.0f;
    float m_panStartY = 0.0f;
    float m_panStartPanX = 0.0f;
    float m_panStartPanY = 0.0f;

    std::vector<Vertex> m_frameVertices;
    std::vector<float> m_renderPosSnapshot;
    // m_frameVertices 内的绘制区间。renderRhi() 会有意把这些区间与文字区间交错绘制，
    quint32 m_geometryDimFirst = 0;
    quint32 m_geometryDimCount = 0;
    quint32 m_geometryHighlightFirst = 0;
    quint32 m_geometryHighlightCount = 0;
    quint32 m_geometryRestFirst = 0;
    quint32 m_geometryRestCount = 0;

    QRhi* m_rhi = nullptr;
    std::unique_ptr<QRhiBuffer> m_vertexBuffer;
    std::unique_ptr<QRhiBuffer> m_uniformBuffer;
    std::unique_ptr<QRhiShaderResourceBindings> m_srb;
    std::unique_ptr<QRhiGraphicsPipeline> m_pipeline;
    quint32 m_vertexBufferSize = 0;
    bool m_pipelineDirty = true;
    bool m_rhiReady = false;

    std::unique_ptr<QRhiBuffer> m_textVertexBuffer;
    std::unique_ptr<QRhiBuffer> m_textUniformBuffer;
    std::unique_ptr<QRhiTexture> m_textAtlasTexture;
    std::unique_ptr<QRhiSampler> m_textSampler;
    std::unique_ptr<QRhiShaderResourceBindings> m_textSrb;
    std::unique_ptr<QRhiGraphicsPipeline> m_textPipeline;
    quint32 m_textVertexBufferSize = 0;
    int m_textAtlasUploadedGeneration = -1;
    bool m_textPipelineDirty = true;

    std::unique_ptr<MsdfFontAtlas> m_fontAtlas;
    QString m_fontPath;
    float m_msdfFontSize = 8.0f;
    std::unordered_map<std::string, LabelLayoutEntry> m_labelLayoutCache;
    std::vector<const LabelLayoutEntry*> m_labelLayoutByIndex;
    std::vector<TextVertex> m_textVertices;
    std::vector<uint8_t> m_retainedLabels;
    // m_textVertices 内的绘制区间：dim 文字在高亮图形下方，普通文字在节点上方，
    // hover 文字在所有内容上方。
    quint32 m_textDimFirst = 0;
    quint32 m_textDimCount = 0;
    quint32 m_textRestFirst = 0;
    quint32 m_textRestCount = 0;
    quint32 m_textHoverFirst = 0;
    quint32 m_textHoverCount = 0;
    std::thread m_msdfAtlasThread;
    std::atomic<bool> m_msdfAtlasThreadRunning{false};
    std::mutex m_msdfAtlasMutex;
    bool m_msdfAtlasResultReady = false;
    MsdfAtlasBuildResult m_msdfAtlasResult;
    int m_msdfAtlasBuildId = 0;

    int m_frameCount = 0;
    bool m_hasSubmittedFrame = false;
    double m_lastFpsTime = 0.0;
    float m_currentFps = 0.0f;
};

#endif // FORCEVIEWRHIWIDGET_H

