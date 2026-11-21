#pragma once

#include <QColor>
#include <QString>

namespace darkeye::myads {

struct DockTheme
{
    QColor background = QColor(QStringLiteral("#ffffff"));
    QColor text = QColor(QStringLiteral("#333333"));
    QColor border = QColor(QStringLiteral("#cccccc"));
    QColor primary = QColor(QStringLiteral("#00aaff"));
    QColor primaryHover = QColor(QStringLiteral("#0099ee"));
    QColor tabBarBackground = QColor(QStringLiteral("#e6e6e6"));
    QColor tabSelected = QColor(QStringLiteral("#ffffff"));
    QColor closeIcon = QColor(QStringLiteral("#333333"));
    QColor closeHover = QColor(QStringLiteral("#f0faff"));
    QColor closePressed = QColor(QStringLiteral("#cccccc"));
    QString fontFamily = QStringLiteral("Microsoft YaHei");
    int tabFontSize = 14;
    int previewAlpha = 80;
    int previewBorderWidth = 2;

    [[nodiscard]] static DockTheme dark();
    [[nodiscard]] QString styleSheet() const;
};

} // namespace darkeye::myads
