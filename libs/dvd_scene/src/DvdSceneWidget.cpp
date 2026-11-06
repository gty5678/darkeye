#include "dvd_scene/DvdSceneWidget.h"
#include "dvd_scene/CoverTextureCache.h"
#include "dvd_scene/DvdShelfController.h"
#include "render3d/Raycaster.h"
#include <QElapsedTimer>
#include <QEnterEvent>
#include <QHideEvent>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPointer>
#include <QSet>
#include <QShowEvent>
#include <QTimer>
#include <QVBoxLayout>
#include <QWheelEvent>
#include <algorithm>
#include <cmath>

namespace darkeye::dvd_scene {
using namespace render3d;
struct DvdSceneWidget::Impl {
    DvdSceneWidget *owner;
    RhiSceneWidget *canvas;
    DvdShelfController shelf;
    std::shared_ptr<Scene> scene=std::make_shared<Scene>();
    std::map<QString,DvdCaseInstance> cases;
    DvdInteractionController interaction{*scene,cases};
    std::unique_ptr<DvdCaseFactory> factory;
    CoverTextureCache covers;
    QHash<NodeId,QString> primitiveIds;
    QHash<QString,QString> coverKeys;
    QHash<QString,QString> coverSpecs;
    QPointer<QWidget> trackedWindow;
    Camera camera;
    float cameraTargetX=0;
    QTimer timer;
    QElapsedTimer clock;
    double lastTick=0;
    QPointF pressPosition,lastPosition;
    QPointF hoverPosition;
    bool hoverPositionValid=false;
    Qt::MouseButton dragButton=Qt::NoButton;
    bool moved=false;
    QString lastSelection,lastState=QStringLiteral("Idle");
    bool lastExpanded=false;
    float lastCameraX=0,lastCameraTargetX=0;

    explicit Impl(DvdSceneWidget *widget):owner(widget),canvas(new RhiSceneWidget(widget))
    {
        clock.start();
        camera.distance=0.25f;
        camera.rotationMode=Camera::RotationMode::InPlace;
        camera.target={0,0.1f,0};
        camera.verticalFov=60;
        camera.nearPlane=0.001f;
        camera.farPlane=100;
        canvas->setScene(scene);
        canvas->setCamera(camera);
        canvas->setMouseTracking(true);
        canvas->setFocusPolicy(Qt::StrongFocus);
        canvas->installEventFilter(owner);
        auto *layout=new QVBoxLayout(owner);
        layout->setContentsMargins(0,0,0,0);
        layout->addWidget(canvas);
        timer.setInterval(16);
        timer.setSingleShot(true);
        timer.setTimerType(Qt::PreciseTimer);
        QObject::connect(&timer,&QTimer::timeout,owner,[this] { tick(); });
        QObject::connect(canvas,&QRhiWidget::frameSubmitted,owner,[this] {
            // Advance after the previous submission instead of competing with
            // it on an independent 16ms clock. Keep the timer as a fallback
            // for interaction states that do not produce a new frame.
            if(needsAnimationTick()) timer.start(0);
        });
        QObject::connect(canvas,&RhiSceneWidget::renderingError,owner,&DvdSceneWidget::sceneError);
        QObject::connect(&covers,&CoverTextureCache::textureReady,owner,[this](const QString &key) {
            const auto texture=covers.find(key);
            if(!texture || !factory) return;
            bool changed=false;
            for(const auto &[id,instance]:cases)
                if(coverKeys.value(id)==key) {
                    factory->setCover(*scene,instance,texture);
                    changed=true;
                }
            const auto warning=covers.error(key);
            if(!warning.isEmpty()) emit owner->coverWarning(warning);
            if(changed) canvas->update();
        });
    }

