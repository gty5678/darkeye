#pragma once

#include <QColor>
#include <QString>

namespace darkeye::myads {

inline constexpr int DockTabBarHeight = 32;
inline constexpr int DockGap = 2;

struct DockTheme
{
    QColor background = QColor(QStringLiteral("#ffffff"));
    QColor text = QColor(QStringLiteral("#333333"));
    QColor border = QColor(QStringLiteral("#cccccc"));
    QColor primary = QColor(QStringLiteral("#00aaff"));
    QColor primaryHover = QColor(QStringLiteral("#0099ee"));
    QColor tabBarBackground = QColor(QStringLiteral("#f4f4f4"));
    QColor tabSelected = tabBarBackground;
    QColor closeIcon = QColor(QStringLiteral("#333333"));
    QColor closeHover = QColor(QStringLiteral("#f0faff"));
    QColor closePressed = QColor(QStringLiteral("#cccccc"));
    QString fontFamily = QStringLiteral("Microsoft YaHei");
    int tabFontSize = 14;
    int previewAlpha = 80;
    int previewBorderWidth = 2;

    [[nodiscard]] static DockTheme dark();
    [[nodiscard]] QColor splitterLine() const;
    [[nodiscard]] QString styleSheet() const;
};

} // namespace darkeye::myads
