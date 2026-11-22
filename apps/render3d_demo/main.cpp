#include "render3d/RhiSceneWidget.h"
#include "render3d/GltfLoader.h"
#include "dvd_scene/DvdModelAsset.h"

#include <QApplication>
#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QCommandLineParser>
#include <QElapsedTimer>
#include <QHBoxLayout>
#include <QLabel>
#include <QMainWindow>
#include <QPainter>
#include <QPushButton>
#include <QSlider>
#include <QTimer>
#include <QVBoxLayout>
#include <cmath>
#include <algorithm>

using namespace darkeye::render3d;

class DemoWindow final : public QMainWindow {
public:
    DemoWindow()
    {
        setWindowTitle(QStringLiteral("DarkEye · QRhi 3D Demo (P1)"));
        resize(1100, 720);
        auto *central = new QWidget(this);
        auto *row = new QHBoxLayout(central);
        m_canvasHost = new QWidget(central);
        m_canvasLayout = new QVBoxLayout(m_canvasHost);
        m_canvasLayout->setContentsMargins(0, 0, 0, 0);
        row->addWidget(m_canvasHost, 1);
        auto *panel = new QWidget(central);
        panel->setFixedWidth(250);
        auto *controls = new QVBoxLayout(panel);
        auto *heading = new QLabel(QStringLiteral("QRhi 3D · 独立验证"), panel);
        heading->setStyleSheet(QStringLiteral("font-size: 20px; font-weight: 600;"));
        controls->addWidget(heading);
        auto *description = new QLabel(QStringLiteral(
            "P1：网格、深度与资源生命周期\nWindows / Direct3D 11"), panel);
        description->setWordWrap(true);
        controls->addWidget(description);
        m_scene = new QComboBox(panel);
        m_scene->addItems({QStringLiteral("彩色立方体"), QStringLiteral("彩色三角形"),
                           QStringLiteral("空场景 / 清屏"), QStringLiteral("DVD GLB（原模型）")});
        controls->addWidget(m_scene);
        m_animate = new QCheckBox(QStringLiteral("自动旋转"), panel);
        m_animate->setChecked(true);
        controls->addWidget(m_animate);
        controls->addWidget(new QLabel(QStringLiteral("旋转角度"), panel));
        m_angle = new QSlider(Qt::Horizontal, panel);
        m_angle->setRange(0, 359);
        m_angle->setValue(30);
        controls->addWidget(m_angle);
        controls->addWidget(new QLabel(QStringLiteral("镜头距离"), panel));
        m_distance = new QSlider(Qt::Horizontal, panel);
        m_distance->setRange(35, 140);
        m_distance->setValue(60);
        controls->addWidget(m_distance);
        m_samples = new QComboBox(panel);
        m_samples->addItems({QStringLiteral("MSAA ×1"), QStringLiteral("MSAA ×4")});
        controls->addWidget(m_samples);
        auto *reset = new QPushButton(QStringLiteral("重置视角"), panel);
        auto *hide = new QPushButton(QStringLiteral("隐藏后恢复（0.8 秒）"), panel);
        auto *reparent = new QPushButton(QStringLiteral("移到另一窗口 / 移回"), panel);
        auto *recreate = new QPushButton(QStringLiteral("销毁并重建 Widget"), panel);
        controls->addWidget(reset);
        controls->addWidget(hide);
        controls->addWidget(reparent);
        controls->addWidget(recreate);
        m_info = new QLabel(panel);
        m_info->setWordWrap(true);
        m_info->setTextInteractionFlags(Qt::TextSelectableByMouse);
        controls->addWidget(m_info);
        m_error = new QLabel(panel);
        m_error->setWordWrap(true);
        m_error->setStyleSheet(QStringLiteral("color: #e75d68"));
        controls->addWidget(m_error);
        controls->addStretch();
        controls->addWidget(new QLabel(QStringLiteral("暂停后按需渲染\n诊断信息输出到终端"), panel));
        row->addWidget(panel);
        setCentralWidget(central);

        m_other.setWindowTitle(QStringLiteral("QRhi 3D · 第二个顶层窗口"));
        m_other.resize(720, 600);
        m_other.installEventFilter(this);
        m_otherLayout = new QVBoxLayout(&m_other);
        m_timer.setInterval(16);
        m_timer.setTimerType(Qt::PreciseTimer);
        connect(&m_timer, &QTimer::timeout, this, [this] {
            m_degrees = std::fmod(m_degrees + float(m_clock.restart()) * 0.035f, 360.0f);
            updateRotation();
        });
        connect(m_animate, &QCheckBox::toggled, this, [this] { updateAnimation(); });
        connect(m_scene, &QComboBox::currentIndexChanged, this, [this] {
            applyScene();
            updateAnimation();
        });
        connect(m_angle, &QSlider::valueChanged, this, [this](int value) {
            m_degrees = float(value);
            updateRotation();
        });
        connect(m_distance, &QSlider::valueChanged, this, [this](int value) {
            m_view->setCameraDistance(float(value) / 10.0f);
        });
        connect(m_samples, &QComboBox::currentIndexChanged, this, [this](int index) {
            m_view->setSampleCount(index == 0 ? 1 : 4);
            m_view->update();
        });
        connect(reset, &QPushButton::clicked, this, [this] {
            m_degrees = 30;
            m_angle->setValue(30);
            m_distance->setValue(60);
            updateRotation();
        });
        connect(hide, &QPushButton::clicked, this, [this] {
            m_view->hide();
            QTimer::singleShot(800, this, [this] { m_view->show(); });
        });
        connect(reparent, &QPushButton::clicked, this, [this] { moveCanvas(); });
        connect(recreate, &QPushButton::clicked, this, [this] {
            m_timer.stop();
            delete m_view;
            createCanvas();
        });
        createCanvas();
    }