    double now() const { return double(clock.nsecsElapsed())/1e9; }
    void updateFocusDistance()
    {
        // Leave room for both halves of an open case in narrow page layouts.
        const float aspect=float(std::max(1,canvas->width()))/std::max(1,canvas->height());
        const float halfFov=camera.verticalFov*3.14159265f/360.0f;
        interaction.setFocusDistance(std::max(0.2f,0.15f/(std::tan(halfFov)*aspect)));
    }
    bool needsAnimationTick() const
    {
        const bool moving=std::abs(camera.target.x()-cameraTargetX)>1e-5f;
        return owner->isVisible() && !owner->window()->isMinimized()
            && (interaction.active() || moving || std::any_of(cases.begin(),cases.end(),
                [](const auto &entry) { return entry.second.hasLocalVideo; }));
    }
    void updateTimer()
    {
        const bool active=needsAnimationTick();
        if(active && !timer.isActive()) {
            if(lastTick<=0) lastTick=now();
            timer.start(16);
        }
        else if(!active) timer.stop();
        if(!active) lastTick=0;
    }

    void notify()
    {
        const auto selected=interaction.selectedId();
        const auto state=DvdInteractionController::stateName(interaction.state());
        if(selected!=lastSelection) {
            lastSelection=selected;
            emit owner->selectionChanged(selected);
        }
        if(state!=lastState) {
            lastState=state;
            emit owner->stateChanged(state);
        }
        const bool expanded=owner->isExpanded();
        if(expanded!=lastExpanded) {
            lastExpanded=expanded;
            emit owner->expandedChanged(expanded);
        }
        if(camera.target.x()!=lastCameraX || cameraTargetX!=lastCameraTargetX) {
            lastCameraX=camera.target.x(); lastCameraTargetX=cameraTargetX;
            emit owner->cameraPositionChanged();
        }
        emit owner->presentationChanged();
    }

    void refreshVisible()
    {
        if(!factory) return;
        const auto viewport=canvas->size();
        const float dpr=float(canvas->devicePixelRatioF());
        const auto viewProjection=camera.projectionMatrix(float(std::max(1,viewport.width()))
            /std::max(1,viewport.height()))*camera.viewMatrix();
        const int focusedPixels=std::clamp(int(std::ceil(viewport.height()*dpr*2/1024))*1024,2048,4096);
        const float height=viewport.height()*dpr*0.19f
            /(2*std::max(0.08f,camera.distance)*std::tan(camera.verticalFov*3.14159265f/360));
        const int shelfPixels=std::clamp(int(std::ceil(height*1.6f/256))*256,512,2048);
        const auto [start,count]=shelf.visibleRange(camera.target.x());
        QSet<QString> desired;
        for(int i=start;i<start+count;++i) desired.insert(shelf.items()[i].stableId);
        if(!interaction.selectedId().isEmpty()) desired.insert(interaction.selectedId());
        for(auto it=cases.begin();it!=cases.end();) {
            if(!desired.contains(it->first) && !interaction.active(it->second)) {
                for(const auto primitive:it->second.primitives) primitiveIds.remove(primitive);
                scene->removeNode(it->second.root);
                coverKeys.remove(it->first);
                coverSpecs.remove(it->first);
                it=cases.erase(it);
            } else ++it;
        }
        for(const auto &id:desired) {
            const int index=shelf.indexOf(id);
            if(index<0) continue;
            const auto &item=shelf.items()[index];
            auto it=cases.find(id);
            // The cover contains front, spine and back. A focused case needs
            // enough texels for the physical viewport, including high-DPI screens.
            const auto clip=viewProjection*QVector4D(index*DvdShelfController::spacing,0.095f,0,1);
            const auto ndc=clip.w()>0?clip.toVector3DAffine():QVector3D();
            const QPointF center((ndc.x()+1)*viewport.width()/2,(1-ndc.y())*viewport.height()/2);
            const bool onScreen=!viewport.isEmpty() && clip.w()>0
                && center.x()>=-64 && center.x()<=viewport.width()+64
                && center.y()>=-viewport.height() && center.y()<=viewport.height()*2;
            const int pixels=id==interaction.selectedId()?focusedPixels:(onScreen?shelfPixels:256);
            const auto spec=item.coverPath+QStringLiteral("|%1").arg(pixels);
            const bool request=it==cases.end() || coverSpecs.value(id)!=spec;
            const auto cover=request?covers.request(item.coverPath,pixels)
                :CoverTextureCache::Request{coverKeys.value(id),{}};
            if(request) coverSpecs.insert(id,spec);
            if(it==cases.end()) {
                auto instance=factory->create(*scene,item,index,cover.texture);
                for(const auto primitive:instance.primitives) primitiveIds.insert(primitive,id);
                coverKeys.insert(id,cover.key);
                cases.emplace(id,std::move(instance));
            } else {
                interaction.setDiscHasLocalVideo(it->second,item.hasLocalVideo,now());
                it->second.home=DvdCaseFactory::shelfTransform(index);
                if(id!=interaction.selectedId() && !interaction.active(it->second))
                    scene->setTransform(it->second.root,interaction.shelfPose(it->second));
                if(coverKeys.value(id)!=cover.key) {
                    coverKeys.insert(id,cover.key);
                    // Keep an existing image while another resolution/path is
                    // decoding; cached errors and explicit empty paths still
                    // replace it with the placeholder deterministically.
                    if(cover.key.isEmpty() || covers.find(cover.key))
                        factory->setCover(*scene,it->second,cover.texture);
                }
            }
        }
        for(const auto &[id,instance]:cases) factory->updateSparkle(*scene,instance,now());
    }

