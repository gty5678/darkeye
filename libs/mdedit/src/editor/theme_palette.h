#pragma once

#include <QPalette>

#include <optional>

namespace mdedit {

inline QColor themeColor(const std::optional<QPalette> &theme, QPalette::ColorRole role,
                         const QColor &fallback)
{
    return theme ? theme->color(role) : fallback;
}

inline QColor syntaxColor(const std::optional<QPalette> &theme, const QColor &original)
{
    if (!theme || theme->color(QPalette::Base).lightness() >= 128) return original;
    const QColor text = theme->color(QPalette::Text);
    return QColor::fromRgbF((original.redF() + text.redF()) / 2,
                            (original.greenF() + text.greenF()) / 2,
                            (original.blueF() + text.blueF()) / 2);
}

} // namespace mdedit
