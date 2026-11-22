#pragma once
#include <QtGlobal>
#include <memory>
#include <vector>
class QRhi;
class QRhiBuffer;
class QRhiCommandBuffer;
class GpuGraphGeometry {
public:
    struct Instance {
        // nodes[2]: Highlighted/Hovered bits, independent of animation amount.
        quint32 nodes[4] = {};
        float shape[4] = {};
        float color[4] = {};
    };
    enum HighlightFlag : quint32 { Highlighted = 1, Hovered = 2 };
    struct HoverStyle {
        float amount = 0;
        float radiusScale = 1;
        float dimColor[4] = {};
        float edgeDimColor[4] = {};
        float highlightColor[4] = {};
    };
    GpuGraphGeometry();
    ~GpuGraphGeometry();
    bool record(QRhi* rhi, QRhiCommandBuffer* cb, QRhiBuffer* state,
                const std::vector<Instance>& descriptions, const float* viewport, quint64 revision,
                const HoverStyle* hover = nullptr);
    QRhiBuffer* instanceBuffer() const;
    // Text source positions are glyph offsets; fields 10/11 are the label
    // origin relative to its node. Snap only the final resident label origin.
    bool recordText(QRhiCommandBuffer* cb, QRhiBuffer* state, const float* vertices, quint32 count,
                    const float* screen = nullptr);
    QRhiBuffer* textBuffer() const;
private:
    struct Impl;
    std::unique_ptr<Impl> d;
};
