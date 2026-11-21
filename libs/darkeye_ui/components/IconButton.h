#pragma once

#include <QPushButton>

namespace darkeye {

class ThemeService;

class IconButton : public QPushButton
{
    Q_OBJECT

public:
    explicit IconButton(const QString &iconName = QStringLiteral("settings"),
                        ThemeService *themeService = nullptr,
                        QWidget *parent = nullptr);
    IconButton(const QString &iconName, const QString &label,
               ThemeService *themeService, QWidget *parent);
    IconButton(const QString &iconName, const QString &iconPath, int iconSize,
               int outerSize, bool hoverable = true, bool inverted = false,
               ThemeService *themeService = nullptr, QWidget *parent = nullptr);

    QString iconName() const;
    void setIconName(const QString &name);
    void setIconPath(const QString &path);
    void setIconPixelSize(int size);
    void setButtonPixelSize(int size);
    void setInverted(bool inverted);
    void setHoverable(bool hoverable);

private:
    void refreshIcon();

    ThemeService *m_themeService = nullptr;
    QString m_iconName;
    QString m_iconPath;
    int m_iconPixelSize = 24;
    int m_buttonPixelSize = 32;
    bool m_inverted = false;
};

using IconPushButton = IconButton;

} // namespace darkeye
