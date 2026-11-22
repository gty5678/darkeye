#pragma once

#include <QColor>
#include <QIcon>
#include <QPushButton>
#include <QSize>

namespace darkeye {

class DesignButton : public QPushButton
{
public:
    explicit DesignButton(const QString &text = {}, QWidget *parent = nullptr);
    DesignButton(const QString &text, const QString &variant, const QIcon &icon,
                 const QSize &iconSize = QSize(24, 24), QWidget *parent = nullptr);
    DesignButton(const QString &text, const QString &variant, const QString &iconSource,
                 const QSize &iconSize = QSize(24, 24), const QColor &iconColor = {},
                 QWidget *parent = nullptr);

    void setVariant(const QString &variant);
    QString variant() const;
};

using Button = DesignButton;

} // namespace darkeye
