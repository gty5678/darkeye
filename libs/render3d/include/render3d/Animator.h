#pragma once

#include "Scene.h"
#include <map>

namespace darkeye::render3d {

class Animator {
public:
    // Times are monotonic seconds supplied by the controller. Retargeting
    // samples an existing animation at 'now' before replacing it.
    bool animateTo(Scene &scene, NodeId id, const Transform &target,
                   double now, double duration);
    std::vector<NodeId> advance(Scene &scene, double now);
    void cancel(NodeId id);
    void clear();
    [[nodiscard]] bool active() const { return !m_tracks.empty(); }
    [[nodiscard]] bool active(NodeId id) const { return m_tracks.contains(id); }

private:
    struct Track { Transform from, to; double start = 0, duration = 0; };
    [[nodiscard]] Transform sample(const Track &track, double now) const;
    std::map<NodeId, Track> m_tracks;
};
} // namespace darkeye::render3d