    void tick()
    {
        updateFocusDistance();
        const auto time=now();
        const auto before=scene->revision();
        const double dt=std::max(0.0,time-lastTick);
        lastTick=time;
        bool cameraChanged=false;
        if(std::abs(camera.target.x()-cameraTargetX)>1e-5f) {
            float x=camera.target.x()+(cameraTargetX-camera.target.x())*float(1-std::exp(-dt*16));
            if(std::abs(x-cameraTargetX)<1e-5f) x=cameraTargetX;
            camera.target.setX(x);
            cameraChanged=true;
        }
        const auto previousSelection=interaction.selectedId();
        const bool wasAnimating=interaction.active();
        interaction.advance(camera,time);
        interaction.followCamera(camera);
        refreshVisible();
        if(cameraChanged) canvas->setCamera(camera);
        if(cameraChanged || previousSelection!=interaction.selectedId()
            || (owner->isExpanded() ? wasAnimating : canvas->cursor().shape()==Qt::PointingHandCursor))
            refreshHover();
        if(scene->revision()!=before) canvas->update();
        notify();
        updateTimer();
    }

    std::optional<RayHit> pick(const QPointF &point) const
    {
        const auto ray=camera.ray(point,canvas->size());
        if(!ray) return {};
        // An open shell's bounding box contains the disc. Resolve its actual
        // surface so a tilted disc cannot be mistaken for the box around it.
        return raycast(*scene,*ray,owner->isExpanded()?RaycastMode::Triangles:RaycastMode::Bounds);
    }

    QString hit(const QPointF &point) const
    {
        const auto result=pick(point);
        return result?primitiveIds.value(result->node):QString();
    }

    bool isDiscHit(const std::optional<RayHit> &hit,const QString &id) const
    {
        if(!hit || !owner->isExpanded()) return false;
        const auto instance=cases.find(id);
        const auto *node=scene->node(hit->node);
        return instance!=cases.end() && node && node->parent==instance->second.disc;
    }

    void refreshHover()
    {
        if(!hoverPositionValid || dragButton!=Qt::NoButton) return;
        const auto result=pick(hoverPosition);
        const auto id=result?primitiveIds.value(result->node):QString();
        interaction.hover(id,now());
        canvas->setCursor(isDiscHit(result,id)?Qt::PointingHandCursor:Qt::ArrowCursor);
    }

