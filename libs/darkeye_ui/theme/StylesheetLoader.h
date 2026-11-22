#pragma once

#include "darkeye_ui/theme/ThemeService.h"

#include <QString>

namespace darkeye
{

class StylesheetLoader final
{
public:
    [[nodiscard]] static QString load(const QString &templatePath,
                                      const ThemeTokens &tokens,
                                      QString *errorMessage = nullptr);
    [[nodiscard]] static QString render(QString stylesheetTemplate,
                                        const ThemeTokens &tokens);
};

} // namespace darkeye
