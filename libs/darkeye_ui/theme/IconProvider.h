#pragma once

#include <QColor>
#include <QIcon>
#include <QSize>
#include <QString>

namespace darkeye {

class IconProvider final
{
public:
    static QIcon builtIn(const QString &name, const QSize &size = QSize(24, 24),
                         const QColor &color = QColor(QStringLiteral("#333333")),
                         qreal devicePixelRatio = 0.0);
    static QIcon fromSvg(const QString &source, const QSize &size,
                         const QColor &color = {}, qreal devicePixelRatio = 0.0);
    static bool contains(const QString &name);
};

} // namespace darkeye
