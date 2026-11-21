#pragma once
#include <QPainterPath>
#include <QRectF>
#include <QtGlobal>

namespace darkeye {
inline QPainterPath chamferPath(const QRectF &rect, qreal corner = 6)
{
    const qreal c = qBound(qreal(0), corner, qMin(rect.width(), rect.height()) / 2);
    QPainterPath path;
    path.moveTo(rect.left() + c, rect.top());
    path.lineTo(rect.right() - c, rect.top());
    path.lineTo(rect.right(), rect.top() + c);
    path.lineTo(rect.right(), rect.bottom() - c);
    path.lineTo(rect.right() - c, rect.bottom());
    path.lineTo(rect.left() + c, rect.bottom());
    path.lineTo(rect.left(), rect.bottom() - c);
    path.lineTo(rect.left(), rect.top() + c);
    path.closeSubpath();
    return path;
}
} // namespace darkeye
