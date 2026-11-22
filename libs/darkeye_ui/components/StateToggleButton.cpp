#include "darkeye_ui/components/StateToggleButton.h"

#include "darkeye_ui/theme/ThemeService.h"
#include "darkeye_ui/theme/IconProvider.h"

namespace darkeye {

StateToggleButton::StateToggleButton(const QString &state1Icon,
                                     const QString &state2Icon, int iconSize,
                                     int outerSize, ThemeService *themes,
                                     QWidget *parent)
    : QPushButton(parent), m_state1Icon(state1Icon), m_state2Icon(state2Icon),
      m_iconSize(iconSize), m_themes(themes)
{
    setObjectName(QStringLiteral("DesignStateToggleButton"));
    setFixedSize(outerSize, outerSize);
    setIconSize(QSize(iconSize, iconSize));
    setCursor(Qt::PointingHandCursor);
    connect(this, &QPushButton::clicked, this, &StateToggleButton::toggleState);
    if (m_themes != nullptr) {
        connect(m_themes, &ThemeService::themeChanged, this,
                [this] { refreshIcons(); });
    }
    refreshIcons();
}

bool StateToggleButton::state() const { return m_state; }

void StateToggleButton::setState(bool state)
{
    if (m_state == state) return;
    m_state = state;
    refreshIcons();
}

void StateToggleButton::toggleState()
{
    m_state = !m_state;
    refreshIcons();
    setToolTip(m_state ? QStringLiteral("已激活") : QStringLiteral("默认"));
    emit stateChanged(m_state);
}

void StateToggleButton::refreshIcons()
{
    const ThemeId theme = m_themes == nullptr ? ThemeId::Light : m_themes->current();
    const QString custom = m_themes == nullptr ? QString() : m_themes->customPrimary();
    const ThemeTokens tokens = ThemeService::tokens(theme, custom);
    setIcon(IconProvider::builtIn(m_state ? m_state2Icon : m_state1Icon,
                                  QSize(m_iconSize, m_iconSize),
                                  QColor(m_state ? tokens.primary : tokens.icon)));
}

} // namespace darkeye


