#include "darkeye_ui/components/InteractionEffects.h"

#include <QEasingCurve>
#include <QMouseEvent>
#include <QPainter>
#include <QPropertyAnimation>
#include <QStyle>
#include <QStyleOptionButton>
#include <QWheelEvent>

namespace darkeye {
namespace {

void paintButtonBase(IconButton *button, QPainter &painter)
{
    QStyleOptionButton option;
    option.initFrom(button);
    option.text = button->text();
    button->style()->drawControl(QStyle::CE_PushButton, &option, &painter, button);
}

void paintIcon(IconButton *button, QPainter &painter, qreal angle, qreal offset)
{
    const QPixmap pixmap = button->icon().pixmap(button->iconSize());
    if (pixmap.isNull()) return;
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);
    painter.translate(button->width() / 2.0 + offset, button->height() / 2.0);
    painter.rotate(angle);
    const QSize size = button->iconSize();
    const QRectF target(-size.width() / 2.0, -size.height() / 2.0,
                        size.width(), size.height());
    painter.drawPixmap(target, pixmap, QRectF(pixmap.rect()));
}

} // namespace

RotateButton::RotateButton(const QString &iconName, ThemeService *themes,
                           QWidget *parent)
    : IconButton(iconName, themes, parent)
{
    setObjectName(QStringLiteral("DesignRotateButton"));
    m_animation = new QPropertyAnimation(this, "angle", this);
    m_animation->setDuration(400);
}

RotateButton::RotateButton(const QString &iconName, const QString &iconPath,
                           int iconSize, int outerSize, bool hoverable,
                           ThemeService *themes, QWidget *parent)
    : IconButton(iconName, iconPath, iconSize, outerSize, hoverable, false,
                 themes, parent)
{
    setObjectName(QStringLiteral("DesignRotateButton"));
    m_animation = new QPropertyAnimation(this, "angle", this);
    m_animation->setDuration(400);
}

qreal RotateButton::angle() const { return m_angle; }
void RotateButton::setAngle(qreal angle) { m_angle = angle; update(); }

void RotateButton::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    paintButtonBase(this, painter);
    paintIcon(this, painter, m_angle, 0.0);
}

void RotateButton::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        m_animation->stop();
        m_animation->setStartValue(m_angle);
        m_animation->setEndValue(m_angle + 180.0);
        m_animation->start();
    }
    IconButton::mousePressEvent(event);
}

ShakeButton::ShakeButton(const QString &iconName, ThemeService *themes,
                         QWidget *parent)
    : IconButton(iconName, themes, parent)
{
    setObjectName(QStringLiteral("DesignShakeButton"));
    m_animation = new QPropertyAnimation(this, "iconOffset", this);
    m_animation->setDuration(400);
    m_animation->setEasingCurve(QEasingCurve::OutQuad);
}

ShakeButton::ShakeButton(const QString &iconName, const QString &iconPath,
                         int iconSize, int outerSize, bool hoverable,
                         ThemeService *themes, QWidget *parent)
    : IconButton(iconName, iconPath, iconSize, outerSize, hoverable, false,
                 themes, parent)
{
    setObjectName(QStringLiteral("DesignShakeButton"));
    m_animation = new QPropertyAnimation(this, "iconOffset", this);
    m_animation->setDuration(400);
    m_animation->setEasingCurve(QEasingCurve::OutQuad);
}

qreal ShakeButton::iconOffset() const { return m_offset; }
void ShakeButton::setIconOffset(qreal offset) { m_offset = offset; update(); }

void ShakeButton::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    paintButtonBase(this, painter);
    paintIcon(this, painter, 0.0, m_offset);
}

void ShakeButton::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        m_animation->stop();
        m_animation->setStartValue(0.0);
        m_animation->setKeyValueAt(0.25, -7.0);
        m_animation->setKeyValueAt(0.50, 5.0);
        m_animation->setKeyValueAt(0.75, -3.0);
        m_animation->setEndValue(0.0);
        m_animation->start();
    }
    IconButton::mousePressEvent(event);
}

ClickableSlider::ClickableSlider(Qt::Orientation orientation,
                                 ThemeService *, QWidget *parent)
    : QSlider(orientation, parent)
{
    setObjectName(QStringLiteral("DesignClickableSlider"));
    if (orientation == Qt::Horizontal) setMinimumHeight(22);
    else setMinimumWidth(22);
}

void ClickableSlider::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        const qreal ratio = orientation() == Qt::Horizontal
                                ? event->position().x() / qMax(1, width())
                                : 1.0 - event->position().y() / qMax(1, height());
        const qreal bounded = qBound(0.0, ratio, 1.0);
        setValue(minimum() + qRound(bounded * (maximum() - minimum())));
    }
    QSlider::mousePressEvent(event);
}

void ClickableSlider::wheelEvent(QWheelEvent *event) { event->ignore(); }

} // namespace darkeye
