#pragma once

#include <QColorSpace>
#include <QPalette>
#include <QObject>
#include <QMap>
#include <QPointer>
#include <QString>
#include <QVector>

class QApplication;

namespace darkeye {

class SelectionIndicatorStyle;

enum class ThemeId
{
    Light,
    Dark,
    Red,
    Green,
    Yellow,
    Blue,
    Purple,
};

struct ThemeTokens
{
    QString primary;
    QString primaryHover;
    QString background;
    QString inputBackground;
    QString pageBackground;
    QString border;
    QString borderFocus;
    QString text;
    QString textInverse;
    QString textPlaceholder;
    QString textDisabled;
    QString success;
    QString warning;
    QString error;
    QString info;
    QString icon;
    QString iconDisabled;
    QString radiusMd = QStringLiteral("8px");
    QString fontFamilyBase = QStringLiteral("Microsoft YaHei");
    QString fontSizeBase = QStringLiteral("14px");
    QString fontSizeMiddle = QStringLiteral("16px");
    QString fontSizeWorkspaceTab = QStringLiteral("14px");
    static constexpr int defaultBorderWidth = 2;
    QString borderWidth = QString::number(defaultBorderWidth) + QStringLiteral("px");

    QString textOnPrimaryHover{};
    QString textOnStatus{};
    QString textOnImage{};
    QString progressFill{};
    QString modified = QStringLiteral("#ffa500");
    QString imageDropOutline = QStringLiteral("#8a8a8a");
    static constexpr auto defaultColorSpace = QColorSpace::DisplayP3;
    QColorSpace colorSpace{defaultColorSpace}; // Encoding of the RGB tokens in this instance.

    [[nodiscard]] QMap<QString, QString> toMap() const;
    [[nodiscard]] ThemeTokens convertedTo(const QColorSpace &target) const;
};

class ThemeService final : public QObject
{
    Q_OBJECT

public:
    // Initializes the application's Fusion base style; construct before widgets.
    explicit ThemeService(QApplication &application);

    ThemeId current() const;
    QString customPrimary() const;

    // These three operations deliberately mirror Python ThemeManager's
    // state-only API.  They never mutate QApplication's style sheet; callers
    // that want to apply the state use setTheme().
    void setCurrent(ThemeId theme);
    void setCustomPrimary(const QString &customPrimary);
    [[nodiscard]] ThemeTokens currentTokens() const;

    bool setTheme(ThemeId theme, const QString &customPrimary = {});

    static ThemeId fromSettings(const QString &value);
    static QString toSettings(ThemeId theme);
    static QString displayName(ThemeId theme);
    static QVector<ThemeId> availableThemes();
    static QColorSpace outputColorSpace();
    static QColorSpace::NamedColorSpace colorMode();
    static void setColorMode(QColorSpace::NamedColorSpace mode);
    static ThemeTokens sourceTokens(ThemeId theme, const QString &customPrimary = {});
    static ThemeTokens tokens(ThemeId theme, const QString &customPrimary = {});

signals:
    void themeChanged(darkeye::ThemeId theme);

private:
    QApplication &m_application;
    QPalette m_paletteBeforeDark;
    QPointer<SelectionIndicatorStyle> m_indicatorStyle;
    ThemeId m_current = ThemeId::Light;
    QString m_customPrimary;
};

} // namespace darkeye
