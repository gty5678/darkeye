#pragma once
#include "CoverDocument.h"

namespace darkeye::cover_maker {
// Left to right: back, spine, front. Pixel dimensions specify exact proportions.
struct SleeveDocument {
    // Current libs/dvd/dvd.glb cover UV seams: u=0.4766346 and 0.5252864.
    // Use 4x the existing 800x540 placeholder canvas; round seams to pixels.
    int backWidth = 1525;
    int spineWidth = 156;
    int frontWidth = 1519;
    int height = 2160;
    std::array<CoverDocument, 3> panels;
    QSize outputSize() const { return {backWidth + spineWidth + frontWidth, height}; }
};
bool validSleeve(const SleeveDocument &document);
QRectF panelRect(const SleeveDocument &document, int index);
void paintSleeve(QPainter &painter, const QRectF &target, const SleeveDocument &document);
QImage renderSleeve(const SleeveDocument &document);
bool saveSleeve(const SleeveDocument &document, const QString &path, QString *error = nullptr);
}