    RhiSceneWidget *view() const { return m_view; }
    bool selectScene(const QString &name)
    {
        const auto index = QStringList{"cube", "triangle", "empty", "dvd"}.indexOf(name);
        if (index < 0) return false;
        m_scene->setCurrentIndex(int(index));
        return true;
    }

protected:
    void closeEvent(QCloseEvent *event) override
    {
        m_timer.stop();
        m_other.hide();
        QMainWindow::closeEvent(event);
    }

    void changeEvent(QEvent *event) override
    {
        QMainWindow::changeEvent(event);
        if (event->type() == QEvent::WindowStateChange)
            updateAnimation();
    }

    bool eventFilter(QObject *object, QEvent *event) override
    {
        if (object == &m_other && event->type() == QEvent::Close) {
            if (m_view->parentWidget() == &m_other)
                moveCanvas();
            event->ignore();
            return true;
        }
        if (object == m_view && (event->type() == QEvent::Show || event->type() == QEvent::Hide))
            QTimer::singleShot(0, this, [this] { updateAnimation(); });
        if (object == &m_other && event->type() == QEvent::WindowStateChange)
            QTimer::singleShot(0, this, [this] { updateAnimation(); });
        return QMainWindow::eventFilter(object, event);
    }

private:
    void createCanvas()
    {
        m_view = new RhiSceneWidget(m_canvasHost);
        m_view->setMinimumSize(160, 160);
        m_view->installEventFilter(this);
        m_canvasLayout->addWidget(m_view);
        connect(m_view, &RhiSceneWidget::renderingError, m_error, &QLabel::setText);
        connect(m_view, &QRhiWidget::frameSubmitted, this, [this] {
            // Updating text can repaint the whole window. Do it only when the
            // diagnostics actually change, so a paused scene remains idle.
            const auto &s = m_view->statistics();
            const QString text = QStringLiteral(
                "%1\n%2 × %3 px · DPR %4 · MSAA ×%5\n"
                "GPU 资源代次：%6\n管线代次：%7\n几何上传：%8\nBuffer：%9 B\n"
                "共享几何：%10 · Draw：%11\n纹理/目标：%12 KiB")
                .arg(s.backend).arg(s.pixelSize.width()).arg(s.pixelSize.height())
                .arg(m_view->devicePixelRatioF(), 0, 'f', 2).arg(s.samples)
                .arg(s.resourceGenerations).arg(s.pipelineGenerations)
                .arg(s.geometryUploads).arg(s.bufferBytes)
                .arg(s.geometryCount).arg(s.drawCalls).arg(s.textureBytes / 1024);
            if (m_info->text() != text)
                m_info->setText(text);
        });
        m_error->clear();
        m_other.hide();
        applyScene();
        updateRotation();
        m_view->setCameraDistance(float(m_distance->value()) / 10.0f);
        m_view->setSampleCount(m_samples->currentIndex() == 0 ? 1 : 4);
        m_view->show();
        updateAnimation();
    }

