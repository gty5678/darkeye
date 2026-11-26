#include "render3d/RhiSceneWidget.h"
#include "Renderer.h"

#include <QLoggingCategory>
#include <QElapsedTimer>
#include <QAction>
#include <QFrame>
#include <QHideEvent>
#include <QLabel>
#include <QShowEvent>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>
#include <QPointer>
#include <QPainter>
#include <algorithm>
#include <cmath>

Q_DECLARE_LOGGING_CATEGORY(render3dLog)

namespace darkeye::render3d {

namespace {
class StatisticsPanel final : public QFrame {
public:
    explicit StatisticsPanel(QWidget *parent)
        : QFrame(parent,Qt::Tool|Qt::FramelessWindowHint|Qt::WindowDoesNotAcceptFocus) {}
protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(QColor(QStringLiteral("#596273")));
        painter.setBrush(QColor(20,24,33,225));
        painter.drawRoundedRect(QRectF(rect()).adjusted(0.5,0.5,-0.5,-0.5),6,6);
    }
};
}

struct RhiSceneWidget::StatisticsOverlay {
    RhiSceneWidget *owner;
    QPointer<QFrame> panel;
    QPointer<QWidget> trackedWindow;
    QLabel *values;
    QTimer timer;
    QElapsedTimer clock;
    quint64 previousFrames = 0;
    quint64 previousUploadBytes = 0;

    explicit StatisticsOverlay(RhiSceneWidget *widget)
        : owner(widget), panel(new StatisticsPanel(widget)),
          values(new QLabel(panel))
    {
        // Use a non-activating overlay window: repainting text in the canvas's
        // backing store would submit extra QRhi frames and skew the FPS count.
        panel->setAttribute(Qt::WA_ShowWithoutActivating);
        panel->setAttribute(Qt::WA_TranslucentBackground);
        panel->setObjectName(QStringLiteral("RhiStatisticsPanel"));
        panel->setStyleSheet(QStringLiteral(
            "QFrame#RhiStatisticsPanel QLabel { color:#e6edf3;background:transparent;"
            " border:0;font-size:12px; }"
            "QFrame#RhiStatisticsPanel QToolButton { color:#cbd5e1;background:transparent;"
            " border:0;font-size:16px; }"
            "QFrame#RhiStatisticsPanel QToolButton:hover { background:#465166; }"));
        auto *layout = new QVBoxLayout(panel);
        layout->setContentsMargins(12,8,12,10);
        layout->setSpacing(6);
        auto *header = new QHBoxLayout;
        auto *title = new QLabel(QStringLiteral("渲染统计"),panel);
        title->setStyleSheet(QStringLiteral("font-weight:600;"));
        header->addWidget(title);
        header->addStretch();
        auto *close = new QToolButton(panel);
        close->setObjectName(QStringLiteral("RhiStatisticsClose"));
        close->setText(QStringLiteral("×"));
        close->setToolTip(QStringLiteral("关闭渲染统计"));
        close->setFixedSize(22,22);
        QObject::connect(close,&QToolButton::clicked,owner,[widget] { widget->setStatisticsVisible(false); });
        header->addWidget(close);
        layout->addLayout(header);
        values->setObjectName(QStringLiteral("RhiStatisticsValues"));
        values->setTextFormat(Qt::PlainText);
        values->setMinimumWidth(200);
        layout->addWidget(values);
        panel->move(12,12);
        panel->setToolTip(QStringLiteral(
            "FPS 按最近半秒的帧提交数统计；静止时按需渲染，可降至 0。\n"
            "CPU 为最近一帧的准备与命令录制耗时；纹理/目标为估算占用。"));
        timer.setInterval(500);
        QObject::connect(&timer,&QTimer::timeout,owner,[this] {
            const auto elapsed = clock.restart();
            const auto frames = owner->statistics().submittedFrames;
            const auto uploadBytes = owner->statistics().textureUploadBytes;
            update(elapsed>0 ? (frames-previousFrames)*1000.0/elapsed : 0,
                   elapsed>0 ? (uploadBytes-previousUploadBytes)*1000.0/elapsed/1048576.0 : 0);
            previousFrames = frames;
            previousUploadBytes = uploadBytes;
        });
        update(0);
    }

    ~StatisticsOverlay()
    {
        if(trackedWindow) trackedWindow->removeEventFilter(owner);
        delete panel.data();
    }

    void position()
    {
        auto *window=owner->window();
        if(trackedWindow!=window) {
            if(trackedWindow) trackedWindow->removeEventFilter(owner);
            trackedWindow=window;
            if(window!=owner) window->installEventFilter(owner);
        }
        panel->move(owner->mapToGlobal(QPoint(12,12)));
        const bool visible=owner->statisticsVisible() && owner->isVisible() && !window->isMinimized();
        panel->setVisible(visible);
        if(!visible) timer.stop();
        else if(!timer.isActive()) start();
    }

