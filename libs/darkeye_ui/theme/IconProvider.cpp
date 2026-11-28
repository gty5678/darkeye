#include "darkeye_ui/theme/IconProvider.h"
#include "darkeye_ui/UiResources.h"

#include <QApplication>
#include <QFile>
#include <QImage>
#include <QPainter>
#include <QPixmapCache>
#include <QScreen>
#include <QSvgRenderer>

namespace darkeye {
namespace {

QString builtInPath(const QString &name)
{
    QString normalized = name.trimmed().toLower();
    if (normalized.isEmpty() || normalized.contains(QLatin1Char('/'))
        || normalized.contains(QLatin1Char('\\'))) return {};
    if (normalized == QStringLiteral("refresh")) normalized = QStringLiteral("refresh_cw");
    if (normalized == QStringLiteral("circle_question"))
        normalized = QStringLiteral("circle_question_mark");
    return QStringLiteral(":/darkeye_ui/icons/") + normalized + QStringLiteral(".svg");
}

qreal effectiveRatio(qreal requested)
{
    if (requested > 0.0) return requested;
    if (QApplication::primaryScreen() != nullptr) {
        return QApplication::primaryScreen()->devicePixelRatio();
    }
    return 1.0;
}

QPixmap renderSvg(const QString &source, const QSize &size, const QColor &color, qreal ratio)
{
    QString svg = source;
    if (!source.trimmed().startsWith(QLatin1Char('<'))) {
        QFile file(source);
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) return {};
        svg = QString::fromUtf8(file.readAll());
    }
    if (color.isValid()) svg.replace(QStringLiteral("currentColor"), color.name());

    QSvgRenderer renderer(svg.toUtf8());
    if (!renderer.isValid()) return {};
    const QSize pixels(qMax(1, qRound(size.width() * ratio)),
                       qMax(1, qRound(size.height() * ratio)));
    QImage image(pixels, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    QPainter painter(&image);
    renderer.render(&painter);
    painter.end();
    QPixmap pixmap = QPixmap::fromImage(image);
    pixmap.setDevicePixelRatio(ratio);
    return pixmap;
}

} // namespace

QIcon IconProvider::builtIn(const QString &name, const QSize &size,
                           const QColor &color, qreal devicePixelRatio)
{
    const QString path = builtInPath(name);
    if (path.isEmpty() || size.isEmpty()) return {};
    initializeUiResources();
    const qreal ratio = effectiveRatio(devicePixelRatio);
    const QString key = QStringLiteral("darkeye-ui:%1x%2@%3:%4:")
        .arg(size.width()).arg(size.height()).arg(ratio, 0, 'g', 17)
        .arg(color.isValid() ? color.name() : QString()) + path;
    QPixmap pixmap;
    if (!QPixmapCache::find(key, &pixmap)) {
        pixmap = renderSvg(path, size, color, ratio);
        if (pixmap.isNull()) return {};
        QPixmapCache::insert(key, pixmap);
    }
    return QIcon(pixmap);
}

QIcon IconProvider::fromSvg(const QString &source, const QSize &size,
                           const QColor &color, qreal devicePixelRatio)
{
    if (size.isEmpty()) return {};
    const QPixmap pixmap = renderSvg(source, size, color, effectiveRatio(devicePixelRatio));
    return pixmap.isNull() ? QIcon() : QIcon(pixmap);
}

bool IconProvider::contains(const QString &name)
{
    initializeUiResources();
    const QString path = builtInPath(name);
    return !path.isEmpty() && QFile::exists(path);
}

} // namespace darkeye