    void cameraChanged()
    {
        updateFocusDistance();
        interaction.followCamera(camera);
        refreshVisible();
        canvas->setCamera(camera);
        refreshHover();
        notify();
        updateTimer();
    }
};

DvdSceneWidget::DvdSceneWidget(QWidget *parent):QWidget(parent),d(std::make_unique<Impl>(this)) {}
DvdSceneWidget::~DvdSceneWidget() { d->timer.stop(); }

bool DvdSceneWidget::setModel(std::shared_ptr<const ModelAsset> model)
{
    QSet<QString> names;
    if(model) for(const auto &node:model->nodes) names.insert(node.name);
    if(!model || !names.contains("back") || !names.contains("front")
        || !names.contains("spine") || !names.contains("disc")) {
        emit sceneError(QStringLiteral("DVD model requires back/spine/front/disc nodes"));
        return false;
    }
    d->interaction.reset();
    d->cases.clear();
    d->primitiveIds.clear();
    d->coverKeys.clear();
    d->coverSpecs.clear();
    d->scene->clear();
    d->factory=std::make_unique<DvdCaseFactory>(std::move(model));
    d->refreshVisible();
    d->canvas->update();
    d->notify();
    for(const auto &item:d->shelf.items())
        if(item.selected) { focusItem(item.stableId); break; }
    return true;
}

bool DvdSceneWidget::setItems(QList<DvdItemViewModel> items)
{
    const bool initial=d->shelf.items().isEmpty();
    QString error;
    if(!d->shelf.setItems(std::move(items),&error)) { emit sceneError(error); return false; }
    d->coverSpecs.clear(); // re-check path versions after an explicit data update
    if(!d->interaction.selectedId().isEmpty() && d->shelf.indexOf(d->interaction.selectedId())<0)
        d->interaction.reset();
    for(auto it=d->cases.begin();it!=d->cases.end();) {
        if(d->shelf.indexOf(it->first)<0) {
            for(const auto primitive:it->second.primitives) d->primitiveIds.remove(primitive);
            d->scene->removeNode(it->second.root);
            d->coverKeys.remove(it->first);
            d->coverSpecs.remove(it->first);
            it=d->cases.erase(it);
        } else ++it;
    }
    if(initial) d->cameraTargetX=(std::min(itemCount(),DvdShelfController::maxVisibleItems)-1)
        *DvdShelfController::spacing*0.5f;
    d->cameraTargetX=d->shelf.clampCameraX(d->cameraTargetX);
    d->camera.target.setX(d->shelf.clampCameraX(initial?d->cameraTargetX:d->camera.target.x()));
    d->refreshVisible();
    d->canvas->setCamera(d->camera);
    d->notify();
    d->updateTimer();
    for(const auto &item:d->shelf.items())
        if(item.selected) { focusItem(item.stableId); break; }
    return true;
}

bool DvdSceneWidget::updateItem(const DvdItemViewModel &item)
{
    auto items=d->shelf.items();
    const auto index=d->shelf.indexOf(item.stableId);
    if(index<0) return false;
    items[index]=item;
    return setItems(std::move(items));
}

bool DvdSceneWidget::focusItem(const QString &id)
{
    d->updateFocusDistance();
    if(!d->factory || d->shelf.indexOf(id)<0) return false;
    if(!d->cases.contains(id)) {
        d->cameraTargetX=d->shelf.clampCameraX(d->shelf.indexOf(id)*DvdShelfController::spacing);
        d->camera.target.setX(d->cameraTargetX);
        d->refreshVisible();
        d->canvas->setCamera(d->camera);
    }
    const bool result=d->interaction.select(id,d->camera,d->now());
    d->refreshVisible();
    d->notify();
    d->canvas->update();
    d->updateTimer();
    return result;
}
void DvdSceneWidget::returnToShelf()
{
    d->interaction.returnToShelf(d->now());
    d->notify();
    d->updateTimer();
}
void DvdSceneWidget::toggleOpen() { d->interaction.toggleOpen(d->now()); d->notify(); d->updateTimer(); }
bool DvdSceneWidget::isExpanded() const
{
    const auto it=d->cases.find(selectedId());
    return it!=d->cases.end() && it->second.expanded;
}
bool DvdSceneWidget::isFullyExpanded() const
{
    const auto it=d->cases.find(selectedId());
    return it!=d->cases.end() && it->second.expanded
        && d->interaction.state()==DvdInteractionController::State::Focused
        && !d->interaction.active(it->second);
}
bool DvdSceneWidget::isNearlyClosed() const
{
    const auto it=d->cases.find(selectedId());
    if(it==d->cases.end() || it->second.expanded) return false;
    // Use the actual animated pose, so interruptions and slow frames do not
    // reveal overlays while the lid is still visibly open.
    const float minimumDot=std::cos(8.0f*3.14159265f/360.0f);
    for(const auto hinge:{it->second.spine,it->second.front}) {
        const auto *node=d->scene->node(hinge);
        if(!node || std::abs(QQuaternion::dotProduct(node->transform.rotation,QQuaternion()))<minimumDot)
            return false;
    }
    return true;
}
std::optional<QRectF> DvdSceneWidget::selectedFrontRect() const
{
    const auto it=d->cases.find(selectedId());
    if(it==d->cases.end()) return {};
    QPointF lower(1e20,1e20),upper(-1e20,-1e20);
    bool found=false;
    for(const auto part:it->second.coverPrimitives) {
        const auto *node=d->scene->node(part);
        if(!node || node->parent!=it->second.front || !node->mesh) continue;
        const auto world=d->scene->worldMatrix(part);
        for(const auto &vertex:node->mesh->vertices) {
            const auto point=d->camera.project(world.map({vertex.position[0],vertex.position[1],vertex.position[2]}),d->canvas->size());
            if(!point) return {};
            lower.setX(std::min(lower.x(),point->x())); lower.setY(std::min(lower.y(),point->y()));
            upper.setX(std::max(upper.x(),point->x())); upper.setY(std::max(upper.y(),point->y()));
            found=true;
        }
    }
    return found?std::optional<QRectF>(QRectF(lower,upper)):std::nullopt;
}
std::optional<QPointF> DvdSceneWidget::selectedDiscCenter() const
{
    const auto it=d->cases.find(selectedId());
    if(it==d->cases.end() || !it->second.disc) return {};
    return d->camera.project(d->scene->worldMatrix(it->second.disc).map(QVector3D()),d->canvas->size());
}
std::optional<QRectF> DvdSceneWidget::selectedCaseRect() const
{
    const auto it=d->cases.find(selectedId());
    if(it==d->cases.end()) return {};
    QPointF lower(1e20,1e20),upper(-1e20,-1e20);
    bool found=false;
    for(const auto part:it->second.primitives) {
        const auto *node=d->scene->node(part);
        if(!node || node->parent==it->second.disc || !node->mesh) continue;
        const auto world=d->scene->worldMatrix(part);
        for(const auto &vertex:node->mesh->vertices) {
            const auto point=d->camera.project(world.map({vertex.position[0],vertex.position[1],vertex.position[2]}),d->canvas->size());
            if(!point) return {};
            lower.setX(std::min(lower.x(),point->x())); lower.setY(std::min(lower.y(),point->y()));
            upper.setX(std::max(upper.x(),point->x())); upper.setY(std::max(upper.y(),point->y()));
            found=true;
        }
    }
    return found?std::optional<QRectF>(QRectF(lower,upper)):std::nullopt;
}
std::optional<QPointF> DvdSceneWidget::selectedSpineCenter() const
{
    const auto it=d->cases.find(selectedId());
    if(it==d->cases.end() || !it->second.spine) return {};
    return d->camera.project(d->scene->worldMatrix(it->second.spine).map(QVector3D(0.006707f,0,0)),d->canvas->size());
}
float DvdSceneWidget::cameraX() const { return d->camera.target.x(); }
std::optional<QPointF> DvdSceneWidget::selectedInfoAnchor() const
{
    const auto it=d->cases.find(selectedId());
    if(it==d->cases.end()) return {};
    const auto *front=d->scene->node(it->second.front);
    if(!front) return {};
    // Legacy frontInfoAnchor was a sibling of front under spine, rather than
    // the center of the front mesh. It follows the spine's rotation only.
    const auto local=front->transform.translation+QVector3D(0.0675f,0,0);
    return d->camera.project(d->scene->worldMatrix(it->second.spine).map(local),d->canvas->size());
}
float DvdSceneWidget::cameraTargetX() const { return d->cameraTargetX; }
void DvdSceneWidget::setCameraX(float x,bool immediate)
{
    d->cameraTargetX=d->shelf.clampCameraX(x);
    if(immediate) { d->camera.target.setX(d->cameraTargetX); d->cameraChanged(); }
    d->notify(); d->updateTimer();
}
void DvdSceneWidget::scrollToIndex(int index)
{
    d->cameraTargetX=d->shelf.clampCameraX(index*DvdShelfController::spacing);
    d->updateTimer();
}
void DvdSceneWidget::resetCamera()
{
    d->camera.yaw=0; d->camera.pitch=0; d->camera.distance=0.25f;
    d->camera.target.setY(0.1f);
    d->cameraChanged();
}
void DvdSceneWidget::setDarkTheme(bool dark)
{
    d->canvas->setClearColor(QColor(dark?"#151c2b":"#eef1f7"));
}
void DvdSceneWidget::setCoverBudget(quint64 bytes)
{
    d->covers.setBudget(bytes);
    d->canvas->setTextureBudget(bytes);
}
QString DvdSceneWidget::selectedId() const { return d->interaction.selectedId(); }
DvdInteractionController::State DvdSceneWidget::state() const { return d->interaction.state(); }
int DvdSceneWidget::itemCount() const { return int(d->shelf.items().size()); }
int DvdSceneWidget::visibleItemCount() const { return int(d->cases.size()); }
int DvdSceneWidget::pendingCovers() const { return d->covers.pendingCount(); }
quint64 DvdSceneWidget::cachedCoverBytes() const { return d->covers.bytes(); }
RhiSceneWidget *DvdSceneWidget::renderWidget() const { return d->canvas; }
const QList<DvdItemViewModel> &DvdSceneWidget::items() const { return d->shelf.items(); }

void DvdSceneWidget::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    if(d->trackedWindow!=window()) {
        if(d->trackedWindow) d->trackedWindow->removeEventFilter(this);
        d->trackedWindow=window();
        d->trackedWindow->installEventFilter(this);
    }
    d->tick();
}
void DvdSceneWidget::hideEvent(QHideEvent *event) { d->timer.stop(); QWidget::hideEvent(event); }

