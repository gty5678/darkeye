#pragma once

#include <QString>

namespace darkeye
{

class ThemeService;

class ThemeContext final
{
public:
    [[nodiscard]] static ThemeService *resolve(ThemeService *themeService,
                                               const QString &caller);
};

} // namespace darkeye
