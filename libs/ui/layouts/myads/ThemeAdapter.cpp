#include "ui/layouts/myads/ThemeAdapter.h"

#include "ui/layouts/myads/WorkspaceWidget.h"

namespace darkeye::myads
{

DockTheme dockThemeFromTokens(const ThemeTokens &tokens)
{
    DockTheme theme;
    theme.background = QColor(tokens.background);
    theme.text = QColor(tokens.text);
    theme.border = QColor(tokens.border);
    theme.primary = QColor(tokens.primary);
    theme.primaryHover = QColor(tokens.primaryHover);
    theme.tabBarBackground = QColor(tokens.pageBackground);
    theme.tabSelected = theme.tabBarBackground;
    theme.closeIcon = QColor(tokens.icon);
    theme.closeHover = QColor(tokens.inputBackground);
    theme.closePressed = QColor(tokens.border);
    theme.fontFamily = tokens.fontFamilyBase;
    theme.tabFontSize = tokens.fontSizeWorkspaceTab.chopped(2).toInt();
    return theme;
}

void bindDarkeyeTheme(WorkspaceWidget *workspace, ThemeService *themeService)
{
    if (workspace == nullptr || themeService == nullptr)
    {
        return;
    }
    const auto apply = [workspace, themeService] {
        workspace->applyTheme(dockThemeFromTokens(
            ThemeService::tokens(themeService->current(), themeService->customPrimary())));
    };
    apply();
    QObject::connect(themeService, &ThemeService::themeChanged, workspace,
                     [apply](ThemeId) { apply(); });
}

} // namespace darkeye::myads