bool DvdSceneWidget::eventFilter(QObject *object,QEvent *event)
{
    if(object==d->trackedWindow && event->type()==QEvent::WindowStateChange)
        QTimer::singleShot(0,this,[this] { d->tick(); });
    if(object!=d->canvas) return QWidget::eventFilter(object,event);
    switch(event->type()) {
    case QEvent::Resize:
        d->cameraChanged();
        break;
    case QEvent::MouseButtonPress: {
        const auto *mouse=static_cast<QMouseEvent *>(event);
        d->dragButton=mouse->button();
        d->pressPosition=d->lastPosition=mouse->position();
        d->moved=false;
        d->canvas->setFocus();
        return true;
    }
    case QEvent::MouseMove: {
        const auto *mouse=static_cast<QMouseEvent *>(event);
        const auto delta=mouse->position()-d->lastPosition;
        d->lastPosition=mouse->position();
        if((mouse->position()-d->pressPosition).manhattanLength()>5) d->moved=true;
        if(d->dragButton==Qt::RightButton) {
            // Match QML: turn at the eye, with restrained yaw/pitch.
            d->camera.yaw=std::clamp(d->camera.yaw-float(delta.x())*0.15f,-15.0f,15.0f);
            d->camera.pitch=std::clamp(d->camera.pitch-float(delta.y())*0.15f,-10.0f,10.0f);
            d->cameraChanged();
        } else if(d->dragButton==Qt::MiddleButton) {
            const float scale=d->camera.distance/std::max(1,d->canvas->height());
            d->camera.pan(float(-delta.x())*scale,float(delta.y())*scale);
            d->camera.target.setX(d->shelf.clampCameraX(d->camera.target.x()));
            d->cameraTargetX=d->camera.target.x();
            d->cameraChanged();
        } else if(mouse->buttons()==Qt::NoButton) {
            d->hoverPosition=mouse->position();
            d->hoverPositionValid=true;
            d->refreshHover();
            d->updateTimer();
        }
        return true;
    }
    case QEvent::MouseButtonRelease: {
        const auto *mouse=static_cast<QMouseEvent *>(event);
        if(mouse->button()==Qt::LeftButton && !d->moved) {
            const auto hit=d->pick(mouse->position());
            const auto id=hit?d->primitiveIds.value(hit->node):QString();
            if(id.isEmpty()) returnToShelf();
            else {
                if(d->isDiscHit(hit,id)) {
                    emit discClicked(id);
                    d->dragButton=Qt::NoButton;
                    return true;
                }
                if(id==selectedId() && state()==DvdInteractionController::State::Focused) toggleOpen();
                else focusItem(id);
                emit dvdClicked(id);
            }
        }
        d->dragButton=Qt::NoButton;
        return true;
    }
    case QEvent::MouseButtonDblClick: {
        const auto *mouse=static_cast<QMouseEvent *>(event);
        const auto id=d->hit(mouse->position());
        if(!id.isEmpty()) { focusItem(id); emit dvdDoubleClicked(id); }
        return true;
    }
    case QEvent::Wheel: {
        const auto *wheel=static_cast<QWheelEvent *>(event);
        d->hoverPosition=wheel->position();
        d->hoverPositionValid=d->canvas->rect().contains(wheel->position().toPoint());
        const auto angle=wheel->angleDelta();
        const float steps=angle.isNull()?float(wheel->pixelDelta().y())/60
            :float(angle.y()?angle.y():angle.x())/120;
        if(wheel->modifiers().testFlag(Qt::ControlModifier)) {
            d->camera.zoom(steps);
            d->camera.distance=std::clamp(d->camera.distance,0.08f,2.0f);
            d->cameraChanged();
        } else {
            d->interaction.returnToShelf(d->now(),3.0f);
            d->notify();
            d->cameraTargetX=d->shelf.clampCameraX(d->cameraTargetX-steps*3*DvdShelfController::spacing);
            d->refreshHover();
            d->updateTimer();
        }
        return true;
    }
    case QEvent::KeyPress: {
        const auto *key=static_cast<QKeyEvent *>(event);
        if(key->key()==Qt::Key_Escape) { returnToShelf(); return true; }
        if(key->key()==Qt::Key_Left || key->key()==Qt::Key_Right) {
            const float direction=key->key()==Qt::Key_Left?-1.0f:1.0f;
            d->cameraTargetX=d->shelf.clampCameraX(d->cameraTargetX+direction*3*DvdShelfController::spacing);
            d->updateTimer(); return true;
        }
        break;
    }
    case QEvent::Enter:
        d->hoverPosition=static_cast<QEnterEvent *>(event)->position();
        d->hoverPositionValid=true;
        d->refreshHover(); d->updateTimer(); break;
    case QEvent::Leave:
        d->hoverPositionValid=false;
        d->interaction.hover({},d->now());
        d->canvas->setCursor(Qt::ArrowCursor);
        d->updateTimer(); break;
    default:break;
    }
    return QWidget::eventFilter(object,event);
}
} // namespace darkeye::dvd_scene
