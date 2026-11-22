#pragma once

#include "settings/Paths.h"
#include "darkeye_ui/theme/ThemeService.h"
#include "database/DatabaseManager.h"
#include "settings/Settings.h"

#include <QApplication>
#include <memory>

class QWidget;
class ForceViewRhiWidget;
class QSplashScreen;

namespace darkeye {

class MainWindow;
class LocalApiServer;
class ManagedCollector;

class Application final
{
public:
    Application(int &argc, char **argv);
    ~Application();

    int run();

private:
    void configureIdentity();
    void applyInitialTheme();
    void prepareGraphicsPrewarm();
    void finishGraphicsPrewarm();
    void startBackgroundServices();
    void startCollector();
    void startLlamaServer();
    void reportStartupStatus(const QString &text);
    void checkForUpdatesAutomatically();
    void stopLlamaServer();

    QApplication m_application;
    int m_startupExitCode = 0;
    settings::Paths m_paths;
    ThemeService m_themeService;
    DatabaseManager m_databaseManager;
    std::unique_ptr<QSplashScreen> m_splash;
    std::unique_ptr<MainWindow> m_mainWindow;
    std::unique_ptr<QWidget> m_graphicsPrewarmWindow;
    ForceViewRhiWidget *m_graphPrewarmView = nullptr;
    bool m_graphPrewarmFrameSubmitted = false;
    std::unique_ptr<LocalApiServer> m_localApiServer;
    std::unique_ptr<ManagedCollector> m_managedCollector;
};

} // namespace darkeye
