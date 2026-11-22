#ifndef PHYSICSSTATE_H
#define PHYSICSSTATE_H

#include <vector>
#include <cstdint>
#include <atomic>
#include <algorithm>
#include <mutex>

/**
 * PhysicsState: flat arrays holding physical simulation data.
 *
 * All per-node arrays are indexed 0..nNodes-1.
 * pos/vel are interleaved: [x0,y0, x1,y1, ...], length 2*nNodes.
 * edges is interleaved: [src0,dst0, src1,dst1, ...], length 2*edgeCount.
 */
struct PhysicsState
{
    int nNodes = 0;

    // positions: [x0,y0, x1,y1, ...], size 2*nNodes
    std::vector<float> pos;

    // velocities: [x0,y0, x1,y1, ...], size 2*nNodes
    std::vector<float> vel;

    // per-node mass, size nNodes (default 1.0)
    std::vector<float> mass;

    // per-node dragging flag, size nNodes
    std::vector<uint8_t> dragging;   // 0 or 1 (avoid std::vector<bool> proxy)

    // Render-side double buffer for position snapshots.
    std::vector<float> renderPosA;
    std::vector<float> renderPosB;
    std::atomic<int> renderIndex{0};
    mutable std::mutex renderMutex;

    // Drag target positions, size 2*nNodes.
    std::vector<float> dragPos;

    // Edge list: [src0,dst0, src1,dst1, ...], size 2*E
    std::vector<int> edges;

    int edgeCount() const { return static_cast<int>(edges.size()) / 2; }

    /**
     * Allocate/reset all arrays for n nodes and the given edge list.
     * pos is not initialized here; caller fills it.
     */
    void init(int n, const std::vector<int>& edgeList)
    {
        nNodes = n;
        pos.resize(2 * n);
        vel.assign(2 * n, 0.0f);
        mass.assign(n, 1.0f);
        dragging.assign(n, 0);
        renderPosA.assign(2 * n, 0.0f);
        renderPosB.assign(2 * n, 0.0f);
        renderIndex.store(0, std::memory_order_release);
        dragPos.assign(2 * n, 0.0f);
        edges.clear();
        edges.reserve(edgeList.size());
        for (size_t i = 0; i + 1 < edgeList.size(); i += 2) {
            const int source = edgeList[i];
            const int target = edgeList[i + 1];
            if (source < 0 || source >= n || target < 0 || target >= n || source == target)
                continue;
            edges.push_back(source);
            edges.push_back(target);
        }
    }

    // Convenience accessors (no bounds check)
    float  px(int i) const { return pos[2 * i];     }
    float  py(int i) const { return pos[2 * i + 1]; }
    float& px(int i)       { return pos[2 * i];     }
    float& py(int i)       { return pos[2 * i + 1]; }

    float  vx(int i) const { return vel[2 * i];     }
    float  vy(int i) const { return vel[2 * i + 1]; }
    float& vx(int i)       { return vel[2 * i];     }
    float& vy(int i)       { return vel[2 * i + 1]; }

    void copyRenderPos(std::vector<float>& out) const
    {
        std::lock_guard<std::mutex> lock(renderMutex);
        int idx = renderIndex.load(std::memory_order_acquire);
        const auto& src = (idx == 0) ? renderPosA : renderPosB;
        out.assign(src.begin(), src.end());
    }

    void syncRenderPosFromPos()
    {
        std::lock_guard<std::mutex> lock(renderMutex);
        if (renderPosA.size() != pos.size()) {
            renderPosA.resize(pos.size());
            renderPosB.resize(pos.size());
        }
        std::copy(pos.begin(), pos.end(), renderPosA.begin());
        std::copy(pos.begin(), pos.end(), renderPosB.begin());
        renderIndex.store(0, std::memory_order_release);
    }

    void publishRenderPos()
    {
        std::lock_guard<std::mutex> lock(renderMutex);
        int front = renderIndex.load(std::memory_order_acquire);
        int back = 1 - front;
        auto& dst = (back == 0) ? renderPosA : renderPosB;
        if (dst.size() != pos.size())
            dst.resize(pos.size());
        std::copy(pos.begin(), pos.end(), dst.begin());
        renderIndex.store(back, std::memory_order_release);
    }

    void syncDragPosFromPos()
    {
        if (dragPos.size() != pos.size())
            dragPos.resize(pos.size());
        std::copy(pos.begin(), pos.end(), dragPos.begin());
    }

    // Set drag target position from pointer interaction.
    void setDragPos(int i, float x, float y)
    {
        if (i < 0 || i >= nNodes) return;
        dragPos[2 * i]     = x;
        dragPos[2 * i + 1] = y;
    }

    void updateRenderPosAt(int i, float x, float y)
    {
        if (i < 0 || i >= nNodes) return;
        std::lock_guard<std::mutex> lock(renderMutex);
        int idx = renderIndex.load(std::memory_order_acquire);
        auto& buf = (idx == 0) ? renderPosA : renderPosB;
        if (buf.size() < pos.size())
            buf.resize(pos.size());
        buf[2 * i]     = x;
        buf[2 * i + 1] = y;
    }
};

#endif // PHYSICSSTATE_H


