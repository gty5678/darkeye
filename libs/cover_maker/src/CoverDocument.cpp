#include "cover_maker/CoverDocument.h"
#include <QPainter>
#include <QPainterPath>
#include <QPainterPathStroker>
#include <QGuiApplication>
#include <QSaveFile>
#include <QFontMetricsF>
#include <QTextBoundaryFinder>
#include <algorithm>
#include <cmath>

namespace darkeye::cover_maker {
QVector<LayerRef> orderedLayers(const CoverDocument &doc)
{
    QVector<LayerRef> result;
    for(int i=0;i<doc.imageLayers.size();++i)result.append({false,i});
    for(int i=0;i<doc.textLayers.size();++i)result.append({true,i});
    std::stable_sort(result.begin(),result.end(),[&](auto a,auto b){
        return (a.text?doc.textLayers[a.index].z:doc.imageLayers[a.index].z)<(b.text?doc.textLayers[b.index].z:doc.imageLayers[b.index].z);
    });return result;
}
int nextLayerZ(const CoverDocument &doc)
{
    int z=0;for(auto ref:orderedLayers(doc))z=std::max(z,(ref.text?doc.textLayers[ref.index].z:doc.imageLayers[ref.index].z)+1);return z;
}
void normalizeLayerOrder(CoverDocument &doc)
{
    int z=0;for(auto ref:orderedLayers(doc)) {if(ref.text)doc.textLayers[ref.index].z=z++;else doc.imageLayers[ref.index].z=z++;}
}
QRectF imageLayerRect(const ImageLayer &layer, const QSize &size)
{
    return {layer.frame.x() * size.width(), layer.frame.y() * size.height(),
            layer.frame.width() * size.width(), layer.frame.height() * size.height()};
}
QRectF imageLayerSource(const ImageLayer &layer)
{
    const auto crop = layer.crop.normalized().intersected(QRectF(0,0,1,1));
    return {crop.x() * layer.image.width(), crop.y() * layer.image.height(),
            crop.width() * layer.image.width(), crop.height() * layer.image.height()};
}
ImageLayer makeImageLayer(const QImage &image, const QSize &size, const QString &name)
{
    ImageLayer layer; layer.image = image; layer.name = name;
    if (image.isNull() || size.isEmpty()) return layer;
    QSizeF fitted(image.size()); fitted.scale(QSizeF(size) * .8, Qt::KeepAspectRatio);
    layer.frame = {(1 - fitted.width() / size.width()) / 2, (1 - fitted.height() / size.height()) / 2,
                   fitted.width() / size.width(), fitted.height() / size.height()};
    return layer;
}
void enableImageLayers(CoverDocument &doc)
{
    if (doc.editableImageLayers) return;
    for (int i = 0; i < 2; ++i) {
        const auto &slot = doc.imageSlots[i]; const auto frame = slotRect(doc.layout, i, QSizeF(doc.outputSize));
        if (slot.image.isNull() || frame.isEmpty()) continue;
        const auto image = imageRect(slot, frame);
        ImageLayer layer; layer.image = slot.image; layer.name = QStringLiteral("图片 %1").arg(i+1);
        layer.frame = {frame.x() / doc.outputSize.width(), frame.y() / doc.outputSize.height(),
                       frame.width() / doc.outputSize.width(), frame.height() / doc.outputSize.height()};
        layer.crop = {(frame.x()-image.x()) / image.width(), (frame.y()-image.y()) / image.height(),
                      frame.width() / image.width(), frame.height() / image.height()};
        doc.imageLayers.append(layer);
    }
    doc.editableImageLayers = true;
}
QFont textLayerFont(const TextLayer &layer)
{
    QFont font = QGuiApplication::font();
    if (!layer.fontFamily.isEmpty()) font.setFamily(layer.fontFamily);
    font.setPixelSize(std::clamp(layer.fontSize, 6, 1000));
    font.setBold(layer.bold);
    if(!layer.vertical && layer.horizontalSpacing!=0)
        font.setLetterSpacing(QFont::AbsoluteSpacing,std::clamp(layer.horizontalSpacing,-1000,1000));
    return font;
}
namespace {
QVector<QStringList> verticalColumns(const QString &text)
{
    QVector<QStringList> columns;
    for (const auto &line : text.split(u'\n')) {
        QStringList glyphs;
        QTextBoundaryFinder finder(QTextBoundaryFinder::Grapheme, line);
        int start = 0;
        for (int end = finder.toNextBoundary(); end != -1; end = finder.toNextBoundary()) {
            glyphs.append(line.mid(start, end - start)); start = end;
        }
        columns.append(glyphs);
    }
    return columns;
}
qreal verticalAdvance(const TextLayer &layer)
{
    // Upright characters use one em by default, without the font's horizontal line leading.
    return std::max(1, std::clamp(layer.fontSize, 6, 1000) + std::clamp(layer.verticalSpacing, -1000, 1000));
}
QSizeF verticalCell(const TextLayer &layer)
{
    const QFontMetricsF metrics(textLayerFont(layer));
    qreal width = metrics.height();
    for (const auto &column : verticalColumns(layer.text))
        for (const auto &glyph : column) width = std::max(width, metrics.horizontalAdvance(glyph));
    return {width, metrics.lineSpacing()};
}
struct TextGeometry {
    QPainterPath glyphs;
    QPainterPath background;
    QPainterPath visual;
};
TextGeometry textGeometry(const TextLayer &layer)
{
    TextGeometry result;
    // Font contours can overlap at stroke joins. Odd-even fill punches holes there;
    // non-zero winding preserves joins while keeping genuine counter-shapes (B, 8, 田).
    result.glyphs.setFillRule(Qt::WindingFill);
    result.background.setFillRule(Qt::WindingFill);
    result.visual.setFillRule(Qt::WindingFill);
    const auto font=textLayerFont(layer);const QFontMetricsF metrics(font);
    if(layer.vertical) {
        const auto columns=verticalColumns(layer.text);const auto cell=verticalCell(layer);
        for(int column=0;column<columns.size();++column)for(int row=0;row<columns[column].size();++row) {
            QPainterPath glyph;glyph.setFillRule(Qt::WindingFill);glyph.addText(0,0,font,columns[column][row]);
            if(glyph.isEmpty())continue;
            const auto center=glyph.boundingRect().center();
            glyph.translate(-column*cell.width()-center.x(),row*verticalAdvance(layer)-center.y());
            result.glyphs.addPath(glyph);
        }
    } else {
        int row=0;for(const auto &line:layer.text.split(u'\n'))result.glyphs.addText(0,row++*metrics.lineSpacing(),font,line);
    }
    if(result.glyphs.isEmpty())return result;
    result.visual=result.glyphs;
    if(layer.stroke && layer.strokeColor.alpha()>0) {
        QPainterPathStroker stroker;stroker.setWidth(std::clamp(layer.strokeWidth,1,100)*2);stroker.setJoinStyle(Qt::RoundJoin);
        result.visual.addPath(stroker.createStroke(result.glyphs));
    }
    if(layer.shadow && layer.shadowColor.alpha()>0)result.visual.addPath(result.glyphs.translated(layer.shadowOffset));
    if(layer.background && layer.textBackground.alpha()>0) {
        result.background.addRect(result.glyphs.boundingRect().adjusted(-6,-6,6,6));result.visual.addPath(result.background);
    }
    // Normalize the actual visible outline, not the font's line box or advance width.
    const auto center=result.visual.boundingRect().center();
    result.glyphs.translate(-center);result.background.translate(-center);result.visual.translate(-center);
    return result;
}
}
QPainterPath textLayerShape(const TextLayer &layer) {return textGeometry(layer).visual;}
QRectF textLayerRect(const TextLayer &layer)
{
    const auto shape=textLayerShape(layer);
    return shape.isEmpty()?QRectF(-.5,-.5,1,1):shape.boundingRect();
}
QRectF textLayerBounds(const TextLayer &layer,const QSize &size)
{
    const auto shape=textLayerShape(layer);
    if(shape.isEmpty())return textLayerTransform(layer,size).mapRect(textLayerRect(layer));
    return textLayerTransform(layer,size).map(shape).boundingRect();
}
QTransform textLayerTransform(const TextLayer &layer, const QSize &size)
{
    QTransform transform;
    transform.translate(layer.center.x() * size.width(), layer.center.y() * size.height());
    transform.rotate(layer.rotation); return transform;
}
QVector<TextLayer> resolvedTextLayers(const CoverDocument &doc)
{
    if (doc.editableTextLayers) return doc.textLayers;
    QVector<TextLayer> layers;
    if (doc.spineText) {
        if (!doc.title.isEmpty() || !doc.subtitle.isEmpty()) {
            TextLayer layer; layer.text = doc.subtitle.isEmpty() ? doc.title : doc.title + QStringLiteral(" · ") + doc.subtitle;
            layer.fontSize = std::max(6, qRound(doc.outputSize.width() * .38));
            layer.rotation = 90; layer.bold = true; layers.append(layer);
        }
    } else {
        if (!doc.title.isEmpty()) {
            TextLayer layer; layer.text = doc.title; layer.bold = true;
            layer.fontSize = std::max(6, qRound(doc.outputSize.width() * .065));
            layer.center = {.5, doc.textAtTop ? .08 : .8}; layers.append(layer);
        }
        if (!doc.subtitle.isEmpty()) {
            TextLayer layer; layer.text = doc.subtitle;
            layer.fontSize = std::max(6, qRound(doc.outputSize.width() * .03));
            layer.center = {.5, doc.textAtTop ? .2 : .94}; layers.append(layer);
        }
    }
    return layers;
}
void enableTextLayers(CoverDocument &doc)
{
    if (doc.editableTextLayers) return;
    doc.textLayers = resolvedTextLayers(doc); doc.editableTextLayers = true;
}
QRectF slotRect(Layout layout, int index, const QSizeF &size)
{
    const qreal w = size.width(), h = size.height();
    if (index < 0 || index > 1 || (index == 1 && layout == Layout::Single)) return {};
    if (layout == Layout::Split) return {0, index * h / 2, w, h / 2};
    if (layout == Layout::Inset && index == 1) return {w * .57, h * .08, w * .36, h * .34};
    return {0, 0, w, h};
}
QRectF imageRect(const ImageSlot &slot, const QRectF &frame)
{
    if (slot.image.isNull() || frame.isEmpty()) return {};
    const qreal zoom = std::isfinite(slot.zoom) ? std::clamp(slot.zoom, 1., 5.) : 1.;
    const qreal scale = std::max(frame.width() / slot.image.width(), frame.height() / slot.image.height()) * zoom;
    const QSizeF size = QSizeF(slot.image.size()) * scale;
    return {frame.x() - (size.width() - frame.width()) * std::clamp(slot.position.x(), 0., 1.),
            frame.y() - (size.height() - frame.height()) * std::clamp(slot.position.y(), 0., 1.),
            size.width(), size.height()};
}
namespace {
void paintTextLayer(QPainter &p,const CoverDocument &doc,const TextLayer &layer)
{
    if(!layer.visible || layer.text.isEmpty())return;
    const auto geometry=textGeometry(layer);
    p.save();p.setTransform(textLayerTransform(layer,doc.outputSize),true);
    if(layer.background)p.fillPath(geometry.background,layer.textBackground);
    if(layer.shadow)p.fillPath(geometry.glyphs.translated(layer.shadowOffset),layer.shadowColor);
    if(layer.stroke)p.strokePath(geometry.glyphs,QPen(layer.strokeColor,std::clamp(layer.strokeWidth,1,100)*2,Qt::SolidLine,Qt::RoundCap,Qt::RoundJoin));
    p.fillPath(geometry.glyphs,layer.color);p.restore();
}
}
void paintCover(QPainter &p, const QRectF &target, const CoverDocument &doc)
{
    if (target.isEmpty() || doc.outputSize.isEmpty()) return;
    p.save();
    p.setClipRect(target, Qt::IntersectClip);
    p.translate(target.topLeft());
    p.scale(target.width() / doc.outputSize.width(), target.height() / doc.outputSize.height());
    const QSizeF size(doc.outputSize);
    const QRectF canvas(QPointF(), size);
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    p.setRenderHint(QPainter::Antialiasing);
    p.fillRect(canvas, doc.backgroundColor);
    if(doc.editableImageLayers && doc.editableTextLayers) {
        for(auto ref:orderedLayers(doc)) {
            if(ref.text)paintTextLayer(p,doc,doc.textLayers[ref.index]);
            else {const auto &layer=doc.imageLayers[ref.index];const auto source=imageLayerSource(layer);
                if(layer.visible && !layer.image.isNull() && !source.isEmpty())p.drawImage(imageLayerRect(layer,doc.outputSize),layer.image,source);}
        }
    } else {
        if(doc.editableImageLayers) {
            for(const auto &layer:doc.imageLayers)if(layer.visible)p.drawImage(imageLayerRect(layer,doc.outputSize),layer.image,imageLayerSource(layer));
        } else {
    for (int i = 0; i < 2; ++i) {
        const auto frame = slotRect(doc.layout, i, size);
        if (frame.isEmpty()) continue;
        p.save();
        p.setClipRect(frame, Qt::IntersectClip);
        if (doc.imageSlots[i].image.isNull()) {
            p.fillRect(frame, doc.backgroundColor);
            if (doc.showImagePlaceholders) {
                QFont f; f.setPixelSize(qRound(size.width() * .035)); p.setFont(f);
                p.setPen(QColor("#b7c3d2"));
                p.drawText(frame, Qt::AlignCenter, QStringLiteral("图片 %1").arg(i + 1));
            }
        } else p.drawImage(imageRect(doc.imageSlots[i], frame), doc.imageSlots[i].image);
        p.restore();
    }
        }
        for(const auto &layer:resolvedTextLayers(doc))paintTextLayer(p,doc,layer);
    }
    p.restore();
}
QImage renderCover(const CoverDocument &doc)
{
    // Bound allocations for callers as well as the editor's presets.
    if (doc.outputSize.isEmpty() || doc.outputSize.width() > 8192 || doc.outputSize.height() > 8192) return {};
    QImage image(doc.outputSize, QImage::Format_ARGB32_Premultiplied);
    if (image.isNull()) return {};
    image.fill(Qt::transparent);
    QPainter painter(&image);
    paintCover(painter, QRectF(QPointF(), QSizeF(image.size())), doc);
    return image;
}
bool saveCover(const CoverDocument &doc, const QString &path, QString *error)
{
    QSaveFile file(path);
    const QImage image = renderCover(doc);
    if (image.isNull()) { if (error) *error = QStringLiteral("无效的输出尺寸或内存不足"); return false; }
    if (!file.open(QIODevice::WriteOnly) || !image.save(&file, "PNG") || !file.commit()) {
        if (error) *error = file.errorString();
        return false;
    }
    return true;
}
}
