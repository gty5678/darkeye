#include "darkeye_ui/theme/ThemeService.h"
#include "ChamferStyle.h"

#include <QApplication>
#include <QColor>
#include <QFile>
#include <QHash>
#include <QResource>
#include <QRegularExpression>

#include <mutex>

void initializeDarkeyeUiResourceCollection()
{
    Q_INIT_RESOURCE(darkeye_ui_resources);
}

namespace darkeye {
namespace {

ThemeTokens lightTokens()
{
    return {"#00aaff", "#0099ee", "#ffffff", "#f0faff", "#ffffff",
            "#cccccc", "#00aaff", "#333333", "#ffffff", "#bbbbbb",
            "#999999", "#2e7d32", "#ed6c02", "#c62828", "#0288d1",
            "#333333", "#999999"};
}

ThemeTokens darkTokens()
{
    return {"#00aaff", "#33bbff", "#1e1e1e", "#2d2d2d", "#252526",
            "#444444", "#00aaff", "#e0e0e0", "#1e1e1e", "#888888",
            "#666666", "#66bb6a", "#ffb74d", "#ef5350", "#29b6f6",
            "#e0e0e0", "#666666"};
}

ThemeTokens baseTokens(ThemeId theme)
{
    switch (theme) {
    case ThemeId::Red:
        return {"#c62828", "#b71c1c", "#fff5f5", "#ffebee", "#fce4ec",
                "#ef9a9a", "#c62828", "#4a1515", "#ffffff", "#c62828",
                "#8d6e63", "#1b5e20", "#e65100", "#b71c1c", "#01579b",
                "#4a1515", "#8d6e63"};
    case ThemeId::Green:
        return {"#2e7d32", "#1b5e20", "#f1f8e9", "#e8f5e9", "#e0f2e9",
                "#a5d6a7", "#2e7d32", "#1b3d1f", "#ffffff", "#388e3c",
                "#6b7c6d", "#1b5e20", "#e65100", "#c62828", "#1565c0",
                "#1b3d1f", "#6b7c6d"};
    case ThemeId::Yellow:
        return {"#f9a825", "#f57f17", "#fffde7", "#fff8e1", "#ffecb3",
                "#ffe082", "#f9a825", "#3e2723", "#ffffff", "#ff8f00",
                "#8d6e63", "#2e7d32", "#e65100", "#c62828", "#0277bd",
                "#3e2723", "#8d6e63"};
    case ThemeId::Blue:
        return {"#1976d2", "#1565c0", "#f8fbff", "#e8f4fc", "#e1f0fa",
                "#90caf9", "#1976d2", "#1e3a5f", "#ffffff", "#5c9fd6",
                "#78909c", "#2e7d32", "#e65100", "#c62828", "#0288d1",
                "#1e3a5f", "#78909c"};
    case ThemeId::Purple:
        return {"#8e24aa", "#7b1fa2", "#faf8fc", "#f3e5f5", "#ede7f6",
                "#d1c4e9", "#8e24aa", "#3e2a4a", "#ffffff", "#9575cd",
                "#78909c", "#2e7d32", "#e65100", "#c62828", "#7e57c2",
                "#3e2a4a", "#78909c"};
    case ThemeId::Dark:
        return darkTokens();
    case ThemeId::Light:
        return lightTokens();
    }
    return lightTokens();
}

} // namespace

QMap<QString, QString> ThemeTokens::toMap() const
{
    return {
        {QStringLiteral("color_primary"), primary},
        {QStringLiteral("color_primary_hover"), primaryHover},
        {QStringLiteral("color_bg"), background},
        {QStringLiteral("color_bg_input"), inputBackground},
        {QStringLiteral("color_bg_page"), pageBackground},
        {QStringLiteral("color_border"), border},
        {QStringLiteral("color_border_focus"), borderFocus},
        {QStringLiteral("color_text"), text},
        {QStringLiteral("color_text_inverse"), textInverse},
        {QStringLiteral("color_text_placeholder"), textPlaceholder},
        {QStringLiteral("color_text_disabled"), textDisabled},
        {QStringLiteral("color_success"), success},
        {QStringLiteral("color_warning"), warning},
        {QStringLiteral("color_error"), error},
        {QStringLiteral("color_info"), info},
        {QStringLiteral("color_icon"), icon},
        {QStringLiteral("color_icon_disabled"), iconDisabled},
        {QStringLiteral("radius_md"), radiusMd},
        {QStringLiteral("font_family_base"), fontFamilyBase},
        {QStringLiteral("font_size_base"), fontSizeBase},
        {QStringLiteral("font_size_middle"), fontSizeMiddle},
        {QStringLiteral("font_size_workspace_tab"), fontSizeWorkspaceTab},
        {QStringLiteral("border_width"), borderWidth},
    };
}

ThemeService::ThemeService(QApplication &application)
    : QObject(&application), m_application(application)
{
    // Use the same base widget style for every application using our theme.
    m_application.setStyle(QStringLiteral("Fusion"));
    new ChamferStyle(this);
}

ThemeId ThemeService::current() const
{
    return m_current;
}

QString ThemeService::customPrimary() const
{
    return m_customPrimary;
}

void ThemeService::setCurrent(ThemeId theme)
{
    if (m_current == theme) return;
    m_current = theme;
    emit themeChanged(theme);
}

void ThemeService::setCustomPrimary(const QString &customPrimary)
{
    const QString normalized = customPrimary.trimmed();
    if (m_customPrimary == normalized) return;
    m_customPrimary = normalized;
}

ThemeTokens ThemeService::currentTokens() const
{
    return tokens(m_current, m_customPrimary);
}

namespace {

QString loadStyleSheetTemplate()
{
    static std::once_flag resourceInitialization;
    std::call_once(resourceInitialization, initializeDarkeyeUiResourceCollection);
    QFile styleFile(QStringLiteral(":/styles/theme.qss"));
    if (!styleFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        qWarning() << "Cannot load theme resource";
        return {};
    }

    QString styleSheet = QString::fromUtf8(styleFile.readAll());
    // Business widgets may change objectName for lookup/testing. Keep the original
    // ID selectors (and their priority), and add stable component selectors.
    const QRegularExpression componentId(QStringLiteral(
        "#(DesignButton|DesignInput|DesignTextEdit|DesignPlainTextEdit|DesignComboBox|DesignLabel)(?![A-Za-z0-9_])"));
    const QRegularExpression comments(QStringLiteral("/\\*.*?\\*/"),
                                     QRegularExpression::DotMatchesEverythingOption);
    QStringList rules = styleSheet.split(QLatin1Char('}'));
    for (QString &rule : rules) {
        const qsizetype brace = rule.indexOf(QLatin1Char('{'));
        if (brace < 0) continue;
        QString selectors = rule.left(brace);
        selectors.remove(comments);
        QStringList aliases;
        for (QString selector : selectors.split(QLatin1Char(','))) {
            if (!selector.contains(componentId)) continue;
            selector.replace(componentId, QStringLiteral("[darkeyeStyle=\"\\1\"]"));
            aliases.append(selector);
        }
        if (!aliases.isEmpty()) {
            rule.insert(brace, QLatin1Char(',') + aliases.join(QLatin1Char(',')));
        }
    }
    return ChamferStyle::flattenCorners(rules.join(QLatin1Char('}')));
}

} // namespace

bool ThemeService::setTheme(ThemeId theme, const QString &customPrimary)
{
    static const QString styleSheetTemplate = loadStyleSheetTemplate();
    if (styleSheetTemplate.isEmpty()) return false;

    const QColor requested(customPrimary.trimmed());
    const QString nextPrimary = requested.isValid()
                                    && (theme == ThemeId::Light || theme == ThemeId::Dark)
                                ? requested.name()
                                : QString();
    const ThemeTokens values = tokens(theme, nextPrimary);
    QString styleSheet = styleSheetTemplate;
    const QHash<QString, QString> replacements = {
        {"@PRIMARY@", values.primary},
        {"@PRIMARY_HOVER@", values.primaryHover},
        {"@BACKGROUND@", values.background},
        {"@INPUT_BACKGROUND@", values.inputBackground},
        {"@PAGE_BACKGROUND@", values.pageBackground},
        {"@BORDER@", values.border},
        {"@BORDER_FOCUS@", values.borderFocus},
        {"@TEXT@", values.text},
        {"@TEXT_INVERSE@", values.textInverse},
        {"@TEXT_PLACEHOLDER@", values.textPlaceholder},
        {"@TEXT_DISABLED@", values.textDisabled},
        {"@SUCCESS@", values.success},
        {"@WARNING@", values.warning},
        {"@ERROR@", values.error},
        {"@INFO@", values.info},
        {"@ICON@", values.icon},
        {"@ICON_DISABLED@", values.iconDisabled},
        {"@FONT_SIZE_MIDDLE@", values.fontSizeMiddle},
        {"@FONT_SIZE_BASE@", values.fontSizeBase},
        {"@RADIUS_MD@", values.radiusMd},
        {"@FONT_FAMILY_BASE@", values.fontFamilyBase},
        {"@BORDER_WIDTH@", values.borderWidth},
    };
    for (auto iterator = replacements.cbegin(); iterator != replacements.cend(); ++iterator) {
        styleSheet.replace(iterator.key(), iterator.value());
    }
    if (m_application.styleSheet() != styleSheet) m_application.setStyleSheet(styleSheet);
    const bool changed = m_current != theme || m_customPrimary != nextPrimary;
    m_current = theme;
    m_customPrimary = nextPrimary;
    if (changed) emit themeChanged(theme);
    return true;
}

ThemeId ThemeService::fromSettings(const QString &value)
{
    const QString normalized = value.trimmed().toUpper();
    if (normalized == "DARK") return ThemeId::Dark;
    if (normalized == "RED") return ThemeId::Red;
    if (normalized == "GREEN") return ThemeId::Green;
    if (normalized == "YELLOW") return ThemeId::Yellow;
    if (normalized == "BLUE") return ThemeId::Blue;
    if (normalized == "PURPLE") return ThemeId::Purple;
    return ThemeId::Light;
}

QString ThemeService::toSettings(ThemeId theme)
{
    switch (theme) {
    case ThemeId::Dark: return QStringLiteral("DARK");
    case ThemeId::Red: return QStringLiteral("RED");
    case ThemeId::Green: return QStringLiteral("GREEN");
    case ThemeId::Yellow: return QStringLiteral("YELLOW");
    case ThemeId::Blue: return QStringLiteral("BLUE");
    case ThemeId::Purple: return QStringLiteral("PURPLE");
    case ThemeId::Light: return QStringLiteral("LIGHT");
    }
    return QStringLiteral("LIGHT");
}

QString ThemeService::displayName(ThemeId theme)
{
    switch (theme) {
    case ThemeId::Light: return QStringLiteral("亮色");
    case ThemeId::Dark: return QStringLiteral("暗色");
    case ThemeId::Red: return QStringLiteral("红色");
    case ThemeId::Green: return QStringLiteral("绿色");
    case ThemeId::Yellow: return QStringLiteral("黄色");
    case ThemeId::Blue: return QStringLiteral("蓝色");
    case ThemeId::Purple: return QStringLiteral("紫色");
    }
    return QStringLiteral("亮色");
}

QVector<ThemeId> ThemeService::availableThemes()
{
    return {ThemeId::Light, ThemeId::Dark, ThemeId::Red, ThemeId::Green,
            ThemeId::Yellow, ThemeId::Blue, ThemeId::Purple};
}

ThemeTokens ThemeService::tokens(ThemeId theme, const QString &customPrimary)
{
    ThemeTokens result = baseTokens(theme);
    if (theme == ThemeId::Blue)
    {
        result.fontSizeWorkspaceTab = QStringLiteral("16px");
    }
    if (theme != ThemeId::Light && theme != ThemeId::Dark) {
        return result;
    }

    const QColor primary(customPrimary.trimmed());
    if (!primary.isValid()) {
        return result;
    }

    float hue = 0.0F;
    float saturation = 0.0F;
    float lightness = 0.0F;
    float alpha = 0.0F;
    primary.getHslF(&hue, &saturation, &lightness, &alpha);
    result.primary = primary.name();
    result.borderFocus = result.primary;
    const float hoverLightness =
        theme == ThemeId::Dark ? lightness + 0.15F : lightness * 0.9F;
    result.primaryHover =
        QColor::fromHslF(hue, saturation, qBound(0.0F, hoverLightness, 1.0F), alpha)
            .name();
    result.info =
        QColor::fromHslF(hue, qBound(0.0F, saturation * 0.95F, 1.0F),
                         qBound(0.0F,
                                lightness
                                    + (theme == ThemeId::Dark ? 0.08F : -0.05F),
                                1.0F),
                         alpha)
            .name();
    result.inputBackground =
        QColor::fromHslF(hue, theme == ThemeId::Dark ? 0.03 : 0.10,
                         theme == ThemeId::Dark ? 0.18 : 0.88)
            .name();
    return result;
}

} // namespace darkeye
