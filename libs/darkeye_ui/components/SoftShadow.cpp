#include "darkeye_ui/components/SoftShadow.h"

#include <QCache>
#include <QDataStream>
#include <QIODevice>
#include <QMutex>
#include <QMutexLocker>
#include <QPainter>

#include <cmath>
#include <vector>

namespace darkeye {

// Bound the cache and serialize misses so matching shapes are blurred once.
// Call from a loading worker; painting should only consume the returned image.
QImage renderSoftShadow(const QSize &pixels, const QPainterPath &path, qreal dpr,
                        qreal offsetY, qreal blurSigma)
{
    static QMutex mutex;
    static QCache<QByteArray, QImage> cache(8192); // KiB
    QByteArray key;
    QDataStream stream(&key, QIODevice::WriteOnly);
    stream << pixels << dpr << path << offsetY << blurSigma;
    QMutexLocker lock(&mutex);
    if (const auto *cached = cache.object(key)) return *cached;
    QImage composite(pixels, QImage::Format_ARGB32_Premultiplied);
    composite.setDevicePixelRatio(dpr);
    composite.fill(Qt::transparent);
    QImage silhouette(pixels, QImage::Format_ARGB32_Premultiplied);
    silhouette.setDevicePixelRatio(dpr);
    silhouette.fill(Qt::transparent);
    {
        QPainter painter(&silhouette);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.translate(0, offsetY);
        painter.fillPath(path, Qt::black);
    }
    // Separable Gaussian in physical pixels keeps the blur consistent
    // at every scale factor.
    const qreal sigma = blurSigma * dpr;
    const int radius = qCeil(3 * sigma);
    std::vector<qreal> kernel(2 * radius + 1);
    qreal total = 0;
    for (int i = -radius; i <= radius; ++i) {
        total += kernel[i + radius] = std::exp(-i * i / (2 * sigma * sigma));
    }
    for (auto &weight : kernel) weight /= total;
    std::vector<qreal> horizontal(pixels.width() * pixels.height());
    for (int y = 0; y < pixels.height(); ++y) {
        const auto *row = reinterpret_cast<const QRgb *>(silhouette.constScanLine(y));
        for (int x = 0; x < pixels.width(); ++x) {
            qreal alpha = 0;
            for (int k = -radius; k <= radius; ++k) {
                if (x + k >= 0 && x + k < pixels.width())
                    alpha += qAlpha(row[x + k]) * kernel[k + radius];
            }
            horizontal[y * pixels.width() + x] = alpha;
        }
    }
    for (int y = 0; y < pixels.height(); ++y) {
        auto *row = reinterpret_cast<QRgb *>(composite.scanLine(y));
        for (int x = 0; x < pixels.width(); ++x) {
            qreal alpha = 0;
            for (int k = -radius; k <= radius; ++k) {
                if (y + k >= 0 && y + k < pixels.height())
                    alpha += horizontal[(y + k) * pixels.width() + x] * kernel[k + radius];
            }
            row[x] = qRgba(0, 0, 0, qRound(alpha * 80 / 255));
        }
    }
    cache.insert(key, new QImage(composite), qMax(1, int(composite.sizeInBytes() / 1024)));
    return composite;
}

} // namespace darkeye
