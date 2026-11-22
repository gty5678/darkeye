#pragma once

#include "darkeye_ui/theme/ThemeService.h"
#include "ui/layouts/myads/DockTheme.h"

namespace darkeye::myads
{

class WorkspaceWidget;

[[nodiscard]] DockTheme dockThemeFromTokens(const ThemeTokens &tokens);
void bindDarkeyeTheme(WorkspaceWidget *workspace, ThemeService *themeService);

} // namespace darkeye::myads