    void applyScene()
    {
        switch (m_scene->currentIndex()) {
        case 0: m_view->setMesh(coloredCube()); break;
        case 1: m_view->setMesh(coloredTriangle()); break;
        case 3: showDvdAsset(); break;
        default: m_view->setMesh({}); break;
        }
    }

    void showDvdAsset()
    {
        const auto loaded = darkeye::dvd_scene::loadDvdModel(m_loader, QStringLiteral(":/dvd_demo/dvd.glb"));
        if (!loaded) {
            m_error->setText(loaded.error);
            return;
        }
        auto scene = std::make_shared<Scene>();
        const auto group = scene->createNode();
        const auto nodes = loaded.asset->instantiate(*scene, group);
        auto texture = std::make_shared<TextureData>();
        texture->source = QStringLiteral("generated DVD cover");
        texture->image = QImage(512, 320, QImage::Format_RGBA8888);
        texture->image.fill(QColor("#246885"));
        {
            QPainter painter(&texture->image);
            painter.fillRect(246, 0, 20, 320, QColor("#17202f"));
            painter.setPen(Qt::white);
            painter.setFont(QFont("Arial", 22, QFont::Bold));
            painter.drawText(QRect(0, 0, 245, 320), Qt::AlignCenter, QStringLiteral("DARKEYE\nBACK\nTOP ↑"));
            painter.drawText(QRect(267, 0, 245, 320), Qt::AlignCenter, QStringLiteral("QRhi 3D\nFRONT\nTOP ↑"));
        }
        for (const auto &item : scene->renderItems()) {
            if (item.material && item.material->name == "cover") {
                auto material = std::make_shared<Material>(*item.material);
                material->baseColorTexture = texture;
                scene->setMesh(item.node, item.mesh, material);
            }
        }
        for (std::size_t i = 0; i < nodes.size(); ++i)
            if (loaded.asset->nodes[i].name == "disc") scene->setVisible(nodes[i], false);
        Transform transform;
        transform.rotation = QQuaternion::fromAxisAndAngle(0, 1, 0, -90);
        transform.scale = {10, 10, 10};
        scene->setTransform(group, transform);
        QVector3D lower(1e10f, 1e10f, 1e10f), upper(-1e10f, -1e10f, -1e10f);
        for (const auto &item : scene->renderItems())
            for (const auto &vertex : item.mesh->vertices) {
                const auto &p = vertex.position;
                const auto world = item.world.map(QVector3D(p[0], p[1], p[2]));
                for (int axis = 0; axis < 3; ++axis) {
                    lower[axis] = std::min(lower[axis], world[axis]);
                    upper[axis] = std::max(upper[axis], world[axis]);
                }
            }
        transform.translation = -(lower + upper) * 0.5f;
        scene->setTransform(group, transform);
        m_view->setScene(std::move(scene));
    }

