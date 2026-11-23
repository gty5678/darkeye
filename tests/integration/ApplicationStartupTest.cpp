#include "Application.h"
#include "MainWindow.h"
#include "ui/layouts/myads/WorkspaceWidget.h"
#include "dvd_scene/DvdSceneWidget.h"
#include "database/SchemaManager.h"
#include "database/SqliteConnection.h"
#include "database/repositories/WorkRepository.h"
#include "settings/Settings.h"
#include "darkeye_ui/components/ColorWheel.h"
#include "ui/dialogs/TermsDialog.h"

#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QSettings>
#include <QFile>
#include <QScreen>
#include <QSplashScreen>
#include <QTemporaryDir>
#include <QTimer>
#include <QWindow>
#include <QtTest>

// Application owns QApplication, so this integration test uses its own main.
// Each process checks one consent path using fresh temporary databases.
int main(int argc, char **argv)
{
    const bool accept = argc > 1 && QByteArray(argv[1]) == "accept";
    QTemporaryDir databases;
    if (!databases.isValid()) return 4;
    darkeye::Application application(argc, argv);
    const darkeye::settings::Paths runtimePaths;
    const QStringList arguments = QCoreApplication::arguments();
    for (const auto &entry : {qMakePair(QStringLiteral("--config-dir"), runtimePaths.configDirectory()),
                              qMakePair(QStringLiteral("--avwiki-dir"), runtimePaths.wikiDirectory()),
                              qMakePair(QStringLiteral("--data-dir"), runtimePaths.dataDirectory())}) {
        const int index = arguments.indexOf(entry.first);
        if (index < 0 || index + 1 >= arguments.size()
            || entry.second != QDir(arguments.at(index + 1)).absolutePath()) return 9;
    }
    if (QFileInfo::exists(QDir(runtimePaths.dataDirectory()).filePath(QStringLiteral("avwiki")))) return 10;
    QSplashScreen *splash = nullptr;
    for (QWidget *widget : QApplication::topLevelWidgets()) {
        if (widget->objectName() == QStringLiteral("startupSplash"))
            splash = qobject_cast<QSplashScreen *>(widget);
    }
    if (!splash || !splash->isVisible() || splash->isEnabled()
        || splash->pixmap().isNull()
        || !splash->testAttribute(Qt::WA_TranslucentBackground)
        || splash->pixmap().toImage().pixelColor(0, 0).alpha() != 0
        || splash->message() != QStringLiteral("正在准备运行环境"))
        return 2;
    const auto configuredApp = darkeye::settings::app(runtimePaths.settingsFile());
    const auto gamut = configuredApp.colorMode == QStringLiteral("SRGB")
        ? QColorSpace::SRgb : QColorSpace::DisplayP3;
#ifdef Q_OS_MACOS
    const auto actual = splash->windowHandle()->format().colorSpace();
    if (actual.primaries() != QColorSpace(gamut).primaries()) return 11;
#endif
    if (!splash->grab().save(QDir(QCoreApplication::applicationDirPath())
                               .filePath(QStringLiteral("startup-splash.png"))))
        return 3;

    darkeye::settings::Paths paths;
    QSettings config(paths.settingsFile(), QSettings::IniFormat);
    // This test covers a fresh startup, independent of the previous run's tabs.
    QFile::remove(QDir(paths.dataDirectory()).filePath(QStringLiteral("workspace-v2.json")));
    config.remove(QStringLiteral("workspace"));
    const QString publicDb = databases.filePath(QStringLiteral("public.db"));
    const QString privateDb = databases.filePath(QStringLiteral("private.db"));
    config.setValue(QStringLiteral("Paths/Database"), publicDb);
    config.setValue(QStringLiteral("Paths/PrivateDatabase"), privateDb);
    config.sync();
    auto appSettings = darkeye::settings::app();
    appSettings.firstLaunch = true;
    appSettings.maximized = false;
    appSettings.update.checkAndNotify = false;
    darkeye::settings::saveApp(appSettings);

    bool consentSeen = false;
    bool mainWindowSeen = false;
    bool splashClosed = false;
    bool failed = false;
    QTimer observer;
    observer.setInterval(20);
    QObject::connect(&observer, &QTimer::timeout, qApp, [&] {
        for (QWidget *widget : QApplication::topLevelWidgets()) {
            if (auto *terms = qobject_cast<darkeye::TermsDialog *>(widget);
                terms && terms->isVisible()) {
                consentSeen = true;
                // Rejecting consent must happen before databases are opened.
                failed |= QFileInfo::exists(publicDb) || QFileInfo::exists(privateDb);
                accept ? terms->accept() : terms->reject();
                if (accept) {
                    // Seed a real DVD for the first visit to the shelf after consent.
                    darkeye::SqliteConnection connection;
                    QString error;
                    if (!connection.open(publicDb, false, &error)
                        || !darkeye::SchemaManager::initializeEmptyDatabase(
                            connection, darkeye::DatabaseKind::Public, &error)) {
                        failed = true;
                    } else {
                        darkeye::Work work;
                        work.serialNumber = QStringLiteral("STARTUP-001");
                        darkeye::WorkRepository works(connection.database());
                        QList<qint64> tags;
                        for (const auto &tag : works.tagOptions())
                            if (tag.name == QStringLiteral("绿色封面")) tags.append(tag.id);
                        failed |= !works.insertComplete(work, {}, {}, tags, &error).has_value();
                    }
                }
                return;
            }
            if (auto *window = dynamic_cast<darkeye::MainWindow *>(widget);
                window && window->isVisible() && !mainWindowSeen) {
                // Prewarm pumps events before Application::run enters exec().
                // Do not finish the test while startup still owns the splash.
                bool starting = false;
                for (QWidget *top : QApplication::topLevelWidgets())
                    starting |= top->objectName() == QStringLiteral("startupSplash");
                if (starting) return;
                if (const auto primary = darkeye::OklchColor::fromString(configuredApp.customPrimaryOklch)) {
                    const auto *themes = qApp->findChild<darkeye::ThemeService *>();
                    failed |= !themes || QColor(themes->customPrimary()) != QColor(primary->hex(gamut));
                }
                mainWindowSeen = true;
                // Check after startup's finish() has returned and the first
                // background services have had a chance to execute.
                QTimer::singleShot(1500, qApp, [&, window] {
                    if (const auto *screen = window->screen()) {
                        const QSize expected = screen->availableGeometry().size() * 0.85;
                        if (window->size() != expected) {
                            qWarning() << "Unexpected visible startup size:" << window->size()
                                       << "expected" << expected;
                            failed = true;
                        }
                    } else {
                        failed = true;
                    }
                    splashClosed = true;
                    for (QWidget *topLevel : QApplication::topLevelWidgets())
                        if (topLevel->objectName() == QStringLiteral("startupSplash"))
                            splashClosed = false;
                    using darkeye::myads::SidebarSide;
                    auto *workspace = window->findChild<darkeye::myads::WorkspaceWidget *>();
                    auto *scene = window->findChild<darkeye::dvd_scene::DvdSceneWidget *>(
                        QStringLiteral("ShelfDvdScene"));
                    if (!workspace || !scene) {
                        failed = true;
                    } else {
                        failed |= workspace->activePane()->currentContentId() != QStringLiteral("shelf")
                            || !workspace->isSidebarOpen(SidebarSide::Left)
                            || !workspace->isSidebarOpen(SidebarSide::Right);
                        auto *renderer = scene->renderWidget();
                        QElapsedTimer frameWait;
                        frameWait.start();
                        while (renderer->statistics().submittedFrames == 0 && frameWait.elapsed() < 3000)
                            QTest::qWait(20);
                        failed |= !scene->isVisible()
                            || scene->itemCount() != 1 || scene->visibleItemCount() != 1
                            || renderer->statistics().submittedFrames == 0
                            || renderer->statistics().resourceGenerations == 0
                            || renderer->statistics().pixelSize.isEmpty()
                            || renderer->statistics().samples != 4
                            || !renderer->lastError().isEmpty();
                    }
                    QCoreApplication::quit();
                });
            }
        }
    });
    observer.start();
    QTimer::singleShot(20000, qApp, [&] {
        qWarning() << "Startup test timed out; consent/main window/splash closed:"
                   << consentSeen << mainWindowSeen << splashClosed;
        failed = true;
        QCoreApplication::exit(5);
    });
    const int result = application.run();
    if (result != 0 || failed || !consentSeen) return 6;
    if (accept) {
        if (!mainWindowSeen || !splashClosed || darkeye::settings::app().firstLaunch
            || !QFileInfo::exists(publicDb) || !QFileInfo::exists(privateDb))
            return 7;
    } else if (mainWindowSeen || !darkeye::settings::app().firstLaunch
               || QFileInfo::exists(publicDb) || QFileInfo::exists(privateDb)) {
        return 8;
    }
    return 0;
}
