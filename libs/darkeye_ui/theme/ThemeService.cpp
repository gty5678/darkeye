#include "darkeye_ui/theme/ThemeService.h"
#include "ChamferStyle.h"
#include "darkeye_ui/theme/StylesheetLoader.h"

#include <QApplication>
#include <QColor>
#include <QColorTransform>
#include <QFile>
#include <QPalette>
#include <QPainter>
#include <QProxyStyle>
#include <QResource>
#include <QRegularExpression>
#include <QStringView>
#include <QStyleOption>
#include <QSurfaceFormat>

#include <algorithm>
#include <cmath>
#include <mutex>
#include <utility>

void initializeDarkeyeUiResourceCollection()
{
    Q_INIT_RESOURCE(darkeye_ui_resources);
}

namespace darkeye {
namespace {

auto themeColorMode = ThemeTokens::defaultColorSpace;

struct ThemeInfo {
    ThemeId id;
    QStringView settingsKey;
    QStringView displayName;
};

constexpr ThemeInfo themes[] = {
    {ThemeId::Light, u"LIGHT", u"亮色"},
    {ThemeId::Dark, u"DARK", u"暗色"},
    {ThemeId::Red, u"RED", u"红色"},
    {ThemeId::Green, u"GREEN", u"绿色"},
    {ThemeId::Yellow, u"YELLOW", u"黄色"},
    {ThemeId::Blue, u"BLUE", u"蓝色"},
    {ThemeId::Purple, u"PURPLE", u"紫色"},
};

const ThemeInfo &themeInfo(ThemeId id)
{
    for (const auto &theme : themes)
        if (theme.id == id) return theme;
    return themes[0];
}

ThemeTokens lightTokens()
{
    return {"#00aaff", "#0099ee", "#ffffff", "#ffffff", "#f4f4f4",
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

double luminance(const QColor &color, const QColorSpace &space)
{
    const auto linear = [](double channel) {
        return channel <= 0.04045 ? channel / 12.92
                                  : std::pow((channel + 0.055) / 1.055, 2.4);
    };
    const bool p3 = space.primaries() == QColorSpace::Primaries::DciP3D65;
    return (p3 ? 0.228975 : 0.2126) * linear(color.redF())
        + (p3 ? 0.691739 : 0.7152) * linear(color.greenF())
        + (p3 ? 0.079287 : 0.0722) * linear(color.blueF());
}

QString focusBorder(const ThemeTokens &tokens)
{
    const double surfaces[] = {luminance(QColor(tokens.pageBackground), tokens.colorSpace),
                               luminance(QColor(tokens.background), tokens.colorSpace),
                               luminance(QColor(tokens.inputBackground), tokens.colorSpace)};
    const auto contrast = [&](const QColor &color) {
        const double edge = luminance(color, tokens.colorSpace);
        double minimum = 21.0;
        for (const double surface : surfaces)
            minimum = std::min(minimum, (std::max(edge, surface) + 0.05)
                                           / (std::min(edge, surface) + 0.05));
        return minimum;
    };

    const QColor primary(tokens.primary);
    if (contrast(primary) >= 3.0) return tokens.primary;

    const QColor black(Qt::black);
    const QColor white(Qt::white);
    const QColor target = contrast(black) >= contrast(white) ? black : white;
    // Keep the smallest change towards black or white that makes the edge visible.
    const int endpoint = target.red();
    const auto blend = [&](int step) {
        const auto mix = [&](int channel) {
            return (channel * (255 - step) + endpoint * step + 127) / 255;
        };
        return QColor(mix(primary.red()), mix(primary.green()), mix(primary.blue()));
    };
    int low = 0;
    int high = 255;
    while (high - low > 1) {
        const int middle = (low + high) / 2;
        if (contrast(blend(middle)) >= 3.0)
            high = middle;
        else
            low = middle;
    }
    return blend(high).name();
}

} // namespace

class SelectionIndicatorStyle final : public QProxyStyle
{
public:
    SelectionIndicatorStyle() : QProxyStyle(QStringLiteral("Fusion")) {}

    void setColors(const ThemeTokens &tokens)
    {
        m_primary = QColor(tokens.primary);
        const int gray = qGray(QColor(tokens.border).rgb());
        m_outline = QColor(gray, gray, gray);
    }

    void drawPrimitive(PrimitiveElement element, const QStyleOption *option,
                       QPainter *painter, const QWidget *widget) const override
    {
        if (element != PE_IndicatorCheckBox && element != PE_IndicatorRadioButton
            && element != PE_IndicatorItemViewItemCheck) {
            QProxyStyle::drawPrimitive(element, option, painter, widget);
            return;
        }

        const bool checked = option->state.testFlag(State_On);
        const bool partial = option->state.testFlag(State_NoChange);
        const QRectF bounds(option->rect);
        const QPointF center = bounds.center();
        const qreal radius = qMin(bounds.width(), bounds.height()) / 2 - 0.75;
        const QPolygonF diamond{center + QPointF(0, -radius), center + QPointF(radius, 0),
                                center + QPointF(0, radius), center + QPointF(-radius, 0)};

        painter->save();
        painter->setRenderHint(QPainter::Antialiasing);
        if (!option->state.testFlag(State_Enabled))
            painter->setOpacity(painter->opacity() * 0.5);
        painter->setPen(checked ? QPen(Qt::NoPen) : QPen(m_outline, 1.5));
        painter->setBrush(checked ? QBrush(m_primary) : QBrush(Qt::NoBrush));
        painter->drawPolygon(diamond);
        if (partial) {
            painter->setPen(QPen(m_primary, 1.5));
            painter->drawLine(center - QPointF(radius / 2, 0),
                              center + QPointF(radius / 2, 0));
        }
        painter->restore();
    }

private:
    QColor m_primary;
    QColor m_outline;
};

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
        {QStringLiteral("color_modified"), modified},
        {QStringLiteral("color_image_drop_outline"), imageDropOutline},
        {QStringLiteral("border_width_indicator"), QStringLiteral("3px")},
        {QStringLiteral("border_width_thin"), QStringLiteral("1px")},
        {QStringLiteral("field_action_width"), QStringLiteral("20px")},
        {QStringLiteral("field_content_height"), QStringLiteral("20px")},
        {QStringLiteral("font_size_empty_state_icon"), QStringLiteral("36px")},
        {QStringLiteral("font_size_heading"), QStringLiteral("18px")},
        {QStringLiteral("font_size_rating_icon"), QStringLiteral("28px")},
        {QStringLiteral("font_size_search_input"), QStringLiteral("20px")},
        {QStringLiteral("font_size_search_result"), QStringLiteral("17px")},
        {QStringLiteral("font_size_statistic"), QStringLiteral("30pt")},
        {QStringLiteral("icon_size_md"), QStringLiteral("16px")},
        {QStringLiteral("icon_size_sm"), QStringLiteral("12px")},
        {QStringLiteral("radius_chip"), QStringLiteral("12px")},
        {QStringLiteral("radius_color_picker"), QStringLiteral("3px")},
        {QStringLiteral("radius_compact"), QStringLiteral("4px")},
        {QStringLiteral("radius_progress_chunk"), QStringLiteral("5px")},
        {QStringLiteral("radius_scrollbar"), QStringLiteral("5px")},
        {QStringLiteral("radius_slider_handle"), QStringLiteral("7px")},
        {QStringLiteral("radius_sm"), QStringLiteral("6px")},
        {QStringLiteral("radius_xs"), QStringLiteral("2px")},
        {QStringLiteral("scrollbar_compact_handle_min"), QStringLiteral("28px")},
        {QStringLiteral("scrollbar_compact_width"), QStringLiteral("8px")},
        {QStringLiteral("scrollbar_handle_min"), QStringLiteral("30px")},
        {QStringLiteral("scrollbar_width"), QStringLiteral("10px")},
        {QStringLiteral("slider_handle_margin"), QStringLiteral("-5px")},
        {QStringLiteral("slider_handle_size"), QStringLiteral("14px")},
        {QStringLiteral("slider_track_size"), QStringLiteral("4px")},
        {QStringLiteral("space_1"), QStringLiteral("1px")},
        {QStringLiteral("space_2"), QStringLiteral("2px")},
        {QStringLiteral("space_3"), QStringLiteral("3px")},
        {QStringLiteral("space_4"), QStringLiteral("4px")},
        {QStringLiteral("space_5"), QStringLiteral("5px")},
        {QStringLiteral("space_6"), QStringLiteral("6px")},
        {QStringLiteral("space_8"), QStringLiteral("8px")},
        {QStringLiteral("space_10"), QStringLiteral("10px")},
        {QStringLiteral("space_11"), QStringLiteral("11px")},
        {QStringLiteral("space_12"), QStringLiteral("12px")},
        {QStringLiteral("space_14"), QStringLiteral("14px")},
        {QStringLiteral("space_15"), QStringLiteral("15px")},
        {QStringLiteral("space_18"), QStringLiteral("18px")},
        {QStringLiteral("space_24"), QStringLiteral("24px")},
        {QStringLiteral("widget_size_max"), QStringLiteral("16777215px")},
        {QStringLiteral("workspace_search_input_height"), QStringLiteral("24px")},
    };
}

ThemeTokens ThemeTokens::convertedTo(const QColorSpace &target) const
{
    if (!target.isValid() || colorSpace == target) return *this;

    const auto transform = colorSpace.transformationToColorSpace(target);
    ThemeTokens result = *this;
    result.colorSpace = target;
    constexpr QString ThemeTokens::*colors[] = {
        &ThemeTokens::primary, &ThemeTokens::primaryHover, &ThemeTokens::background,
        &ThemeTokens::inputBackground, &ThemeTokens::pageBackground, &ThemeTokens::border,
        &ThemeTokens::borderFocus, &ThemeTokens::text, &ThemeTokens::textInverse,
        &ThemeTokens::textPlaceholder, &ThemeTokens::textDisabled, &ThemeTokens::success,
        &ThemeTokens::warning, &ThemeTokens::error, &ThemeTokens::info, &ThemeTokens::icon,
        &ThemeTokens::iconDisabled, &ThemeTokens::modified, &ThemeTokens::imageDropOutline,
    };
    for (const auto member : colors) {
        const QColor color(this->*member);
        if (color.isValid()) {
            result.*member = QColor::fromRgba(transform.map(color.rgba())).name(
                color.alpha() == 255 ? QColor::HexRgb : QColor::HexArgb);
        }
    }
    return result;
}

ThemeService::ThemeService(QApplication &application)
    : QObject(&application), m_application(application)
{
    // Use the same base widget style for every application using our theme.
    m_indicatorStyle = new SelectionIndicatorStyle;
    m_indicatorStyle->setColors(currentTokens());
    m_application.setStyle(m_indicatorStyle);
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

    return QString::fromUtf8(styleFile.readAll());
}

QString prepareStyleSheet(QString styleSheet)
{
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
    QString styleSheet = StylesheetLoader::render(styleSheetTemplate, values);
    if (styleSheet.contains(QStringLiteral("{{"))) {
        qWarning() << "Unresolved token in theme stylesheet";
        return false;
    }
    styleSheet = prepareStyleSheet(std::move(styleSheet));
    if (m_indicatorStyle) m_indicatorStyle->setColors(values);
    QPalette palette = m_application.palette();
    const QColor pageBackground(values.pageBackground);
    if (palette.color(QPalette::Window) != pageBackground) {
        palette.setColor(QPalette::Window, pageBackground);
        m_application.setPalette(palette);
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
    for (const auto &theme : themes)
        if (theme.settingsKey == normalized) return theme.id;
    return themes[0].id;
}

QString ThemeService::toSettings(ThemeId theme)
{
    return themeInfo(theme).settingsKey.toString();
}

QString ThemeService::displayName(ThemeId theme)
{
    return themeInfo(theme).displayName.toString();
}

QVector<ThemeId> ThemeService::availableThemes()
{
    QVector<ThemeId> ids;
    for (const auto &theme : themes)
        ids.append(theme.id);
    return ids;
}

QColorSpace ThemeService::outputColorSpace()
{
    const QColorSpace formatSpace = QSurfaceFormat::defaultFormat().colorSpace();
    return formatSpace.isValid() ? formatSpace : QColorSpace(QColorSpace::SRgb);
}

QColorSpace::NamedColorSpace ThemeService::colorMode()
{
    return themeColorMode;
}

void ThemeService::setColorMode(QColorSpace::NamedColorSpace mode)
{
    themeColorMode = mode;
}

ThemeTokens ThemeService::sourceTokens(ThemeId theme, const QString &customPrimary)
{
    ThemeTokens result = baseTokens(theme).convertedTo(QColorSpace(colorMode()));
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
    return result;
}

ThemeTokens ThemeService::tokens(ThemeId theme, const QString &customPrimary)
{
    auto result = sourceTokens(theme, customPrimary).convertedTo(outputColorSpace());
    result.borderFocus = focusBorder(result);
    return result;
}

} // namespace darkeye
