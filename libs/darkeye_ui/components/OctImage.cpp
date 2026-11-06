#include "darkeye_ui/components/OctImage.h"
#include "darkeye_ui/components/SoftShadow.h"

#include <QDir>
#include <QEvent>
#include <QEnterEvent>
#include <QMouseEvent>
#include <QImageReader>
#include <QFileInfo>
#include <QPainter>
#include <QMetaObject>
#include <QPointer>
#include <QResource>
#include <QThreadPool>

#include <cmath>

// Q_INIT_RESOURCE must declare the generated symbol in the global namespace.
static void ensureFallbackImageResourceInitialized()
{
    static const bool initialized = [] {
        Q_INIT_RESOURCE(darkeye_ui_resources);
        return true;
    }();
    Q_UNUSED(initialized);
}

namespace darkeye {

namespace {

constexpr auto kAnonymousImagePath = ":/icons/anonymous.jpg";
constexpr int kShadowExtension = 4;
constexpr int kShadowBlurPadding = 3;

QImage renderAvatar(const QImage &source, const QSize &size, const QPainterPath &path,
                    bool shadow, qreal dpr)
{
    const QSize pixels(qCeil(size.width() * dpr), qCeil(size.height() * dpr));
    QImage composite;
    if (shadow) {
        composite = renderSoftShadow(pixels, path, dpr, 2.0 + kShadowExtension, 3.0);
    } else {
        composite = QImage(pixels, QImage::Format_ARGB32_Premultiplied);
        composite.setDevicePixelRatio(dpr);
        composite.fill(Qt::transparent);
    }
    QImage face(pixels, QImage::Format_ARGB32_Premultiplied);
    face.setDevicePixelRatio(dpr);
    face.fill(Qt::transparent);
    {
        QPainter painter(&face);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.fillPath(path, Qt::white);
        // SourceIn preserves the antialiased alpha of the polygon boundary.
        painter.setCompositionMode(QPainter::CompositionMode_SourceIn);
        painter.setRenderHint(QPainter::SmoothPixmapTransform);
        const qreal side = qMin(source.width(), source.height());
        painter.drawImage(path.boundingRect(), source,
                           QRectF((source.width() - side) / 2,
                                  (source.height() - side) / 2, side, side));
    }
    QPainter painter(&composite);
    painter.drawImage(QPointF(0, 0), face);
    painter.end();
    return composite;
}

} // namespace

OctImage::OctImage(const QString &imagePath, const QString &basePath, int diameter,
                   bool shadow, QWidget *parent, bool deferImage)
    : QLabel(parent), m_diameter(qMax(16, diameter)), m_basePath(basePath), m_shadow(shadow),
      m_imageActive(!deferImage)
{
    setObjectName(QStringLiteral("DesignOctImage"));
    setFixedSize(m_diameter, m_diameter
                               + (m_shadow ? kShadowExtension + kShadowBlurPadding : 0));
    setAlignment(Qt::AlignCenter);
    setCursor(Qt::ArrowCursor);
    setMouseTracking(true);
    setAttribute(Qt::WA_TranslucentBackground);
    updateImage(imagePath);
}

QString OctImage::source() const { return m_source; }

void OctImage::setImageActive(bool active)
{
    if (m_imageActive == active) return;
    m_imageActive = active;
    ++m_request;
    if (active) {
        startLoad();
    } else {
        clear();
        m_renderCache = {};
        update();
    }
}

void OctImage::updateImage(const QString &imagePath)
{
    ensureFallbackImageResourceInitialized();
    m_source = resolvedPath(imagePath);
    if (m_source.isEmpty() || !QFileInfo::exists(m_source)) {
        m_source = QString::fromLatin1(kAnonymousImagePath);
    }
    ++m_request;
    clear();
    m_renderCache = {};
    startLoad();
}

QString OctImage::resolvedPath(const QString &imagePath) const
{
    if (imagePath.trimmed().isEmpty()) return {};
    if (QFileInfo(imagePath).isAbsolute() || m_basePath.isEmpty()) return imagePath;
    return QDir(m_basePath).filePath(imagePath);
}

void OctImage::startLoad()
{
    if (!m_imageActive || m_loading) return;
    m_loading = true;
    const quint64 request = m_request;
    const QString path = m_source;
    const qreal dpr = devicePixelRatioF();
    const QSize targetSize = size();
    const auto shape = avatarPath();
    const bool shadow = m_shadow;
    QPointer<OctImage> guard(this);
    QThreadPool::globalInstance()->start([guard, request, path, dpr, targetSize, shape, shadow] {
        QImageReader reader(path);
        reader.setAutoTransform(true);
        const QSize pixels(qCeil(targetSize.width() * dpr), qCeil(targetSize.height() * dpr));
        if (reader.size().isValid())
            reader.setScaledSize(reader.size().scaled(pixels, Qt::KeepAspectRatioByExpanding));
        QImage image = reader.read();
        // Some image handlers cannot downsample while decoding.
        if (!image.isNull())
            image = image.scaled(pixels, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
        const QImage rendered = image.isNull() ? QImage()
            : renderAvatar(image, targetSize, shape, shadow, dpr);
        if (guard.isNull()) return;
        QMetaObject::invokeMethod(guard, [guard, request, path, image, rendered] {
            if (!guard.isNull()) guard->applyImage(request, path, image, rendered);
        }, Qt::QueuedConnection);
    });
}

void OctImage::applyImage(quint64 request, const QString &path, const QImage &image,
                          const QImage &rendered)
{
    m_loading = false;
    if (request != m_request || path != m_source) {
        startLoad();
        return;
    }
    if (image.isNull()) {
        setText(QStringLiteral("无图片"));
        return;
    }
    setPixmap(QPixmap::fromImage(image));
    m_renderCache = QPixmap::fromImage(rendered);
    update();
    emit imageReady(image);
    emit imageLoaded(path);
}

bool OctImage::event(QEvent *event)
{
    if (event->type() == QEvent::Enter) {
        const auto *enterEvent = static_cast<QEnterEvent *>(event);
        setCursor(containsAvatarPoint(enterEvent->position())
                      ? Qt::PointingHandCursor : Qt::ArrowCursor);
    } else if (event->type() == QEvent::MouseMove) {
        const auto *mouseEvent = static_cast<QMouseEvent *>(event);
        setCursor(containsAvatarPoint(mouseEvent->position())
                      ? Qt::PointingHandCursor : Qt::ArrowCursor);
    } else if (event->type() == QEvent::Leave) {
        setCursor(Qt::ArrowCursor);
    }
    const bool handled = QLabel::event(event);
    if (event->type() == QEvent::DevicePixelRatioChange && !m_source.isEmpty()) {
        ++m_request;
        // Retain the previous bitmap until the replacement has been prepared.
        startLoad();
    }
    return handled;
}

QPainterPath OctImage::avatarPath() const
{
    // Keep the entire shadow inside the widget, including the downward offset.
    const qreal margin = m_shadow ? qMin(8.0, m_diameter / 8.0) : 0.0;
    const QRectF bounds = QRectF(0, 0, m_diameter, m_diameter)
                              .adjusted(margin, margin, -margin, -margin);
    const qreal cut = bounds.width() / (2.0 + std::sqrt(2.0));
    QPainterPath path;
    path.moveTo(bounds.left() + cut, bounds.top());
    path.lineTo(bounds.right() - cut, bounds.top());
    path.lineTo(bounds.right(), bounds.top() + cut);
    path.lineTo(bounds.right(), bounds.bottom() - cut);
    path.lineTo(bounds.right() - cut, bounds.bottom());
    path.lineTo(bounds.left() + cut, bounds.bottom());
    path.lineTo(bounds.left(), bounds.bottom() - cut);
    path.lineTo(bounds.left(), bounds.top() + cut);
    path.closeSubpath();
    return path;
}

bool OctImage::containsAvatarPoint(const QPointF &point) const
{
    return avatarPath().contains(point);
}

void OctImage::paintEvent(QPaintEvent *event)
{
    if (m_renderCache.isNull()) {
        QLabel::paintEvent(event);
        return;
    }
    QPainter painter(this);
    painter.drawPixmap(0, 0, m_renderCache);
}

} // namespace darkeye
