#include "ui/layouts/myads/DockTheme.h"

namespace darkeye::myads {

DockTheme DockTheme::dark()
{
    DockTheme theme;
    theme.background = QColor(QStringLiteral("#1e1e1e"));
    theme.text = QColor(QStringLiteral("#e0e0e0"));
    theme.border = QColor(QStringLiteral("#444444"));
    theme.primaryHover = QColor(QStringLiteral("#33bbff"));
    theme.tabBarBackground = QColor(QStringLiteral("#252526"));
    theme.tabSelected = theme.background;
    theme.closeIcon = QColor(QStringLiteral("#e0e0e0"));
    theme.closeHover = QColor(QStringLiteral("#2d2d2d"));
    theme.closePressed = QColor(QStringLiteral("#444444"));
    return theme;
}

QString DockTheme::styleSheet() const
{
    return QStringLiteral(
        "#MyAdsWorkspace { background:%1; color:%2; }"
        "QWidget[myadsPane=\"true\"] { background:%1; border:none; }"
        "QWidget#MyAdsLeftSidebar { border-right:1px solid %9; }"
        "QWidget#MyAdsRightSidebar { border-left:1px solid %9; }"
        "QTabBar#MyAdsTabBar { background:%8; color:%2; border:none; "
        "font-family:\"%4\"; font-size:%5px; }"
        "QStackedWidget#MyAdsPageStack, QStackedWidget#MyAdsPageStack > QWidget { background:%6; }"
        "QToolButton#MyAdsCloseButton { padding:0; margin:0; border:none; background:transparent; }"
        "QToolButton#MyAdsCloseButton:hover { background:%3; border-radius:2px; }"
        "QToolButton#MyAdsCloseButton:pressed { background:%7; }")
        .arg(background.name(), text.name(), closeHover.name(), fontFamily,
             QString::number(tabFontSize), tabSelected.name(), closePressed.name(),
             tabBarBackground.name(), border.name());
}

} // namespace darkeye::myads


