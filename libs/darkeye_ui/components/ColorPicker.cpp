#include "darkeye_ui/components/ColorPicker.h"
#include "darkeye_ui/components/ColorWheel.h"
#include "darkeye_ui/theme/ChamferPath.h"

#include <QtMath>

#include <QApplication>
#include <QEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QScreen>
#include <QSignalBlocker>

namespace darkeye {

ColorPicker::ColorPicker(const QColor &color, bool showText, Shape shape,
                         QWidget *parent)
    : QLabel(parent), m_color(color.isValid() ? color : QColor("#cccccc")),
      m_showText(showText), m_shape(shape)
{
    setObjectName(QStringLiteral("DesignColorPicker"));
    setAlignment(Qt::AlignCenter);
    setCursor(Qt::PointingHandCursor);
    updateDisplay();
}

QString ColorPicker::color() const { return m_color.name(); }

void ColorPicker::setColor(const QString &color)
{
    const QColor candidate(color);
    if (!candidate.isValid() || candidate == m_color) return;
    m_color = candidate;
    if (m_shape == Shape::Rectangle && m_showText) setText(m_color.name());
    update();
    emit colorChanged(m_color.name());
}

void ColorPicker::setShowText(bool show) { m_showText = show; updateDisplay(); }
void ColorPicker::setShape(Shape shape) { m_shape = shape; updateDisplay(); }

void ColorPicker::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event)
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    const QRectF bounds = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
    // A regular octagon has equal horizontal, vertical and diagonal sides.
    const qreal corner = m_shape == Shape::Octagon
        ? qMin(bounds.width(), bounds.height()) / (2 + qSqrt(2.0)) : 6;
    painter.fillPath(chamferPath(bounds, corner), m_color);
    if (m_shape == Shape::Rectangle && m_showText) {
        painter.setPen(m_color.lightness() > 128 ? Qt::black : Qt::white);
        painter.setFont(font());
        painter.drawText(rect(), alignment(), text());
    }
}

void ColorPicker::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        if (m_colorWheel != nullptr && m_colorWheel->isVisible()) {
            m_colorWheel->hide();
            event->accept();
            return;
        }
        if (m_colorWheel == nullptr) {
            m_colorWheel = new ColorWheelSimple(this);
            m_colorWheel->setWindowFlags(Qt::FramelessWindowHint |
                                         Qt::WindowStaysOnTopHint | Qt::Popup);
            connect(m_colorWheel, &ColorWheelSimple::colorChanged, this,
                    [this](const QColor &color) { setColor(color.name()); });
            qApp->installEventFilter(this);
        }
        {
            const QSignalBlocker blocker(m_colorWheel);
            m_colorWheel->setInitialColor(m_color.name(QColor::HexRgb));
        }
        m_colorWheel->adjustSize();
        const QPoint topLeft = mapToGlobal(QPoint(0, 0));
        const QRect available = screen() != nullptr
            ? screen()->availableGeometry() : QRect(topLeft, QSize(1920, 1080));
        const int x = qBound(available.left(),
            topLeft.x() + (width() - m_colorWheel->width()) / 2,
            available.right() - m_colorWheel->width() + 1);
        const int gap = 10;
        const int y = topLeft.y() - available.top() >= m_colorWheel->height() + gap
            ? topLeft.y() - m_colorWheel->height() - gap
            : topLeft.y() + height() + gap;
        m_colorWheel->move(x, y);
        m_colorWheel->show();
        m_colorWheel->raise();
        m_colorWheel->setFocus();
        event->accept();
        return;
    }
    QLabel::mousePressEvent(event);
}

bool ColorPicker::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_colorWheel && event->type() == QEvent::Hide) {
        emit colorConfirmed(m_color.name());
    }
    // Observe clicks before child widgets or native popup handling consume them.
    if (m_colorWheel != nullptr && m_colorWheel->isVisible()
        && event->type() == QEvent::MouseButtonPress) {
        const auto *mouse = static_cast<QMouseEvent *>(event);
        const QPoint local = m_colorWheel->mapFromGlobal(mouse->globalPosition().toPoint());
        if (!m_colorWheel->rect().contains(local)) {
            m_colorWheel->hide();
            // Consume the dismissing click, including clicks on the picker itself.
            return true;
        }
    }
    return QLabel::eventFilter(watched, event);
}

void ColorPicker::updateDisplay()
{
    if (m_shape == Shape::Octagon) {
        setFixedSize(32, 32);
        setText({});
        setStyleSheet({});
        // Paint the octagon directly with transparent corners.
        setAttribute(Qt::WA_TranslucentBackground, true);
    } else {
        setAttribute(Qt::WA_TranslucentBackground, true);
        setMinimumSize(0, 0);
        setMaximumSize(32, 32);
        setText(m_showText ? m_color.name() : QString());
        setStyleSheet(QStringLiteral("background:transparent;font-size:24px;"));
    }
    update();
}

} // namespace darkeye
