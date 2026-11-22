#include "darkeye_ui/components/ChamferButton.h"

#include "darkeye_ui/theme/ThemeService.h"
#include "darkeye_ui/theme/IconProvider.h"

#include <QFileInfo>
#include <QPainter>
#include <QPainterPath>

namespace darkeye {

ChamferButton::ChamferButton(const QString &text, const QString &iconName,
                             int iconSize, int outerSize, qreal chamferRatio,
                             ThemeService *themes, QWidget *parent)
    : QAbstractButton(parent), m_iconName(iconName),
      m_iconSize(iconSize > 0 ? iconSize : qRound(outerSize * 0.55)),
      m_chamferRatio(qBound(0.0, chamferRatio, 1.0)), m_themes(themes)
{
    setObjectName(QStringLiteral("DesignChamferButton"));
    setText(text);
    setToolTip(text);
    setCursor(Qt::PointingHandCursor);
    setMouseTracking(true);
    setFixedSize(outerSize, outerSize);
    if (m_themes != nullptr) {
        connect(m_themes, &ThemeService::themeChanged, this, [this] { update(); });
    }
}

void ChamferButton::setChamferRatio(qreal ratio)
{
    m_chamferRatio = qBound(0.0, ratio, 1.0);
    update();
}
qreal ChamferButton::chamferRatio() const { return m_chamferRatio; }
void ChamferButton::setSelected(bool selected)
{
    m_selected = selected;
    update();
}
bool ChamferButton::isSelected() const { return m_selected; }
void ChamferButton::setIconName(const QString &name)
{
    m_iconName = name;
    setProperty("ChamferIconPath", {});
    update();
}
void ChamferButton::setIconPath(const QString &path)
{
    setProperty("ChamferIconPath", path);
    update();
}
void ChamferButton::setHoverable(bool hoverable)
{
    m_hoverable = hoverable;
    if (!m_hoverable && m_hovered) m_hovered = false;
    update();
}
bool ChamferButton::isHoverable() const { return m_hoverable; }
void ChamferButton::setMenuId(const QString &menuId) { m_menuId = menuId; }
QString ChamferButton::menuId() const { return m_menuId; }
void ChamferButton::setUseNativeTooltip(bool enabled)
{
    m_useNativeTooltip = enabled;
    setToolTip(enabled ? text() : QString());
}
bool ChamferButton::usesNativeTooltip() const { return m_useNativeTooltip; }

void ChamferButton::paintEvent(QPaintEvent *)
{
    const ThemeTokens tokens = ThemeService::tokens(
        m_themes == nullptr ? ThemeId::Light : m_themes->current(),
        m_themes == nullptr ? QString() : m_themes->customPrimary());
    const qreal dx = width() * m_chamferRatio * 0.5;
    const qreal dy = height() * m_chamferRatio * 0.5;
    QPainterPath path;
    path.moveTo(dx, 0); path.lineTo(width() - dx, 0);
    path.lineTo(width(), dy); path.lineTo(width(), height() - dy);
    path.lineTo(width() - dx, height()); path.lineTo(dx, height());
    path.lineTo(0, height() - dy); path.lineTo(0, dy); path.closeSubpath();
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(m_selected ? tokens.primary
                                      : m_hovered && m_hoverable ? tokens.pageBackground
                                                  : tokens.background));
    painter.drawPath(path);
    const QString iconPath = property("ChamferIconPath").toString();
    if (!m_iconName.isEmpty() || !iconPath.isEmpty()) {
        const QColor color(m_selected ? tokens.textInverse : tokens.icon);
        const QSize size(m_iconSize, m_iconSize);
        QIcon icon;
        if (iconPath.isEmpty()) {
            icon = IconProvider::builtIn(m_iconName, size, color);
        } else if (QFileInfo(iconPath).suffix().compare(
                       QStringLiteral("svg"), Qt::CaseInsensitive) == 0) {
            icon = IconProvider::fromSvg(iconPath, size, color);
        } else {
            icon = QIcon(iconPath);
        }
        const QPixmap pixmap = icon.pixmap(size);
        if (!pixmap.isNull()) {
            painter.drawPixmap((width() - m_iconSize) / 2,
                               (height() - m_iconSize) / 2, pixmap);
        }
    }
}

void ChamferButton::enterEvent(QEnterEvent *event)
{
    m_hovered = m_hoverable;
    update();
    QAbstractButton::enterEvent(event);
}
void ChamferButton::leaveEvent(QEvent *event)
{
    m_hovered = false;
    update();
    QAbstractButton::leaveEvent(event);
}

} // namespace darkeye
