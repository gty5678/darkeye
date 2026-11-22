#include "darkeye_ui/components/IconButton.h"

#include "darkeye_ui/theme/ThemeService.h"
#include "darkeye_ui/theme/IconProvider.h"

#include <QFileInfo>
#include <QStyle>

namespace darkeye {

IconButton::IconButton(const QString &iconName, ThemeService *themeService,
                       QWidget *parent)
    : QPushButton(parent), m_themeService(themeService), m_iconName(iconName)
{
    setObjectName(QStringLiteral("DesignIconPushButton"));
    setCursor(Qt::PointingHandCursor);
    setFlat(true);
    setButtonPixelSize(m_buttonPixelSize);
    if (m_themeService != nullptr) {
        connect(m_themeService, &ThemeService::themeChanged, this,
                [this] { refreshIcon(); });
    }
    refreshIcon();
}

IconButton::IconButton(const QString &iconName, const QString &label,
                       ThemeService *themeService, QWidget *parent)
    : IconButton(iconName, themeService, parent)
{
    setToolTip(label);
    setAccessibleName(label);
}

IconButton::IconButton(const QString &iconName, const QString &iconPath,
                       int iconSize, int outerSize, bool hoverable,
                       bool inverted, ThemeService *themeService, QWidget *parent)
    : IconButton(iconName, themeService, parent)
{
    setIconPath(iconPath);
    setIconPixelSize(iconSize);
    setButtonPixelSize(outerSize);
    setHoverable(hoverable);
    setInverted(inverted);
}

QString IconButton::iconName() const { return m_iconName; }

void IconButton::setIconName(const QString &name)
{
    m_iconName = name;
    m_iconPath.clear();
    refreshIcon();
}

void IconButton::setIconPath(const QString &path)
{
    m_iconPath = path;
    refreshIcon();
}

void IconButton::setIconPixelSize(int size)
{
    m_iconPixelSize = qMax(1, size);
    setIconSize(QSize(m_iconPixelSize, m_iconPixelSize));
    refreshIcon();
}

void IconButton::setButtonPixelSize(int size)
{
    m_buttonPixelSize = qMax(1, size);
    setFixedSize(m_buttonPixelSize, m_buttonPixelSize);
}

void IconButton::setInverted(bool inverted)
{
    m_inverted = inverted;
    refreshIcon();
}

void IconButton::setHoverable(bool hoverable)
{
    setProperty("hoverable", hoverable);
    style()->unpolish(this);
    style()->polish(this);
    update();
}

void IconButton::refreshIcon()
{
    const ThemeTokens tokens = m_themeService != nullptr
                                   ? ThemeService::tokens(m_themeService->current(),
                                                          m_themeService->customPrimary())
                                   : ThemeService::tokens(ThemeId::Light);
    const QColor color(m_inverted ? tokens.textInverse : tokens.icon);
    const QSize size(m_iconPixelSize, m_iconPixelSize);
    if (m_iconPath.isEmpty()) {
        setIcon(IconProvider::builtIn(m_iconName, size, color));
    } else if (QFileInfo(m_iconPath).suffix().compare(QStringLiteral("svg"),
                                                       Qt::CaseInsensitive) == 0) {
        setIcon(IconProvider::fromSvg(m_iconPath, size, color));
    } else {
        setIcon(QIcon(m_iconPath));
    }
    setIconSize(size);
}

} // namespace darkeye
