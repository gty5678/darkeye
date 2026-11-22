#include "dvd_scene/DvdInteractionController.h"
#include <algorithm>
#include <cmath>

namespace darkeye::dvd_scene {
using namespace render3d;
DvdInteractionController::DvdInteractionController(Scene &scene,std::map<QString,DvdCaseInstance> &cases)
    :m_scene(scene),m_cases(cases) {}

Transform DvdInteractionController::focusTransform(const Camera &camera) const
{
    const auto cameraRotation=QQuaternion::fromEulerAngles(camera.pitch,camera.yaw,0);
    Transform result;
    result.rotation=cameraRotation*QQuaternion::fromAxisAndAngle(0,1,0,-90);
    const QVector3D pivot(-0.006707f,0,-0.000293f);
    const auto anchor=camera.position()+cameraRotation.rotatedVector({m_focusHorizontalOffset,-0.095f,-m_focusDistance});
    result.translation=anchor-result.rotation.rotatedVector(pivot);
    return result;
}

bool DvdInteractionController::select(const QString &id,const Camera &,double now)
{
    const auto it=m_cases.find(id);
    if(it==m_cases.end()) return false;
    if(id==m_selected && m_state!=State::Returning) return true;
    if(const auto previous=m_cases.find(m_selected);previous!=m_cases.end()) {
        close(previous->second,now);
        m_animator.animateTo(m_scene,previous->second.root,previous->second.home,now,0.5);
    }
    hover({},now);
    m_selected=id;
    m_waitingForClose=false;
    m_inserting=false;
    m_hovered.clear();
    m_state=State::Selected;
    auto pulled=it->second.home;
    pulled.translation.setZ(pulled.translation.z()+0.14f);
    m_animator.animateTo(m_scene,it->second.root,pulled,now,0.18);
    m_pullingOut=true;
    m_state=State::Focusing;
    return true;
}

void DvdInteractionController::close(DvdCaseInstance &instance,double now,double duration)
{
    instance.expanded=false;
    instance.backFolded=false;
    animateHinges(instance,now,duration);
}

void DvdInteractionController::animateHinges(DvdCaseInstance &instance,double now,double duration)
{
    const auto openRotation=QQuaternion::fromAxisAndAngle(0,1,0,-90);
    // Use exact zero for the half-turn so interpolation consistently swings
    // the right panel toward the viewer before it settles over the left.
    const QQuaternion backRotation(0,0,-1,0);
    for(const auto id:{instance.back,instance.spine,instance.front}) {
        if(const auto *node=m_scene.node(id)) {
            auto transform=node->transform;
            transform.rotation={};
            if(id==instance.back) {
                transform.translation=instance.backHomeTranslation;
                if(instance.backFolded) transform.rotation=backRotation;
            } else if(instance.expanded) {
                // As the back folds, these two hinges counter-rotate so
                // the front stays facing the viewer on the left.
                transform.rotation=openRotation;
            }
            m_animator.animateTo(m_scene,id,transform,now,duration);
        }
    }
    // Retargeting can sample an in-flight track before the next frame.
    pinFrontHinge(instance);
}

void DvdInteractionController::pinFrontHinge(const DvdCaseInstance &instance)
{
    const auto *back=m_scene.node(instance.back);
    if(!back || back->transform.rotation.isIdentity()) return;
    const auto *spine=m_scene.node(instance.spine);
    const auto *front=m_scene.node(instance.front);
    if(!spine || !front) return;

    auto pose=back->transform;
    const auto frontHinge=spine->transform.matrix().map(front->transform.translation);
    // The spine counter-rotates by half the back's turn. Translate the
    // assembly by the difference so the front hinge stays in place.
    const auto halfTurn=QQuaternion::slerp(QQuaternion(),pose.rotation,0.5f);
    pose.translation=instance.backHomeTranslation
        +halfTurn.rotatedVector(frontHinge)-pose.rotation.rotatedVector(frontHinge);
    m_scene.setTransform(instance.back,pose);
}

void DvdInteractionController::beginReturn(DvdCaseInstance &instance,double now)
{
    auto aligned=instance.home;
    aligned.translation.setZ(aligned.translation.z()+0.14f);
    m_animator.animateTo(m_scene,instance.root,aligned,now,0.32);
    m_alignmentDeadline=now+0.32;
    m_waitingForClose=false;
    m_inserting=false;
}

void DvdInteractionController::returnToShelf(double now,float closeSpeed)
{
    if(m_state==State::Returning) return;
    const auto it=m_cases.find(m_selected);
    if(it==m_cases.end()) return;
    const bool closing=it->second.expanded || it->second.backFolded
        || m_animator.active(it->second.back) || m_animator.active(it->second.spine)
        || m_animator.active(it->second.front);
    m_animator.advance(m_scene,now);
    m_animator.cancel(it->second.root);
    m_inserting=false;
    if(closing) {
        const float speed=std::isfinite(closeSpeed)?std::clamp(closeSpeed,0.1f,10.0f):1.0f;
        const double duration=0.5/speed;
        close(it->second,now,duration);
        m_closeDeadline=now+duration;
        m_waitingForClose=true;
    } else {
        beginReturn(it->second,now);
    }
    m_state=State::Returning;
    m_pullingOut=false;
}

void DvdInteractionController::toggleOpen(double now)
{
    const auto it=m_cases.find(m_selected);
    if(it==m_cases.end() || m_state!=State::Focused) return;
    auto &instance=it->second;
    if(instance.expanded) {
        close(instance,now,0.5);
    } else {
        instance.expanded=true;
        instance.backFolded=false;
        if(instance.disc) m_scene.setVisible(instance.disc,true);
        animateHinges(instance,now,0.5);
    }
}

void DvdInteractionController::closeToBack(double now)
{
    const auto it=m_cases.find(m_selected);
    if(it==m_cases.end() || m_state!=State::Focused || !it->second.expanded || !it->second.back) return;
    auto &instance=it->second;
    instance.expanded=false;
    instance.backFolded=true;
    animateHinges(instance,now,0.5);
}

Transform DvdInteractionController::shelfPose(const DvdCaseInstance &instance) const
{
    auto pose=instance.home;
    if(instance.stableId==m_hovered && instance.stableId!=m_selected)
        pose.translation.setZ(pose.translation.z()+0.03f);
    return pose;
}

void DvdInteractionController::setDiscHasLocalVideo(DvdCaseInstance &instance,bool hasLocalVideo,double now)
{
    if(instance.hasLocalVideo==hasLocalVideo || !instance.disc) return;
    instance.hasLocalVideo=hasLocalVideo;
    auto pose=instance.discHome;
    // The authored disc normal is X: twist the artwork in its own plane,
    // retaining the model's center, scale and original orientation.
    pose.rotation=QQuaternion::fromAxisAndAngle(1,0,0,hasLocalVideo?0:30)*pose.rotation;
    m_animator.animateTo(m_scene,instance.disc,pose,now,0.38);
}

void DvdInteractionController::hover(const QString &id,double now)
{
    if(id==m_hovered) return;
    if(auto previous=m_cases.find(m_hovered);previous!=m_cases.end() && previous->first!=m_selected)
        m_animator.animateTo(m_scene,previous->second.root,previous->second.home,now,0.2);
    m_hovered=id;
    if(auto current=m_cases.find(id);current!=m_cases.end() && current->first!=m_selected) {
        m_animator.animateTo(m_scene,current->second.root,shelfPose(current->second),now,0.2);
    }
}

void DvdInteractionController::advance(const Camera &camera,double now)
{
    auto finished=m_animator.advance(m_scene,now);
    const auto it=m_cases.find(m_selected);
    if(m_waitingForClose && it!=m_cases.end() && now>=m_closeDeadline) {
        beginReturn(it->second,m_closeDeadline);
        const auto returned=m_animator.advance(m_scene,now);
        finished.insert(finished.end(),returned.begin(),returned.end());
    }
    if(m_state==State::Returning && !m_waitingForClose && !m_inserting
        && it!=m_cases.end() && now>=m_alignmentDeadline) {
        // Finish rotation/alignment outside the shelf, then slide straight in.
        m_inserting=true;
        m_animator.animateTo(m_scene,it->second.root,it->second.home,m_alignmentDeadline,0.18);
        const auto inserted=m_animator.advance(m_scene,now);
        finished.insert(finished.end(),inserted.begin(),inserted.end());
    }
    if(it!=m_cases.end() && std::find(finished.begin(),finished.end(),it->second.root)!=finished.end()) {
        if(m_state==State::Returning && m_inserting && !m_animator.active(it->second.root)) {
            m_selected.clear();
            m_state=State::Idle;
        } else if(m_state==State::Focusing && m_pullingOut) {
            m_pullingOut=false;
            m_animator.animateTo(m_scene,it->second.root,focusTransform(camera),now,0.32);
        } else if(m_state==State::Focusing) {
            m_state=State::Focused;
        }
    }
    for(auto &[_,instance]:m_cases) {
        pinFrontHinge(instance);
        if(!instance.expanded && !m_animator.active(instance.back) && !m_animator.active(instance.spine)
            && !m_animator.active(instance.front) && instance.disc) m_scene.setVisible(instance.disc,false);
    }
}

void DvdInteractionController::followCamera(const Camera &camera)
{
    if(m_state==State::Focused)
        if(const auto it=m_cases.find(m_selected);it!=m_cases.end())
            m_scene.setTransform(it->second.root,focusTransform(camera));
}

void DvdInteractionController::reset()
{
    m_animator.clear();
    m_selected.clear();
    m_hovered.clear();
    m_state=State::Idle;
    m_pullingOut=false;
    m_waitingForClose=false;
    m_inserting=false;
    m_closeDeadline=0;
    m_alignmentDeadline=0;
    for(auto &[_,instance]:m_cases) {
        m_scene.setTransform(instance.root,instance.home);
        instance.expanded=false;
        instance.backFolded=false;
        if(instance.disc) m_scene.setVisible(instance.disc,false);
        if(instance.disc) {
            auto pose=instance.discHome;
            pose.rotation=QQuaternion::fromAxisAndAngle(1,0,0,instance.hasLocalVideo?0:30)*pose.rotation;
            m_scene.setTransform(instance.disc,pose);
        }
        for(const auto id:{instance.back,instance.spine,instance.front}) {
            if(const auto *node=m_scene.node(id)) {
                auto transform=node->transform;
                transform.rotation={};
                if(id==instance.back) transform.translation=instance.backHomeTranslation;
                m_scene.setTransform(id,transform);
            }
        }
    }
}

bool DvdInteractionController::active(const DvdCaseInstance &instance) const
{
    return m_animator.active(instance.root) || m_animator.active(instance.spine)
        || m_animator.active(instance.front) || m_animator.active(instance.back);
}
QString DvdInteractionController::stateName(State state)
{
    switch(state) {
    case State::Idle:return QStringLiteral("Idle");
    case State::Selected:return QStringLiteral("Selected");
    case State::Focusing:return QStringLiteral("Focusing");
    case State::Focused:return QStringLiteral("Focused");
    case State::Returning:return QStringLiteral("Returning");
    }
    return {};
}
} // namespace darkeye::dvd_scene
