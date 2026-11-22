#include "render3d/Animator.h"
#include <algorithm>
#include <cmath>

namespace darkeye::render3d {
Transform Animator::sample(const Track &track, double now) const
{
    const float t = float(std::clamp((now - track.start) / track.duration, 0.0, 1.0));
    const float eased = t * t * (3 - 2 * t);
    return Transform::interpolate(track.from, track.to, eased);
}

bool Animator::animateTo(Scene &scene, NodeId id, const Transform &target,
                          double now, double duration)
{
    if (!scene.node(id) || !target.isFinite() || !std::isfinite(now)
        || !std::isfinite(duration) || duration < 0)
        return false;
    if (const auto it = m_tracks.find(id); it != m_tracks.end())
        scene.setTransform(id, sample(it->second, now));
    const auto from = scene.node(id)->transform;
    if (duration == 0) {
        m_tracks.erase(id);
        return scene.setTransform(id, target);
    }
    m_tracks[id] = {from, target, now, duration};
    return true;
}

std::vector<NodeId> Animator::advance(Scene &scene, double now)
{
    std::vector<NodeId> finished;
    if (!std::isfinite(now))
        return finished;
    for (auto it = m_tracks.begin(); it != m_tracks.end();) {
        if (!scene.node(it->first)) {
            it = m_tracks.erase(it);
            continue;
        }
        scene.setTransform(it->first, sample(it->second, now));
        if (now >= it->second.start + it->second.duration) {
            // Set the exact endpoint; no accumulated frame deltas or drift.
            scene.setTransform(it->first, it->second.to);
            finished.push_back(it->first);
            it = m_tracks.erase(it);
        } else {
            ++it;
        }
    }
    return finished;
}
void Animator::cancel(NodeId id) { m_tracks.erase(id); }
void Animator::clear() { m_tracks.clear(); }
} // namespace darkeye::render3d
