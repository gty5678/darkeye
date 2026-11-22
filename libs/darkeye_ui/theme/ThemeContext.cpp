#include "darkeye_ui/theme/ThemeContext.h"

#include "darkeye_ui/base/WarningOnce.h"

namespace darkeye
{

ThemeService *ThemeContext::resolve(ThemeService *themeService, const QString &caller)
{
    if (themeService == nullptr)
    {
        WarningOnce::warn(
            caller + QStringLiteral(":missing_theme_service"),
            QStringLiteral("%1: ThemeService is unavailable; using light-token fallback.")
                .arg(caller));
    }
    return themeService;
}

} // namespace darkeye
