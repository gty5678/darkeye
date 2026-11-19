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
        "QTabBar#MyAdsTabBar { background:%11; color:%2; border:none; "
        "font-family:\"%6\"; font-size:%7px; }"
        "QStackedWidget#MyAdsPageStack, QStackedWidget#MyAdsPageStack > QWidget { background:%8; }"
        "QToolButton#MyAdsCloseButton { padding:0; margin:0; border:none; background:transparent; }"
        "QToolButton#MyAdsCloseButton:hover { background:%5; border-radius:2px; }"
        "QToolButton#MyAdsCloseButton:pressed { background:%9; }"
        "QSplitter::handle { background:%3; width:2px; height:2px; border:none; margin:0; }"
        "QSplitter::handle:hover { background:qlineargradient(x1:0,y1:0,x2:0,y2:1,"
        "stop:0 %4,stop:0.5 %10,stop:1 %4); }")
        .arg(background.name(), text.name(), border.name(), primary.name(),
             closeHover.name(), fontFamily)
        .arg(tabFontSize)
        .arg(tabSelected.name(), closePressed.name(), primaryHover.name(), tabBarBackground.name());
}

} // namespace darkeye::myads


