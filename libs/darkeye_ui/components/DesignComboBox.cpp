#include "darkeye_ui/components/DesignComboBox.h"

#include <QAbstractItemView>
#include <QPainter>
#include <QStyle>
#include <QStyleOptionComboBox>

namespace darkeye {

DesignComboBox::DesignComboBox(QWidget *parent) : QComboBox(parent)
{
    setObjectName(QStringLiteral("DesignComboBox"));
    setProperty("darkeyeStyle", QStringLiteral("DesignComboBox"));
    view()->setObjectName(QStringLiteral("DesignComboBoxPopup"));
}

void DesignComboBox::paintEvent(QPaintEvent *event)
{
    QComboBox::paintEvent(event);

    QStyleOptionComboBox option;
    initStyleOption(&option);
    const QRect arrowRect = style()->subControlRect(QStyle::CC_ComboBox, &option,
                                                    QStyle::SC_ComboBoxArrow, this);
    const QSize iconSize(16, 16);
    const QRect iconRect(QPoint(arrowRect.center().x() - iconSize.width() / 2,
                                arrowRect.center().y() - iconSize.height() / 2),
                         iconSize);

    QPainter painter(this);
    const auto colorGroup = isEnabled() ? QPalette::Active : QPalette::Disabled;
    painter.setPen(QPen(option.palette.color(colorGroup, QPalette::Text), 2.0,
                        Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));

    // Matches darkeye_ui's SVG_ARROW_DOWN: a 16 px arrow with a vertical stem.
    const qreal centerX = iconRect.center().x() + 0.5;
    painter.drawLine(QPointF(centerX, iconRect.top() + 3),
                     QPointF(centerX, iconRect.top() + 12));
    painter.drawPolyline(QPolygonF({QPointF(iconRect.left() + 4, iconRect.top() + 9),
                                    QPointF(centerX, iconRect.top() + 13),
                                    QPointF(iconRect.right() - 3, iconRect.top() + 9)}));
}

} // namespace darkeye
