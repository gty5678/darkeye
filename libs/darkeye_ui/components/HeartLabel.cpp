#include "darkeye_ui/components/HeartLabel.h"

#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPropertyAnimation>

namespace darkeye {

HeartLabel::HeartLabel(QWidget *parent) : QLabel(parent)
{
    setObjectName(QStringLiteral("DesignHeartLabel"));
    setFixedSize(32, 32);
    setAttribute(Qt::WA_TranslucentBackground);
    setCursor(Qt::PointingHandCursor);
    m_animation = new QPropertyAnimation(this, "scale", this);
    m_animation->setDuration(350);
}

bool HeartLabel::isChecked() const { return m_checked; }
bool HeartLabel::state() const { return m_checked; }
qreal HeartLabel::scale() const { return m_scale; }
void HeartLabel::setScale(qreal scale) { m_scale = scale; update(); }
void HeartLabel::setState(bool state) { m_checked = state; update(); }

void HeartLabel::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.translate(width() / 2.0, height() / 2.0);
    painter.scale(m_scale, m_scale);
    QPainterPath heart;
    heart.moveTo(0, 12);
    heart.cubicTo(-2, 9, -13, 2, -13, -5);
    heart.cubicTo(-13, -13, -3, -15, 0, -8);
    heart.cubicTo(3, -15, 13, -13, 13, -5);
    heart.cubicTo(13, 2, 2, 9, 0, 12);
    painter.setPen(QPen(m_checked ? QColor("#ff2a2a") : QColor("#cccccc"), 2));
    // Keep the unselected heart hollow, matching the Python component's
    // transparent SVG rendering.  Mixing QColor and Qt::NoBrush in a ternary
    // expression coerces NoBrush into a black QColor before setBrush sees it.
    painter.setBrush(m_checked ? QBrush(QColor("#ff2a2a")) : QBrush(Qt::NoBrush));
    painter.drawPath(heart);
}

void HeartLabel::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        m_checked = !m_checked;
        m_animation->stop();
        m_animation->setStartValue(1.0);
        m_animation->setKeyValueAt(0.3, 0.7);
        m_animation->setKeyValueAt(0.6, 1.1);
        m_animation->setEndValue(1.0);
        m_animation->start();
        emit clicked(m_checked);
    }
    QLabel::mousePressEvent(event);
}

} // namespace darkeye
