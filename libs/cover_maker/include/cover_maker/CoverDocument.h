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

class QPainter;

namespace darkeye::cover_maker {
enum class Layout { Single, Split, Inset };
struct ImageSlot {
    QImage image;
    // Normalized travel within the cropped image; (0.5, 0.5) centers it.
    QPointF position{0.5, 0.5};
    qreal zoom = 1.0;
};
struct ImageLayer {
    QImage image;
    QString name;
    QRectF frame{0.1, 0.1, 0.8, 0.8}; // Normalized destination in the panel.
    QRectF crop{0, 0, 1, 1}; // Normalized non-destructive source crop.
};
struct TextLayer {
    QString text = QStringLiteral("新文字");
    QPointF center{0.5, 0.5}; // Normalized panel coordinates.
    QString fontFamily;
    int fontSize = 72; // Pixels at export resolution.
    bool bold = false;
    QColor color = Qt::white;
    qreal rotation = 0;
    bool vertical = false; // Upright graphemes, top-to-bottom; newlines start columns to the left.
};
struct CoverDocument {
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
QRectF imageLayerRect(const ImageLayer &layer, const QSize &size);
QRectF imageLayerSource(const ImageLayer &layer);
ImageLayer makeImageLayer(const QImage &image, const QSize &size, const QString &name = {});
void enableImageLayers(CoverDocument &document);
QVector<TextLayer> resolvedTextLayers(const CoverDocument &document);
void enableTextLayers(CoverDocument &document);
QFont textLayerFont(const TextLayer &layer);
QRectF textLayerRect(const TextLayer &layer);
QTransform textLayerTransform(const TextLayer &layer, const QSize &size);
QRectF slotRect(Layout layout, int index, const QSizeF &size);
QRectF imageRect(const ImageSlot &slot, const QRectF &frame);
// Preview and export share the same coordinate system, including typography.
void paintCover(QPainter &painter, const QRectF &target, const CoverDocument &document);
QImage renderCover(const CoverDocument &document);
bool saveCover(const CoverDocument &document, const QString &path, QString *error = nullptr);
}
