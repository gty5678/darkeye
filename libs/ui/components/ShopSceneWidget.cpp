#include "ShopSceneWidget.h"
#include "render3d/GltfLoader.h"
#include "render3d/Raycaster.h"
#include <QMouseEvent>
#include <QContextMenuEvent>
#include <QPainter>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>
#include <functional>

namespace darkeye {
using namespace render3d;
namespace {
class Outline final : public QWidget {
public:
    explicit Outline(QWidget *parent) : QWidget(parent) {
        setAttribute(Qt::WA_TransparentForMouseEvents);
        setAttribute(Qt::WA_NoSystemBackground);
    }
    QPolygonF points;
    QColor color;
    void paintEvent(QPaintEvent *) override {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(QPen(color, 2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        painter.drawPolygon(points);
    }
};
QPolygonF hull(QList<QPointF> points) {
    std::sort(points.begin(), points.end(), [](auto a, auto b) {
        return a.x() == b.x() ? a.y() < b.y() : a.x() < b.x();
    });
    auto cross = [](auto a, auto b, auto c) {
        return (b.x()-a.x())*(c.y()-a.y())-(b.y()-a.y())*(c.x()-a.x());
    };
    QPolygonF result;
    for (auto p : points) {
        while (result.size() >= 2 && cross(result[result.size()-2], result.last(), p) <= 0)
            result.removeLast();
        result << p;
    }
    const auto lower = result.size();
    for (auto it = points.crbegin(); it != points.crend(); ++it) {
        while (result.size() > lower && cross(result[result.size()-2], result.last(), *it) <= 0)
            result.removeLast();
        result << *it;
    }
    if (!result.isEmpty()) result.removeLast();
    return result;
}
}

ShopSceneWidget::ShopSceneWidget(QWidget *parent)
    : QWidget(parent), m_canvas(new RhiSceneWidget(this)), m_outline(new Outline(m_canvas)),
      m_scene(std::make_shared<Scene>())
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0,0,0,0);
    layout->addWidget(m_canvas);
    m_canvas->setObjectName(QStringLiteral("HomeShopCanvas"));
    m_canvas->setSampleCount(2);
    m_canvas->setClearColor(QColor(QStringLiteral("#18181c")));
    constexpr quint64 coverBudget = 64ULL*1024*1024;
    m_canvas->setTextureBudget(coverBudget);
    m_covers.setBudget(coverBudget);
    m_canvas->setScene(m_scene);
    m_canvas->setMouseTracking(true);
    m_canvas->installEventFilter(this);
    connect(m_canvas, &RhiSceneWidget::renderingError, this, &ShopSceneWidget::sceneError);
    connect(&m_covers, &dvd_scene::CoverTextureCache::textureReady, this,
            [this](const QString &key) {
        updateTextures();
        const auto error = m_covers.error(key);
        if (!error.isEmpty()) emit sceneError(error);
    });
    GltfLoader loader;
    const auto loaded = loader.load(QStringLiteral(":/assets/shop/darkeye-shop.glb"));
    if (!loaded) {
        // Expose initialization failures even before a caller can connect signals.
        m_canvas->setProperty("modelError", loaded.error);
        return;
    }
    ModelNode cover;
    for (const auto &node : loaded.asset->nodes) {
        if (node.name == QStringLiteral("dvd")) { cover = node; continue; }
        if (node.name == QStringLiteral("Cube.004")) {
            ModelAsset back;
            back.nodes.push_back(node);
            auto &panel = back.nodes.back();
            panel.transform.translation.setY(0.51f);
            (void)back.instantiate(*m_scene);
        }
        if (node.name == QStringLiteral("Cube.002")) {
            for (int row=0; row<3; ++row) {
                ModelAsset shelf;
                shelf.nodes.push_back(node);
                shelf.nodes.back().transform.translation.setY(1.01f-row*0.25f);
                (void)shelf.instantiate(*m_scene);
            }
        }
    }
    for (int i=0; i<18; ++i) {
        const auto root = m_scene->createNode(0, QStringLiteral("HomeCover%1").arg(i));
        auto pose = cover.transform;
        pose.translation = {-0.394006f+(i%6)*0.16f, 1.115f-(i/6)*0.25f, -0.043f};
        m_scene->setTransform(root, pose);
        m_roots.append(root);
        for (const auto &primitive : cover.primitives) {
            const auto part = m_scene->createNode(root);
            m_scene->setMesh(part, primitive.mesh, primitive.material);
            m_indices.insert(part, i);
        }
        m_scene->setVisible(root, false);
    }
    LightingSettings lighting;
    lighting.environmentBackground = true;
    lighting.imageBasedLighting = true;
    lighting.toneMapping = true;
    lighting.environmentRotation = 155;
    lighting.keyLightIntensity = 1.8f;
    lighting.keyLightDirection = {-0.5f,0.8f,1};
    m_canvas->setLighting(lighting);
    m_canvas->setEnvironment(EnvironmentMap::loadHdr(QStringLiteral(":/assets/hdr/lebombo_1k.hdr")));
    m_camera.target = {0,0.865f,0};
    m_camera.rotationMode = Camera::RotationMode::InPlace;
    m_camera.verticalFov = 50;
    m_camera.nearPlane = 0.06f;
    m_camera.farPlane = 400;
    updateCamera();
}

void ShopSceneWidget::setCoverPaths(const QStringList &paths) {
    m_paths = paths.mid(0,18);
    m_coverPixels = 0;
    for (int i=0; i<m_roots.size(); ++i) m_scene->setVisible(m_roots[i], i<m_paths.size());
    requestCovers();
    updateHover(m_lastPosition);
}
void ShopSceneWidget::requestCovers() {
    // Match the small home covers to their physical screen size. Bucket requests
    // to avoid decoding again for every resize, without supersampling them.
    const float projectedHeight = m_canvas->height()*float(m_canvas->devicePixelRatioF())*0.19f
        /(2*std::max(0.06f,m_camera.distance)*std::tan(m_camera.verticalFov*3.14159265f/360));
    const int pixels = std::clamp(int(std::ceil(projectedHeight/256))*256,256,4096);
    if (pixels == m_coverPixels) return;
    m_coverPixels = pixels;
    QStringList keys;
    for (const auto &path : m_paths) keys << m_covers.request(path,pixels).key;
    const bool samePaths = m_keys.size()==keys.size() && !m_keys.isEmpty();
    m_keys = keys;
    m_covers.retainRequests(QSet<QString>(keys.cbegin(), keys.cend()));
    updateTextures(samePaths);
}
void ShopSceneWidget::updateTextures(bool keepExisting) {
    for (auto it=m_indices.cbegin(); it!=m_indices.cend(); ++it) {
        if (it.value()>=m_keys.size()) continue;
        const auto *node = m_scene->node(it.key());
        if (!node || !node->material || node->material->name != QStringLiteral("pic")) continue;
        auto texture = m_covers.find(m_keys[it.value()]);
        if (!texture && keepExisting && node->material->baseColorTexture
            && node->material->baseColorTexture->source == m_paths[it.value()]) continue;
        if (!texture) texture = m_covers.placeholder();
        if (node->material->baseColorTexture == texture) continue;
        auto material = std::make_shared<Material>(*node->material);
        material->baseColorTexture = std::move(texture);
        material->roughness = 0.9f;
        m_scene->setMesh(it.key(), node->mesh, material);
    }
    m_canvas->update();
}
void ShopSceneWidget::setOutlineColor(const QColor &color) {
    m_outlineColor = color;
    static_cast<Outline *>(m_outline)->color = color;
    m_outline->update();
}
void ShopSceneWidget::updateCamera() {
    const float aspect = float(std::max(1,m_canvas->width()))/std::max(1,m_canvas->height());
    m_camera.distance = std::max(0.86f, 0.52f/(std::tan(25.0f*3.14159265f/180)*aspect));
    m_canvas->setCamera(m_camera);
    requestCovers();
    m_outline->setGeometry(m_canvas->rect());
    updateHover(m_lastPosition);
}
int ShopSceneWidget::coverAt(const QPointF &position) const {
    const auto ray = m_camera.ray(position, m_canvas->size());
    const auto hit = ray ? raycast(*m_scene,*ray,RaycastMode::Triangles) : std::nullopt;
    return hit ? m_indices.value(hit->node,-1) : -1;
}
void ShopSceneWidget::updateHover(const QPointF &position) {
    m_hovered = m_rotating ? -1 : coverAt(position);
    m_canvas->setCursor(m_hovered>=0 ? Qt::PointingHandCursor : Qt::ArrowCursor);
    auto *outline = static_cast<Outline *>(m_outline);
    QList<QPointF> points;
    if (m_hovered>=0) {
        for (const auto &item : m_scene->renderItems()) {
            if (m_indices.value(item.node,-1)!=m_hovered) continue;
            for (const auto &vertex : item.mesh->vertices) {
                const auto p = m_camera.project(item.world.map(QVector3D(vertex.position[0],vertex.position[1],vertex.position[2])),m_canvas->size());
                if (p) points << *p;
            }
        }
    }
    outline->points = hull(points);
    outline->setVisible(!points.isEmpty());
    outline->raise();
    outline->update();
}
bool ShopSceneWidget::eventFilter(QObject *watched, QEvent *event) {
    if (watched!=m_canvas) return QWidget::eventFilter(watched,event);
    if (event->type()==QEvent::Resize) updateCamera();
    if (event->type()==QEvent::DevicePixelRatioChange) requestCovers();
    if (event->type()==QEvent::Leave) { m_outline->hide(); m_canvas->unsetCursor(); }
    // Windows delivers the context-menu event after release, when m_rotating
    // is already false. Keep the gesture result until the next right press.
    if (event->type()==QEvent::ContextMenu
        && static_cast<QContextMenuEvent *>(event)->reason()==QContextMenuEvent::Mouse
        && (m_rotating || m_rightDragged)) {
        event->accept();
        return true;
    }
    if (event->type()==QEvent::MouseButtonPress) {
        auto *mouse = static_cast<QMouseEvent *>(event);
        m_lastPosition = mouse->position();
        m_rotating = mouse->button()==Qt::RightButton;
        if (m_rotating) m_rightDragged = false;
        if (m_rotating) updateHover(m_lastPosition);
    }
    if (event->type()==QEvent::MouseMove) {
        auto *mouse = static_cast<QMouseEvent *>(event);
        if (m_rotating) {
            const auto delta = mouse->position()-m_lastPosition;
            if (!delta.isNull()) m_rightDragged = true;
            m_camera.yaw -= float(delta.x())*0.12f;
            m_camera.pitch = std::clamp(m_camera.pitch-float(delta.y())*0.12f,-89.0f,89.0f);
            m_canvas->setCamera(m_camera);
        }
        m_lastPosition = mouse->position();
        updateHover(m_lastPosition);
    }
    if (event->type()==QEvent::MouseButtonRelease) {
        auto *mouse = static_cast<QMouseEvent *>(event);
        if (mouse->button()==Qt::RightButton && m_rotating
            && mouse->position()!=m_lastPosition) m_rightDragged = true;
        m_rotating = false;
        updateHover(mouse->position());
        if (mouse->button()==Qt::LeftButton && m_hovered>=0) emit coverClicked(m_hovered);
    }
    return QWidget::eventFilter(watched,event);
}
} // namespace darkeye