    void updateRotation()
    {
        m_view->setRotation(QQuaternion::fromEulerAngles(22.0f, m_degrees, 0.0f));
    }

    void updateAnimation()
    {
        const bool running = m_animate->isChecked() && m_scene->currentIndex() != 2
            && m_view->isVisible() && !m_view->window()->isMinimized();
        if (running && !m_timer.isActive()) {
            m_clock.start();
            m_timer.start();
        } else if (!running) {
            m_timer.stop();
        }
    }

    void moveCanvas()
    {
        m_timer.stop();
        const bool toOther = m_view->parentWidget() != &m_other;
        auto *destination = toOther ? m_otherLayout : m_canvasLayout;
        m_view->setParent(toOther ? &m_other : m_canvasHost);
        destination->addWidget(m_view);
        m_other.setVisible(toOther);
        m_view->show();
        updateAnimation();
    }

    QWidget m_other;
    QWidget *m_canvasHost = nullptr;
    QVBoxLayout *m_canvasLayout = nullptr;
    QVBoxLayout *m_otherLayout = nullptr;
    RhiSceneWidget *m_view = nullptr;
    QComboBox *m_scene = nullptr;
    QComboBox *m_samples = nullptr;
    QCheckBox *m_animate = nullptr;
    QSlider *m_angle = nullptr;
    QSlider *m_distance = nullptr;
    QLabel *m_info = nullptr;
    QLabel *m_error = nullptr;
    QTimer m_timer;
    QElapsedTimer m_clock;
    float m_degrees = 30.0f;
    GltfLoader m_loader;
};

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("darkeye_render3d_demo"));
    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Standalone P1 QRhi 3D demo; no application data."));
    parser.addHelpOption();
    parser.addOption({QStringLiteral("smoke-test"), QStringLiteral("Read back a rendered cube and exit.")});
    parser.addOption({QStringLiteral("screenshot"), QStringLiteral("Save the framebuffer as PNG and exit."),
                      QStringLiteral("path")});
    parser.addOption({QStringLiteral("scene"), QStringLiteral("Scene: cube, triangle, empty, dvd."),
                      QStringLiteral("name"), QStringLiteral("cube")});
    parser.process(app);
    DemoWindow window;
    if (!window.selectScene(parser.value(QStringLiteral("scene")))) {
        qCritical("Unknown scene");
        return 2;
    }
    const bool automated = parser.isSet(QStringLiteral("smoke-test"))
        || parser.isSet(QStringLiteral("screenshot"));
    if (automated) {
        QObject::connect(window.view(), &RhiSceneWidget::renderingError, &app,
                         [&app](const QString &) { app.exit(2); }, Qt::QueuedConnection);
        QTimer::singleShot(700, &app, [&] {
            const auto image = window.view()->grabFramebuffer();
            const auto &stats = window.view()->statistics();
            if (image.isNull() || !stats.submittedFrames || !stats.bufferBytes
                || !window.view()->lastError().isEmpty()
                || image.pixelColor(image.width() / 2, image.height() / 2)
                    == image.pixelColor(0, 0)) {
                qCritical("QRhi demo smoke test failed: missing rendered geometry");
                app.exit(2);
                return;
            }
            const auto path = parser.value(QStringLiteral("screenshot"));
            if (parser.isSet(QStringLiteral("screenshot")) && !image.save(path, "PNG")) {
                qCritical("Cannot save screenshot: %s", qPrintable(path));
                app.exit(3);
                return;
            }
            qInfo("QRhi demo smoke test passed: %s, %dx%d, frames=%llu",
                  qPrintable(stats.backend), image.width(), image.height(), stats.submittedFrames);
            app.exit(0);
        });
        QTimer::singleShot(10000, &app, [&app] {
            qCritical("QRhi demo smoke test timed out");
            app.exit(4);
        });
    }
    window.show();
    return app.exec();
}
