#include "darkeye_ui/theme/ChamferPath.h"
#include "darkeye_ui/components/LoadingFeedback.h"

#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>
#include <QTimer>

namespace darkeye {

Skeleton::Skeleton(int height, int radius, bool animated,
                   int intervalMilliseconds, ThemeService *themes, QWidget *parent)
    : QWidget(parent), m_radius(qMax(0, radius)), m_themes(themes)
{
    setObjectName(QStringLiteral("DesignSkeleton"));
    setFixedHeight(qMax(6, height));
    m_timer = new QTimer(this);
    m_timer->setInterval(qMax(16, intervalMilliseconds));
    connect(m_timer, &QTimer::timeout, this, [this] {
        m_offset = (m_offset + 6) % 240;
        update();
    });
    if (m_themes != nullptr) {
        connect(m_themes, &ThemeService::themeChanged, this,
                [this] { update(); });
    }
    setAnimated(animated);
}

void Skeleton::start() { if (!m_timer->isActive()) m_timer->start(); }
void Skeleton::stop() { m_timer->stop(); }
void Skeleton::setAnimated(bool animated) { animated ? start() : stop(); }
bool Skeleton::isAnimating() const { return m_timer->isActive(); }

void Skeleton::paintEvent(QPaintEvent *)
{
    QRectF area = QRectF(rect()).adjusted(0, 0, -1, -1);
    if (area.isEmpty()) return;
    const ThemeTokens values = ThemeService::tokens(
        m_themes == nullptr ? ThemeId::Light : m_themes->current(),
        m_themes == nullptr ? QString() : m_themes->customPrimary());
    QColor base(values.inputBackground);
    QColor highlight(values.border);
    highlight = base.lightness() > highlight.lightness() ? base.darker(106)
                                                          : base.lighter(112);
    QLinearGradient gradient(area.left() - area.width() + m_offset, area.top(),
                             area.left() + m_offset, area.bottom());
    gradient.setColorAt(0.0, base);
    gradient.setColorAt(0.5, highlight);
    gradient.setColorAt(1.0, base);
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(base);
    painter.setBrush(gradient);
    painter.drawPath(chamferPath(area, m_radius));
}

CalloutTooltip::CalloutTooltip(ThemeService *themes, QWidget *parent)
    : QWidget(parent, Qt::ToolTip | Qt::FramelessWindowHint |
                          Qt::WindowStaysOnTopHint),
      m_themes(themes)
{
    setObjectName(QStringLiteral("DesignCalloutTooltip"));
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_TransparentForMouseEvents);
}

void CalloutTooltip::setTokens(const ThemeTokens &tokens)
{
    m_overrideTokens = tokens;
    update();
}

void CalloutTooltip::clearTokens() { m_overrideTokens.reset(); update(); }

void CalloutTooltip::showFor(QWidget *target, const QString &text)
{
    if (target == nullptr || text.isEmpty()) {
        hide();
        return;
    }
    m_text = text;
    const QFontMetrics metrics(font());
    const int width = metrics.horizontalAdvance(text) + 12 + 18 + 6;
    const int height = metrics.height() + 12;
    setFixedSize(width, height);
    const QPoint topRight = target->mapToGlobal(target->rect().topRight());
    move(topRight.x(), topRight.y() + (target->height() - height) / 2);
    show();
    raise();
}

void CalloutTooltip::paintEvent(QPaintEvent *)
{
    constexpr qreal arrow = 6.0;
    const ThemeTokens values = tokens();
    QPainterPath path;
    path.moveTo(arrow, 0);
    path.lineTo(width(), 0);
    path.lineTo(width(), height());
    path.lineTo(arrow, height());
    path.lineTo(arrow, height() / 2.0 + arrow);
    path.lineTo(0, height() / 2.0);
    path.lineTo(arrow, height() / 2.0 - arrow);
    path.closeSubpath();
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(QPen(QColor(values.border), 1));
    painter.setBrush(QColor(values.inputBackground));
    painter.drawPath(path);
    painter.setPen(QColor(values.text));
    painter.drawText(QRectF(18, 6, width() - 30, height() - 12),
                     Qt::AlignLeft | Qt::AlignVCenter, m_text);
}

ThemeTokens CalloutTooltip::tokens() const
{
    if (m_overrideTokens.has_value()) return *m_overrideTokens;
    return ThemeService::tokens(m_themes == nullptr ? ThemeId::Light
                                                     : m_themes->current(),
                                m_themes == nullptr ? QString()
                                                     : m_themes->customPrimary());
}

} // namespace darkeye
