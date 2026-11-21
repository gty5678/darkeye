#include "darkeye_ui/components/AnimatedIndicators.h"

#include "darkeye_ui/theme/ChamferPath.h"
#include "darkeye_ui/theme/ThemeService.h"

#include <QMouseEvent>
#include <QPainter>
#include <QPropertyAnimation>
#include <QTimer>

#include <cmath>

namespace darkeye {

ToggleSwitch::ToggleSwitch(int width, int height, ThemeService *themes, QWidget *parent)
    : QWidget(parent), m_themes(themes)
{
    setObjectName(QStringLiteral("DesignToggleSwitch"));
    setFixedSize(width, height);
    setCursor(Qt::PointingHandCursor);
    m_offsetAnimation = new QPropertyAnimation(this, "offset", this);
    m_offsetAnimation->setDuration(200);
    m_colorAnimation = new QPropertyAnimation(this, "backgroundColor", this);
    m_colorAnimation->setDuration(200);
    if (m_themes != nullptr) {
        connect(m_themes, &ThemeService::themeChanged, this,
                &ToggleSwitch::refreshTokens);
    }
    refreshTokens();
}

bool ToggleSwitch::isChecked() const { return m_checked; }
qreal ToggleSwitch::offset() const { return m_offset; }
void ToggleSwitch::setOffset(qreal offset) { m_offset = offset; update(); }
QColor ToggleSwitch::backgroundColor() const { return m_background; }
void ToggleSwitch::setBackgroundColor(const QColor &color)
{
    m_background = color;
    update();
}

void ToggleSwitch::setChecked(bool checked)
{
    if (m_checked == checked) return;
    m_checked = checked;
    emit toggled(m_checked);
    m_offsetAnimation->stop();
    m_offsetAnimation->setStartValue(m_offset);
    m_offsetAnimation->setEndValue(endOffset(m_checked));
    m_offsetAnimation->start();
    m_colorAnimation->stop();
    m_colorAnimation->setStartValue(m_background);
    m_colorAnimation->setEndValue(m_checked ? m_active : m_inactive);
    m_colorAnimation->start();
}

void ToggleSwitch::refreshTokens()
{
    const ThemeId theme = m_themes == nullptr ? ThemeId::Light : m_themes->current();
    const QString custom = m_themes == nullptr ? QString() : m_themes->customPrimary();
    const ThemeTokens tokens = ThemeService::tokens(theme, custom);
    m_inactive = QColor(tokens.border);
    m_active = QColor(tokens.primary);
    m_thumb = QColor(tokens.background);
    m_offsetAnimation->stop();
    m_colorAnimation->stop();
    m_offset = endOffset(m_checked);
    m_background = m_checked ? m_active : m_inactive;
    update();
}

void ToggleSwitch::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    // Use regular-octagon corners on the thumb and extend the track horizontally.
    const qreal cornerRatio = 1.0 / (2.0 + std::sqrt(2.0));
    const QRectF track(rect());
    painter.fillPath(chamferPath(track, height() * cornerRatio), m_background);
    const int diameter = height() - 4;
    const QRectF thumb(m_offset, 2.0, diameter, diameter);
    painter.fillPath(chamferPath(thumb, diameter * cornerRatio), m_thumb);
}

void ToggleSwitch::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        setChecked(!m_checked);
        event->accept();
        return;
    }
    QWidget::mousePressEvent(event);
}

qreal ToggleSwitch::endOffset(bool checked) const
{
    return checked ? width() - height() + 2.0 : 2.0;
}

CircularLoading::CircularLoading(int size, int strokeWidth,
                                 ThemeService *themes, QWidget *parent)
    : QWidget(parent), m_size(size), m_stroke(strokeWidth), m_themes(themes)
{
    setObjectName(QStringLiteral("CircularLoading"));
    setFixedSize(size, size);
    m_timer = new QTimer(this);
    m_timer->setInterval(40);
    connect(m_timer, &QTimer::timeout, this, [this] {
        m_angle = std::fmod(m_angle + 8.0, 360.0);
        update();
    });
    if (m_themes != nullptr) {
        connect(m_themes, &ThemeService::themeChanged, this,
                &CircularLoading::refreshTokens);
    }
    refreshTokens();
}

bool CircularLoading::isAnimating() const { return m_timer->isActive(); }
void CircularLoading::start() { m_timer->start(); }
void CircularLoading::stop() { m_timer->stop(); }

void CircularLoading::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    start();
}

void CircularLoading::hideEvent(QHideEvent *event)
{
    stop();
    QWidget::hideEvent(event);
}

void CircularLoading::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    const qreal margin = m_stroke / 2.0;
    const QRectF arcRect = rect().adjusted(margin, margin, -margin, -margin);
    QPen pen(m_track, m_stroke, Qt::SolidLine, Qt::RoundCap);
    painter.setPen(pen);
    painter.setBrush(Qt::NoBrush);
    painter.drawArc(arcRect, 0, 360 * 16);
    pen.setColor(m_arc);
    painter.setPen(pen);
    painter.drawArc(arcRect, qRound(m_angle * 16), -90 * 16);
}

void CircularLoading::refreshTokens()
{
    const ThemeId theme = m_themes == nullptr ? ThemeId::Light : m_themes->current();
    const QString custom = m_themes == nullptr ? QString() : m_themes->customPrimary();
    const ThemeTokens tokens = ThemeService::tokens(theme, custom);
    m_arc = QColor(tokens.primary);
    m_track = QColor(tokens.textDisabled);
    if (m_stroke <= 0) m_stroke = qMax(4, qMin(4, m_size / 3));
    update();
}

} // namespace darkeye