    void update(double fps,double uploadMiBPerSecond=0)
    {
        const auto &s = owner->statistics();
        const auto text = QStringLiteral(
            "FPS（提交）  %1\nCPU 帧耗时  %2 ms\n绘制调用  %3\n可见图元  %4\n"
            "透射遍数  %5\n纹理/目标  %6 MiB\n缓冲  %7 MiB\n纹理上传  %8 MiB/s\n%9 · %10×%11 · %12× MSAA")
            .arg(fps,0,'f',1).arg(s.cpuFrameMs,0,'f',2).arg(s.drawCalls).arg(s.visibleItems)
            .arg(s.transmissionPasses).arg(s.textureBytes/1048576.0,0,'f',1)
            .arg(s.bufferBytes/1048576.0,0,'f',1).arg(uploadMiBPerSecond,0,'f',1)
            .arg(s.backend.isEmpty()?QStringLiteral("初始化中"):s.backend)
            .arg(s.pixelSize.width()).arg(s.pixelSize.height()).arg(s.samples);
        if(values->text()!=text) {
            values->setText(text);
            panel->adjustSize();
        }
    }

    void start()
    {
        previousFrames = owner->statistics().submittedFrames;
        previousUploadBytes = owner->statistics().textureUploadBytes;
        clock.start();
        update(0);
        timer.start();
    }
};

RhiSceneWidget::RhiSceneWidget(QWidget *parent)
    : QRhiWidget(parent), m_scene(std::make_shared<Scene>()), m_renderer(std::make_unique<Renderer>())
{
#ifdef Q_OS_WIN
    setApi(Api::Direct3D11);
#endif
    setSampleCount(1);
    m_statisticsAction = new QAction(QStringLiteral("显示渲染统计"),this);
    m_statisticsAction->setObjectName(QStringLiteral("RhiToggleStatistics"));
    m_statisticsAction->setCheckable(true);
    connect(m_statisticsAction,&QAction::toggled,this,&RhiSceneWidget::setStatisticsVisible);
    addAction(m_statisticsAction);
    setContextMenuPolicy(Qt::ActionsContextMenu);
    connect(this, &QRhiWidget::frameSubmitted, this, [this] {
        ++m_statistics.submittedFrames;
    });
    connect(this, &QRhiWidget::renderFailed, this, [this] {
        reportError(QStringLiteral("QRhiWidget failed to initialize or submit a frame"));
    });
}

RhiSceneWidget::~RhiSceneWidget()
{
    if(m_statisticsOverlay) m_statisticsOverlay->timer.stop();
    // Renderer is destroyed before QRhiWidget releases its surface.
}

void RhiSceneWidget::setStatisticsVisible(bool visible)
{
    if(m_statisticsVisible==visible) return;
    m_statisticsVisible=visible;
    m_statisticsAction->setChecked(visible);
    if(visible && !m_statisticsOverlay)
        m_statisticsOverlay=std::make_unique<StatisticsOverlay>(this);
    if(m_statisticsOverlay) {
        if(visible) {
            m_statisticsOverlay->position();
        } else {
            m_statisticsOverlay->panel->hide();
            m_statisticsOverlay->timer.stop();
        }
    }
    emit statisticsVisibleChanged(visible);
}

void RhiSceneWidget::showEvent(QShowEvent *event)
{
    ++m_visibilityGeneration;
    QRhiWidget::showEvent(event);
    update(); // Rebuild GPU resources after an idle hidden page was trimmed.
    if(m_statisticsVisible && m_statisticsOverlay) {
        m_statisticsOverlay->position();
    }
}

void RhiSceneWidget::hideEvent(QHideEvent *event)
{
    if(m_statisticsOverlay) {
        m_statisticsOverlay->timer.stop();
        m_statisticsOverlay->panel->hide();
    }
    QRhiWidget::hideEvent(event);
    // Keep short page switches cheap, but do not retain a hidden scene's
    // full-size HDR/MSAA/transmission targets indefinitely. CPU scene state
    // survives; prepare() recreates resources on the next visible frame.
    const auto generation = ++m_visibilityGeneration;
    QTimer::singleShot(1000, this, [this, generation] {
        if (generation == m_visibilityGeneration && !isVisible())
            releaseResources();
    });
}

bool RhiSceneWidget::event(QEvent *event)
{
    const bool handled=QRhiWidget::event(event);
    if(m_statisticsOverlay && (event->type()==QEvent::Move || event->type()==QEvent::Resize
                              || event->type()==QEvent::ParentChange))
        m_statisticsOverlay->position();
    return handled;
}

bool RhiSceneWidget::eventFilter(QObject *object,QEvent *event)
{
    if(m_statisticsOverlay && object==m_statisticsOverlay->trackedWindow
       && (event->type()==QEvent::Move || event->type()==QEvent::Resize
           || event->type()==QEvent::WindowStateChange))
        m_statisticsOverlay->position();
    return QRhiWidget::eventFilter(object,event);
}

bool RhiSceneWidget::setMesh(MeshData mesh)
{
    auto scene = std::make_shared<Scene>();
    if (!mesh.empty()) {
        const auto node = scene->createNode();
        auto material = std::make_shared<Material>();
        material->doubleSided = true;
        QString error;
        if (!scene->setMesh(node, std::make_shared<const MeshData>(std::move(mesh)), material, &error)) {
            reportError(error);
            return false;
        }
    }
    setScene(std::move(scene));
    return true;
}

