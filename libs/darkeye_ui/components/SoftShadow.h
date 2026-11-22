#pragma once

#include <QImage>
#include <QPainterPath>

namespace darkeye {

// Thread-safe cached black shadow: opacity 80/255, default sigma 2 DIP.
// pixels is the physical canvas size. Call from a worker to keep cache misses
// off the UI thread; leave 3 * blurSigma DIP around the offset shape for the blur.
QImage renderSoftShadow(const QSize &pixels, const QPainterPath &path, qreal dpr,
                        qreal offsetY = 2.0, qreal blurSigma = 2.0);

} // namespace darkeye
