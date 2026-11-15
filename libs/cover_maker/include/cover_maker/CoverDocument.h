#pragma once
#include <QImage>
#include <QColor>
#include <QPointF>
#include <QRectF>
#include <QString>
#include <array>
#include <QFont>
#include <QVector>
#include <QTransform>
#include <QPainterPath>

class QPainter;

namespace darkeye::cover_maker {
enum class Layout { Single, Split, Inset };
struct ImageSlot {
    QImage image;
    // Normalized travel within the cropped image; (0.5, 0.5) centers it.
    QPointF position{0.5, 0.5};
    qreal zoom = 1.0;
    bool operator==(const ImageSlot &) const = default;
};
struct ImageLayer {
    QImage image;
    QString name;
    QRectF frame{0.1, 0.1, 0.8, 0.8}; // Normalized destination in the panel.
    int z = 0;
    bool visible = true;
    bool locked = false;
    bool operator==(const ImageLayer &) const = default;
    QRectF crop{0, 0, 1, 1}; // Normalized non-destructive source crop.
};
struct TextLayer {
    int z = 100000;
    bool visible = true;
    bool locked = false;
    bool stroke = false;
    int strokeWidth = 2;
    QColor strokeColor = Qt::black;
    bool shadow = false;
    QPointF shadowOffset{4,4};
    QColor shadowColor{0,0,0,180};
    bool background = false;
    QColor textBackground{0,0,0,160};
    bool operator==(const TextLayer &) const = default;
    QString text = QStringLiteral("新文字");
    QPointF center{0.5, 0.5}; // Normalized panel coordinates.
    QString fontFamily;
    int fontSize = 72; // Pixels at export resolution.
    bool bold = false;
    QColor color = Qt::white;
    qreal rotation = 0;
    int horizontalSpacing = 0; // Extra horizontal tracking in export pixels.
    int verticalSpacing = 0; // Extra pixels between upright vertical graphemes.
    bool vertical = false; // Upright graphemes, top-to-bottom; newlines start columns to the left.
};
struct CoverDocument {
    bool operator==(const CoverDocument &) const = default;
    std::array<int,3> sleeveWidths{};
    QSize outputSize{1200, 1800};
    Layout layout = Layout::Single;
    std::array<ImageSlot, 2> imageSlots;
    QVector<ImageLayer> imageLayers;
    bool editableImageLayers = false;
    QVector<TextLayer> textLayers;
    bool editableTextLayers = false;
    // Legacy metadata seeds editable layers once, preserving existing documents.
    QString title;
    QString subtitle;
    bool textAtTop = false;
    bool spineText = false;
    bool showImagePlaceholders = true;
    QColor backgroundColor{QStringLiteral("#243041")};
};
struct LayerRef { bool text = false; int index = -1; bool operator==(const LayerRef &) const = default; };
QVector<LayerRef> orderedLayers(const CoverDocument &document);
int nextLayerZ(const CoverDocument &document);
void normalizeLayerOrder(CoverDocument &document);
QRectF imageLayerRect(const ImageLayer &layer, const QSize &size);
QRectF imageLayerSource(const ImageLayer &layer);
ImageLayer makeImageLayer(const QImage &image, const QSize &size, const QString &name = {});
void enableImageLayers(CoverDocument &document);
QVector<TextLayer> resolvedTextLayers(const CoverDocument &document);
void enableTextLayers(CoverDocument &document);
QFont textLayerFont(const TextLayer &layer);
QPainterPath textLayerShape(const TextLayer &layer);
QRectF textLayerBounds(const TextLayer &layer, const QSize &size);
QRectF textLayerRect(const TextLayer &layer);
QTransform textLayerTransform(const TextLayer &layer, const QSize &size);
QRectF slotRect(Layout layout, int index, const QSizeF &size);
QRectF imageRect(const ImageSlot &slot, const QRectF &frame);
// Preview and export share the same coordinate system, including typography.
void paintCover(QPainter &painter, const QRectF &target, const CoverDocument &document);
QImage renderCover(const CoverDocument &document);
bool saveCover(const CoverDocument &document, const QString &path, QString *error = nullptr);
}
