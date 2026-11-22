#include "cover_maker/SleeveDocument.h"
#include <QPainter>
#include <QSaveFile>
namespace darkeye::cover_maker {
bool validSleeve(const SleeveDocument &d)
{
    return d.backWidth > 0 && d.backWidth <= 8192 && d.spineWidth > 0 && d.spineWidth <= 8192
        && d.frontWidth > 0 && d.frontWidth <= 8192 && d.height > 0 && d.height <= 8192
        && d.outputSize().width() <= 8192;
}
QRectF panelRect(const SleeveDocument &d, int i)
{
    if (!validSleeve(d) || i < 0 || i > 2) return {};
    const int x[] = {0, d.backWidth, d.backWidth + d.spineWidth};
    const int widths[] = {d.backWidth, d.spineWidth, d.frontWidth};
    return {qreal(x[i]), 0, qreal(widths[i]), qreal(d.height)};
}
CoverDocument unifiedComposition(const SleeveDocument &d)
{
    if (d.unified) { auto composition = d.composition; composition.outputSize = d.outputSize(); return composition; }
    CoverDocument composition; composition.outputSize = d.outputSize(); composition.editableImageLayers = true;
    composition.editableTextLayers = true; composition.showImagePlaceholders = false;
    if (!validSleeve(d)) return composition;
    for (int i = 0; i < 3; ++i) {
        auto panel = d.panels[i]; const auto region = panelRect(d,i);
        panel.outputSize = region.size().toSize(); panel.spineText = i == 1;
        enableImageLayers(panel); enableTextLayers(panel);
        const auto mapFrame = [&](const QRectF &frame) {
            return QRectF((region.x()+frame.x()*region.width())/d.outputSize().width(),frame.y(),
                          frame.width()*region.width()/d.outputSize().width(),frame.height());
        };
        // Only keep separate colored backgrounds when the legacy colors differ.
        if (panel.backgroundColor != d.panels[0].backgroundColor) {
            QImage color(1,1,QImage::Format_ARGB32_Premultiplied); color.fill(panel.backgroundColor);
            ImageLayer background; background.image=color; background.name=QStringLiteral("区域底色 %1").arg(i+1);
            background.frame=mapFrame(QRectF(0,0,1,1)); composition.imageLayers.append(background);
        }
        for (auto layer : panel.imageLayers) { layer.frame=mapFrame(layer.frame); composition.imageLayers.append(layer); }
        for (auto layer : panel.textLayers) {
            layer.center.setX((region.x()+layer.center.x()*region.width())/d.outputSize().width());
            composition.textLayers.append(layer);
        }
    }
    composition.backgroundColor=d.panels[0].backgroundColor;
    return composition;
}
void paintSleeve(QPainter &p, const QRectF &target, const SleeveDocument &d)
{
    if (!validSleeve(d) || target.isEmpty()) return;
    if (d.unified) { paintCover(p,target,unifiedComposition(d)); return; }
    p.save(); p.setClipRect(target, Qt::IntersectClip); p.translate(target.topLeft());
    p.scale(target.width() / d.outputSize().width(), target.height() / d.height);
    for (int i = 0; i < 3; ++i) {
        CoverDocument panel = d.panels[i];
        panel.spineText = i == 1; panel.showImagePlaceholders = false;
        panel.outputSize = panelRect(d, i).size().toSize();
        paintCover(p, panelRect(d, i), panel);
    }
    p.restore();
}
QImage renderSleeve(const SleeveDocument &d)
{
    if (!validSleeve(d)) return {};
    QImage image(d.outputSize(), QImage::Format_ARGB32_Premultiplied);
    if (image.isNull()) return {};
    image.fill(Qt::transparent); QPainter p(&image);
    paintSleeve(p, QRectF(QPointF(), QSizeF(image.size())), d);
    return image;
}
bool saveSleeve(const SleeveDocument &d, const QString &path, QString *error)
{
    const auto image = renderSleeve(d);
    if (image.isNull()) { if (error) *error = QStringLiteral("尺寸无效或内存不足；总宽及高度不能超过 8192 像素。"); return false; }
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || !image.save(&file, "PNG") || !file.commit()) {
        if (error) *error = file.errorString();
        return false;
    }
    return true;
}
}