void RhiSceneWidget::setScene(std::shared_ptr<Scene> scene)
{
    m_scene = scene ? std::move(scene) : std::make_shared<Scene>();
    update();
}

bool RhiSceneWidget::setCamera(const Camera &camera)
{
    QString error;
    if (!camera.matrices(1.0f, &error)) {
        reportError(error);
        return false;
    }
    m_camera = camera;
    update();
    return true;
}

void RhiSceneWidget::setRotation(const QQuaternion &rotation)
{
    if (rotation.isNull() || !std::isfinite(rotation.lengthSquared()))
        return;
    const auto normalized = rotation.normalized();
    if (m_rotation == normalized)
        return;
    m_rotation = normalized;
    update();
}

bool RhiSceneWidget::setCameraDistance(float distance)
{
    if (std::isfinite(distance))
        distance = std::clamp(distance, 3.5f, 30.0f);
    if (m_camera.distance == distance)
        return true;
    auto camera = m_camera;
    camera.distance = distance;
    return setCamera(camera);
}

void RhiSceneWidget::setClearColor(const QColor &color)
{
    if (!color.isValid() || color == m_clearColor)
        return;
    m_clearColor = color;
    update();
}

void RhiSceneWidget::setTextureBudget(quint64 bytes)
{
    m_renderer->setTextureBudget(bytes);
    update();
}

const RenderStatistics &RhiSceneWidget::statistics() const { return m_statistics; }
void RhiSceneWidget::setEnvironment(std::shared_ptr<const EnvironmentMap> environment)
{
    m_renderer->setEnvironment(std::move(environment)); update();
}
void RhiSceneWidget::setLighting(const LightingSettings &settings)
{
    auto valid=settings;
    const auto clamp=[](float value,float low,float high,float fallback) {
        return std::isfinite(value)?std::clamp(value,low,high):fallback;
    };
    valid.exposure=clamp(valid.exposure,0.05f,8.0f,1);
    valid.environmentIntensity=clamp(valid.environmentIntensity,0,8,1);
    valid.environmentRotation=clamp(valid.environmentRotation,-360,360,0);
    valid.keyLightIntensity=clamp(valid.keyLightIntensity,0,8,2.4f);
    if(!std::isfinite(valid.keyLightDirection.lengthSquared()) || valid.keyLightDirection.lengthSquared()<1e-8f)
        valid.keyLightDirection={0.5f,1,0.6f};
    valid.keyLightDirection.normalize();
    valid.aoRadius=clamp(valid.aoRadius,0.0001f,10,0.025f);
    valid.aoStrength=clamp(valid.aoStrength,0,3,1);
    valid.transmissionPassLimit=std::clamp(valid.transmissionPassLimit,1,16);
    m_renderer->setLighting(valid); update();
}
LightingSettings RhiSceneWidget::lighting() const { return m_renderer->lighting(); }
QString RhiSceneWidget::lastError() const { return m_lastError; }

void RhiSceneWidget::reportError(const QString &message)
{
    if (m_lastError == message)
        return;
    m_lastError = message;
    qCCritical(render3dLog).noquote() << message;
    emit renderingError(message);
}

void RhiSceneWidget::render(QRhiCommandBuffer *cb)
{
    auto *target = renderTarget();
    if (!target || target->pixelSize().isEmpty())
        return;
    const auto size = target->pixelSize();
    QElapsedTimer frameTimer;
    frameTimer.start();
    if (m_statistics.pixelSize != size) {
        qCInfo(render3dLog) << "Surface size" << size << "DPR" << devicePixelRatioF();
        m_statistics.pixelSize = size;
    }
    m_statistics.samples = target->sampleCount();
    QString error;
    if (!m_renderer->prepare(rhi(), target, m_scene->renderItems(), cb, m_statistics, error)
        || !m_renderer->draw(target, cb, m_camera, m_rotation, m_clearColor, m_statistics, error)) {
        cb->beginPass(target, QColor(Qt::darkRed), {1.0f, 0});
        cb->endPass();
        reportError(error);
        return;
    }
    m_statistics.cpuFrameMs = double(frameTimer.nsecsElapsed()) / 1e6;
    if (!m_lastError.isEmpty()) {
        m_lastError.clear();
        emit renderingRecovered();
    }
    // Requests come from CPU state changes or the demo's animation timer.
}

void RhiSceneWidget::releaseResources()
{
    m_statistics.transmissionPasses=0;
    m_statistics.transmissionFallbackItems=0;
    qCInfo(render3dLog) << "Release GPU resources" << this;
    m_renderer->release();
    m_statistics.bufferBytes = 0;
    m_statistics.textureBytes = 0;
    m_statistics.sourceTextureBytes = 0;
    m_statistics.geometryCount = 0;
    m_statistics.textureCount = 0;
}

} // namespace darkeye::render3d
