#include "darkeye_ui/components/DesignButton.h"

#include "darkeye_ui/theme/IconProvider.h"

#include <QColor>
#include <QFileInfo>
#include <QStyle>

namespace darkeye {

DesignButton::DesignButton(const QString &text, QWidget *parent)
    : QPushButton(text, parent)
{
    setObjectName(QStringLiteral("DesignButton"));
    setProperty("darkeyeStyle", QStringLiteral("DesignButton"));
    setVariant(QStringLiteral("default"));
}

DesignButton::DesignButton(const QString &text, const QString &variant,
                           const QIcon &icon, const QSize &iconSize,
                           QWidget *parent)
    : DesignButton(text, parent)
{
    setVariant(variant);
    setIcon(icon);
    setIconSize(iconSize);
}

DesignButton::DesignButton(const QString &text, const QString &variant,
                           const QString &iconSource, const QSize &iconSize,
                           const QColor &iconColor, QWidget *parent)
    : DesignButton(text, parent)
{
    setVariant(variant);
    const QColor color = iconColor.isValid() ? iconColor : QColor(Qt::black);
    const QIcon icon = QFileInfo::exists(iconSource)
                           ? IconProvider::fromSvg(iconSource, iconSize, color)
                           : IconProvider::builtIn(iconSource, iconSize, color);
    setIcon(icon);
    setIconSize(iconSize);
}

void DesignButton::setVariant(const QString &variant)
{
    setProperty("variant", variant.trimmed().isEmpty() ? QStringLiteral("default")
                                                       : variant.trimmed());
    style()->unpolish(this);
    style()->polish(this);
    update();
}

QString DesignButton::variant() const
{
    return property("variant").toString();
}

} // namespace darkeye
