#pragma once
#include "DvdCaseFactory.h"
#include "render3d/Animator.h"
#include "render3d/Camera.h"

namespace darkeye::dvd_scene {
class DvdInteractionController {
public:
    enum class State { Idle, Selected, Focusing, Focused, Returning };
    DvdInteractionController(render3d::Scene &scene,std::map<QString,DvdCaseInstance> &cases);
    bool select(const QString &id,const render3d::Camera &camera,double now);
    void returnToShelf(double now,float closeSpeed=1.0f);
    void toggleOpen(double now);
    void closeToBack(double now);
    void hover(const QString &id,double now);
    void setDiscHasLocalVideo(DvdCaseInstance &instance,bool hasLocalVideo,double now);
    [[nodiscard]] render3d::Transform shelfPose(const DvdCaseInstance &instance) const;
    void advance(const render3d::Camera &camera,double now);
    void followCamera(const render3d::Camera &camera);
    void setFocusHorizontalOffset(float offset) { m_focusHorizontalOffset=offset; }
    void setFocusDistance(float distance) { m_focusDistance=distance; }
    void reset();
    [[nodiscard]] bool active() const { return m_animator.active(); }
    [[nodiscard]] bool active(const DvdCaseInstance &instance) const;
    [[nodiscard]] QString selectedId() const { return m_selected; }
    [[nodiscard]] State state() const { return m_state; }
    [[nodiscard]] static QString stateName(State state);
private:
    render3d::Transform focusTransform(const render3d::Camera &camera) const;
    void animateHinges(DvdCaseInstance &instance,double now,double duration);
    void pinFrontHinge(const DvdCaseInstance &instance);
    void close(DvdCaseInstance &instance,double now,double duration=0.35);
    void beginReturn(DvdCaseInstance &instance,double now);
    render3d::Scene &m_scene;
    std::map<QString,DvdCaseInstance> &m_cases;
    render3d::Animator m_animator;
    QString m_selected,m_hovered;
    State m_state=State::Idle;
    bool m_pullingOut=false;
    bool m_waitingForClose=false;
    bool m_inserting=false;
    double m_closeDeadline=0;
    float m_focusDistance=0.2f;
    float m_focusHorizontalOffset=0.0075f;
    double m_alignmentDeadline=0;
};
} // namespace darkeye::dvd_scene
