#include "Application.h"

#include "app/LogService.h"
#include "app/Resources.h"
#include "crawler/ManagedCollector.h"
#include "darkeye_ui/theme/ThemeService.h"
#include "http/LocalApiServer.h"
#include "MainWindow.h"
#include "graph_view/ForceViewRhiWidget.h"
#include "dvd_scene/DvdSceneWidget.h"
#include "dvd_scene/DvdModelAsset.h"
#include "services/UpdateService.h"
#include "services/LlamaRuntime.h"
#include "ui/dialogs/TermsDialog.h"

#include <QColor>
#include <QDate>
#include <QDesktopServices>
#include <QDir>
#include <QEventLoop>
#include <QIcon>
#include <QMessageBox>
#include <QFileInfo>
#include <QProcess>
#include <QPixmap>
#include <QSettings>
#include <QStackedWidget>
#include <QSplashScreen>
#include <QThread>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>

namespace darkeye {

namespace
{

QString latestManifestUrl()
{
    const QString configPath = QDir(settings::Paths().configDirectory())
                                   .filePath(QStringLiteral("update.ini"));
    QSettings config(configPath, QSettings::IniFormat);
    const QString configured = config.value(QStringLiteral("Update/LatestJsonUrl")).toString().trimmed();
    return configured.isEmpty() ? QStringLiteral("https://darkeye.win/latest.json") : configured;
}

QString currentIsoWeek()
{
    int year = 0;
    const int week = QDate::currentDate().weekNumber(&year);
    return QStringLiteral("%1-%2").arg(year).arg(week, 2, 10, QLatin1Char('0'));
}

#ifdef Q_OS_WIN
bool startUpdater()
{
    const QString updater = QDir(QCoreApplication::applicationDirPath())
                                .filePath(QStringLiteral("DarkEyeUpdater.exe"));
    if (!QFileInfo::exists(updater))
        return false;
    const QStringList arguments = {
        QStringLiteral("--install-dir"), QCoreApplication::applicationDirPath(),
        QStringLiteral("--current-version"), QStringLiteral(DARKEYE_VERSION),
        QStringLiteral("--main-exe"), QFileInfo(QCoreApplication::applicationFilePath()).fileName(),
        QStringLiteral("--latest-json-url"), latestManifestUrl(),
        QStringLiteral("--keep"), QStringLiteral("data"),
        QStringLiteral("--pid"), QString::number(QCoreApplication::applicationPid())};
    return QProcess::startDetached(updater, arguments, QCoreApplication::applicationDirPath());
}
#endif

} // namespace

Application::Application(int &argc, char **argv)
    : m_application(argc, argv), m_themeService(m_application)
{
    Resources::ensureInitialized();
    configureIdentity();
    QString argumentError;
    if (!settings::Paths::configureFromArguments(m_application.arguments(), &argumentError)) {
        qCritical().noquote() << argumentError;
        m_startupExitCode = 2;
        return;
    }
    m_paths = settings::Paths();
    m_application.setWindowIcon(QIcon(QStringLiteral(":/icons/logo.svg")));
    const QPixmap logo(QStringLiteral(":/icons/logo.svg"));
    m_splash = std::make_unique<QSplashScreen>(logo);
    m_splash->setAttribute(Qt::WA_TranslucentBackground);
    m_splash->setObjectName(QStringLiteral("startupSplash"));
    m_splash->setEnabled(false);
    m_splash->show();
    reportStartupStatus(QStringLiteral("正在准备运行环境"));

    QString errorMessage;
    if (!m_paths.ensureRuntimeDirectories(&errorMessage)) {
        m_splash->close();
        QMessageBox::critical(nullptr, QStringLiteral("Darkeye 启动失败"), errorMessage);
        return;
    }
    if (!settings::ensureDefaults(&errorMessage)) {
        m_splash->close();
        QMessageBox::critical(nullptr, QStringLiteral("Darkeye 启动失败"), errorMessage);
        return;
    }

    LogService::initialize(m_paths.logFile());
    qInfo() << "Darkeye C++ starting" << QCoreApplication::applicationVersion();
    qInfo() << "Runtime mode:" << (m_paths.isPortable() ? "portable" : "standard");
    qInfo() << "Runtime data directory:" << m_paths.dataDirectory();
    qInfo() << "Configuration directory:" << m_paths.configDirectory();
    qInfo() << "AV wiki directory:" << m_paths.wikiDirectory();
    qInfo() << "Public database:" << m_paths.publicDatabase();
}

void Application::reportStartupStatus(const QString &text)
{
    qInfo().noquote() << "[startup]" << text;
    if (m_splash && m_splash->isVisible()) {
        m_splash->showMessage(text);
        m_application.processEvents(QEventLoop::ExcludeUserInputEvents);
    }
}

// Keep first-launch consent ahead of database and main-window construction,
// matching the Python entry point. Background services start after the splash.
int Application::run()
{
    if (m_startupExitCode != 0) return m_startupExitCode;
    if (!m_splash || !m_splash->isVisible())
        return 1;
    AppSettings appSettings = settings::app();
    if (appSettings.firstLaunch) {
        m_splash->hide();
        TermsDialog terms;
        if (terms.exec() != QDialog::Accepted) {
            m_splash->close();
            return 0;
        }
        appSettings.firstLaunch = false;
        settings::saveApp(appSettings);
        m_splash->show();
    }
    QString errorMessage;
    reportStartupStatus(QStringLiteral("正在初始化数据库"));
    if (!m_databaseManager.initialize(m_paths, &errorMessage)) {
        m_splash->close();
        qCritical() << "Database initialization failed:" << errorMessage;
        QMessageBox::critical(nullptr, QStringLiteral("Darkeye 数据库错误"), errorMessage);
        return 1;
    }
    reportStartupStatus(QStringLiteral("正在加载样式"));
    applyInitialTheme();
    reportStartupStatus(QStringLiteral("主窗口加载"));
    m_mainWindow = std::make_unique<MainWindow>(
        m_themeService, m_databaseManager.publicConnection().database(),
        m_databaseManager.privateConnection().database(), m_paths);
    m_localApiServer = std::make_unique<LocalApiServer>(m_databaseManager.publicConnection().database());
    m_managedCollector = std::make_unique<ManagedCollector>();
    m_mainWindow->setLocalApiServer(*m_localApiServer);
    reportStartupStatus(QStringLiteral("正在预热图形引擎"));
    prepareGraphicsPrewarm();
    m_mainWindow->showInitial();
    finishGraphicsPrewarm();
    reportStartupStatus(QStringLiteral("正在准备书架首帧"));
    m_mainWindow->preloadShelfPage(true);
    m_splash->finish(m_mainWindow.get());
    m_splash.reset();
    reportStartupStatus(QStringLiteral("主窗口已显示，后台初始化继续进行"));
    QTimer::singleShot(0, &m_application, [this] { startBackgroundServices(); });
    return m_application.exec();
}

void Application::prepareGraphicsPrewarm()
{
    // Render small native DVD and ForceView scenes during startup.
    // Keep both QRhi users in the top-level widget tree long enough to submit
    // real frames so the first visible shelf/graph page does not initialize the
    // graphics stack on the user's click.
    auto *pageStack = m_mainWindow->centralWidget()->findChild<QStackedWidget *>(
        QStringLiteral("mainPages"));
    if (pageStack == nullptr) {
        qWarning() << "Graphics prewarm skipped: main page stack is unavailable";
        return;
    }
    m_graphicsPrewarmWindow = std::make_unique<QWidget>(pageStack);
    m_graphicsPrewarmWindow->setAttribute(Qt::WA_TransparentForMouseEvents);
    m_graphicsPrewarmWindow->setGeometry(0, 0, 240, 248);

    auto *layout = new QVBoxLayout(m_graphicsPrewarmWindow.get());
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    auto *dvdPrewarm = new dvd_scene::DvdSceneWidget(m_graphicsPrewarmWindow.get());
    dvdPrewarm->setFixedHeight(48);
    render3d::GltfLoader loader;
    const auto model=dvd_scene::loadDvdModel(loader,QStringLiteral(":/assets/dvd/dvd.glb"));
    if(model && dvdPrewarm->setModel(model.asset))
        dvdPrewarm->setItems({{QStringLiteral("prewarm"),{},QStringLiteral("DVD"),0,false}});
    layout->addWidget(dvdPrewarm);

    m_graphPrewarmView = new ForceViewRhiWidget(m_graphicsPrewarmWindow.get());
    layout->addWidget(m_graphPrewarmView);
    m_graphPrewarmView->setGraph(
        1, {}, {0.0f, 0.0f}, {QStringLiteral("prewarm")},
        {QStringLiteral("prewarm")}, {4.0f}, {QColor(QStringLiteral("#808080"))});
    QObject::connect(m_graphPrewarmView, &ForceViewRhiWidget::firstFrameSubmitted,
                     m_graphPrewarmView,
                     [this] { m_graphPrewarmFrameSubmitted = true; });
    m_graphicsPrewarmWindow->show();
    m_graphicsPrewarmWindow->lower();
}

void Application::finishGraphicsPrewarm()
{
    if (m_graphicsPrewarmWindow == nullptr)
        return;
    for (int frame = 0; frame < 10; ++frame) {
        m_application.processEvents(QEventLoop::AllEvents, 50);
        QThread::msleep(16);
    }
    if (m_graphPrewarmView != nullptr)
        m_graphPrewarmView->pauseSimulation();
    // Prewarming requires one visible frame on Windows, but the helper must
    // not remain in the compositing tree afterwards: transparent application
    // widgets would otherwise reveal it.
    m_graphicsPrewarmWindow->hide();
    if (!m_graphPrewarmFrameSubmitted) {
        qWarning() << "Graph renderer prewarm did not submit a frame";
    }
}

Application::~Application()
{
    if (m_localApiServer) m_localApiServer->stop();
    if (m_managedCollector) m_managedCollector->stop();
    stopLlamaServer();
    qInfo() << "Darkeye C++ stopped";
    LogService::shutdown();
}

void Application::startBackgroundServices()
{
    reportStartupStatus(QStringLiteral("正在后台启动本地 API 服务"));
    QString errorMessage;
    if (!m_localApiServer->start(56789, &errorMessage))
        qWarning() << "Local HTTP API did not start:" << errorMessage;
    else
    {
        qInfo() << "Local HTTP API listening on 127.0.0.1:" << m_localApiServer->port();
        reportStartupStatus(QStringLiteral("本地 API 服务已启动"));
    }
    QTimer::singleShot(900, &m_application, [this] { startCollector(); });
    QTimer::singleShot(1200, &m_application, [this] { startLlamaServer(); });
    QTimer::singleShot(2000, &m_application, [this] { checkForUpdatesAutomatically(); });
}

void Application::startCollector()
{
    const CrawlerSettings crawlerSettings = settings::crawler();
    if (!crawlerSettings.autoStartCollector) return;
    if (crawlerSettings.collectorExecutable.trimmed().isEmpty()) {
        reportStartupStatus(QStringLiteral("采集器自启动已跳过：未配置可执行文件"));
        return;
    }
    reportStartupStatus(QStringLiteral("正在后台启动采集器服务"));
    QString errorMessage;
    if (!m_managedCollector->start(crawlerSettings.collectorExecutable, &errorMessage)) {
        qWarning() << "Collector did not start:" << errorMessage;
        reportStartupStatus(QStringLiteral("采集器自启动失败：%1").arg(errorMessage));
    } else {
        reportStartupStatus(QStringLiteral("采集器服务已启动"));
    }
}

void Application::startLlamaServer()
{
    const TranslationSettings translation = settings::translation();
    const LlamaCppSettings &llama = translation.llama;
    if (!llama.autoStart) return;
    if (llama.serverExecutable.trimmed().isEmpty() || llama.modelPath.trimmed().isEmpty()) {
        qWarning() << "llama-server auto start skipped: executable or model is not configured";
        return;
    }
    reportStartupStatus(QStringLiteral("正在后台启动 llama 服务"));
    auto &runtime = get_llama_runtime();
    QObject::connect(&runtime, &LlamaRuntime::statusChanged, &m_application,
                     [this](const QString &status) {
        reportStartupStatus(QStringLiteral("llama 服务：%1").arg(status));
    });
    const QString error = runtime.start(llama);
    if (!error.isEmpty())
        reportStartupStatus(QStringLiteral("llama 自启动失败：%1").arg(error));
}

void Application::checkForUpdatesAutomatically()
{
    AppSettings appSettings = settings::app();
    if (!appSettings.update.automaticCheck)
        return;

    const QString week = currentIsoWeek();
    if (appSettings.update.lastAutoCheckWeek == week)
        return;

    // Persist the attempt before issuing the request. This matches the Python
    // client's once-per-week policy and avoids a network failure delaying startup
    // with another check on every launch.
    appSettings.update.lastAutoCheckWeek = week;
    settings::saveApp(appSettings);

    auto *service = new UpdateService(&m_application);
    QObject::connect(service, &UpdateService::finished, &m_application,
                     [this, service, showNotification = appSettings.update.updateNotification]
                     (const utils::UpdateCheckResult &result)
                     {
                service->deleteLater();
                if (!result.success) {
                    qWarning() << "Automatic update check failed:" << result.message;
                    return;
                }
                if (!result.updateAvailable || !showNotification || m_mainWindow == nullptr)
                    return;

#ifndef Q_OS_WIN
                if (QMessageBox::question(m_mainWindow.get(), QStringLiteral("发现新版本"),
                        result.message + QStringLiteral("\n\n打开发布页下载适合本系统的版本？")) == QMessageBox::Yes)
                    QDesktopServices::openUrl(QUrl(QStringLiteral("https://github.com/gty5678/darkeye-cpp/releases")));
#else
                const QString prompt = result.message
                    + QStringLiteral("\n\n是否立即更新？软件将退出以完成更新。");
                if (QMessageBox::question(m_mainWindow.get(), QStringLiteral("发现新版本"), prompt,
                                          QMessageBox::Yes | QMessageBox::No,
                                          QMessageBox::Yes) != QMessageBox::Yes)
                    return;
                if (!startUpdater()) {
                    QMessageBox::critical(m_mainWindow.get(), QStringLiteral("更新失败"),
                                          QStringLiteral("无法启动更新程序。"));
                    return;
                }
                QCoreApplication::quit();
#endif
                     });
    service->check(QUrl::fromUserInput(latestManifestUrl()), QStringLiteral(DARKEYE_VERSION));
}

void Application::stopLlamaServer()
{
    get_llama_runtime().stop();
}

void Application::configureIdentity()
{
    QCoreApplication::setOrganizationName(QStringLiteral("Darkeye"));
    QCoreApplication::setApplicationName(QStringLiteral("Darkeye"));
    QCoreApplication::setApplicationVersion(QStringLiteral(DARKEYE_VERSION));
}

void Application::applyInitialTheme()
{
    const AppSettings appSettings = settings::app();
    m_themeService.setTheme(ThemeService::fromSettings(appSettings.themeId),
                            appSettings.customPrimary);
}

} // namespace darkeye
