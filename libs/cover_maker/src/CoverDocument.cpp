#include "cover_maker/CoverDocument.h"
#include <QPainter>
#include <QGuiApplication>
#include <QSaveFile>
#include <QFontMetricsF>
#include <QTextBoundaryFinder>
#include <algorithm>
#include <cmath>

namespace darkeye::cover_maker {
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
    font.setBold(layer.bold); return font;
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
QSizeF verticalCell(const TextLayer &layer)
{
    const QFontMetricsF metrics(textLayerFont(layer));
    qreal width = metrics.height();
    for (const auto &column : verticalColumns(layer.text))
        for (const auto &glyph : column) width = std::max(width, metrics.horizontalAdvance(glyph));
    return {width, metrics.lineSpacing()};
}
}
QRectF textLayerRect(const TextLayer &layer)
{
    const QFontMetricsF metrics(textLayerFont(layer));
    if (layer.vertical) {
        const auto columns = verticalColumns(layer.text);
        const auto cell = verticalCell(layer);
        int rows = 1;
        for (const auto &column : columns) rows = std::max(rows, int(column.size()));
        const qreal width = columns.size() * cell.width(), height = rows * cell.height();
        return {-width / 2, -height / 2, width, height};
    }
    const auto lines = layer.text.split(u'\n');
    qreal width = 1;
    for (const auto &line : lines) width = std::max(width, metrics.horizontalAdvance(line));
    // A small margin also includes glyph overhangs in selection and hit testing.
    width += metrics.height() * .2;
    const qreal height = metrics.lineSpacing() * lines.size();
    return {-width / 2, -height / 2, width, height};
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
    if (doc.editableImageLayers) {
        for (const auto &layer : doc.imageLayers) {
            const auto source = imageLayerSource(layer);
            if (!layer.image.isNull() && !source.isEmpty()) p.drawImage(imageLayerRect(layer, doc.outputSize), layer.image, source);
        }
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
    for (const auto &layer : resolvedTextLayers(doc)) {
        if (layer.text.isEmpty()) continue;
        p.save();
        p.setTransform(textLayerTransform(layer, doc.outputSize), true);
        p.setFont(textLayerFont(layer)); p.setPen(layer.color);
        if (layer.vertical) {
            const auto columns = verticalColumns(layer.text);
            const auto cell = verticalCell(layer);
            const auto bounds = textLayerRect(layer);
            for (int column = 0; column < columns.size(); ++column) {
                for (int row = 0; row < columns[column].size(); ++row) {
                    const QRectF box(bounds.right() - (column + 1) * cell.width(), bounds.top() + row * cell.height(),
                                     cell.width(), cell.height());
                    p.drawText(box, Qt::AlignCenter | Qt::TextDontClip, columns[column][row]);
                }
            }
        } else p.drawText(textLayerRect(layer), Qt::AlignLeft | Qt::AlignTop | Qt::TextDontClip, layer.text);
        p.restore();
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
