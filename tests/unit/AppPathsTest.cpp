#include "settings/Paths.h"
#include "settings/Settings.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSettings>
#include <QScopeGuard>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QtTest>

class AppPathsTest final : public QObject
{
    Q_OBJECT

private slots:
    void usesPortableLayoutByDefault();
    void resolvesBundleAndUnixResources();
    void detectsPortableConfigurationAndSystemDataLocation();
    void honorsCommandLineDirectoriesAcrossModules();
    void sharesPortableDataAcrossVersionsAndRelocation();
    void rejectsInvalidPortableConfiguration();
    void separatesInstalledDataAndPreservesWikiEdits();
    void migratesLegacyPathsAndResolvesNewRelativePaths();
    void rejectsUnusableDataDirectory();
    void createsEveryRuntimeDirectory();
    void initializesMissingSettingsFile();
    void honorsLegacyRelativeAndAbsoluteDatabasePaths();
    void readsAndWritesTypedSettings();
};

void AppPathsTest::usesPortableLayoutByDefault()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    const QString applicationDirectory = root.filePath(QStringLiteral("Darkeye"));
    const darkeye::settings::Paths paths(applicationDirectory, QDir(applicationDirectory).filePath(QStringLiteral("data")));
    QCOMPARE(QDir::fromNativeSeparators(paths.dataDirectory()),
             QDir(applicationDirectory).filePath(QStringLiteral("data")));
    QCOMPARE(QDir::fromNativeSeparators(paths.publicDatabase()),
             QDir(applicationDirectory).filePath(QStringLiteral("data/public/public.db")));
    QCOMPARE(QDir::fromNativeSeparators(paths.privateDatabase()),
             QDir(applicationDirectory).filePath(QStringLiteral("data/private/private.db")));
    QCOMPARE(QDir::fromNativeSeparators(paths.publicBackupDirectory()),
             QDir(applicationDirectory).filePath(QStringLiteral("data/public/public_backup")));
    QCOMPARE(QDir::fromNativeSeparators(paths.workCoverDirectory()),
             QDir(applicationDirectory).filePath(QStringLiteral("data/public/workcovers")));
    QCOMPARE(QDir::fromNativeSeparators(paths.fanartDirectory()),
             QDir(applicationDirectory).filePath(QStringLiteral("data/public/fanart")));
    const darkeye::CrawlerSettings crawler = darkeye::settings::crawler(paths.settingsFile());
    QCOMPARE(crawler.coverFetchApiUrl, QUrl(QStringLiteral("http://127.0.0.1:56790/api/v1/image")));
    QCOMPARE(crawler.workApiBaseUrl, QUrl(QStringLiteral("http://127.0.0.1:56790/api/v1/work")));
    QCOMPARE(crawler.actressApiBaseUrl,
             QUrl(QStringLiteral("http://127.0.0.1:56790/api/v1/actress")));
    QCOMPARE(crawler.topActressesApiUrl,
             QUrl(QStringLiteral("http://127.0.0.1:56790/api/v1/top-actresses")));
    QCOMPARE(QDir::fromNativeSeparators(paths.actressImageDirectory()),
             QDir(applicationDirectory).filePath(QStringLiteral("data/public/actressimages")));
    QCOMPARE(QDir::fromNativeSeparators(paths.actorImageDirectory()),
             QDir(applicationDirectory).filePath(QStringLiteral("data/public/actorimages")));
    QCOMPARE(QDir::fromNativeSeparators(paths.privateBackupDirectory()),
             QDir(applicationDirectory).filePath(QStringLiteral("data/private/private_backup")));
    QCOMPARE(QDir::fromNativeSeparators(paths.shortcutsFile()),
             QDir(applicationDirectory).filePath(QStringLiteral("data/shortcuts.json")));
    QCOMPARE(QDir::fromNativeSeparators(paths.crawlerNavButtonsFile()),
             QDir(applicationDirectory).filePath(QStringLiteral("data/crawler_nav_buttons.json")));
    QCOMPARE(QDir::fromNativeSeparators(paths.actressNavButtonsFile()),
             QDir(applicationDirectory).filePath(QStringLiteral("data/actress_nav_buttons.json")));
    QCOMPARE(QDir::fromNativeSeparators(paths.addWorkWorkspaceLayoutFile()),
             QDir(applicationDirectory).filePath(QStringLiteral("data/add_work_workspace_layout.json")));
}

void AppPathsTest::resolvesBundleAndUnixResources()
{
    const QByteArray previous = qgetenv("DARKEYE_DATA_DIR");
    const bool wasSet = qEnvironmentVariableIsSet("DARKEYE_DATA_DIR");
    const auto restore = qScopeGuard([&] {
        if (wasSet) qputenv("DARKEYE_DATA_DIR", previous);
        else qunsetenv("DARKEYE_DATA_DIR");
    });
    qunsetenv("DARKEYE_DATA_DIR");
    QTemporaryDir root;
    QVERIFY(root.isValid());
    const QString mac = root.filePath(QStringLiteral("Portable/Darkeye.app/Contents/MacOS"));
    const QString resources = root.filePath(QStringLiteral("Portable/Darkeye.app/Contents/Resources"));
    QVERIFY(QDir().mkpath(mac));
    QVERIFY(QDir().mkpath(resources + QStringLiteral("/avwiki")));
    QSettings marker(root.filePath(QStringLiteral("Portable/portable.ini")), QSettings::IniFormat);
    marker.setValue(QStringLiteral("Portable/DataDirectory"), QStringLiteral("data"));
    marker.sync();
    const darkeye::settings::Paths macPaths(mac);
    QVERIFY(macPaths.isPortable());
    QCOMPARE(macPaths.dataDirectory(), root.filePath(QStringLiteral("Portable/data")));
    QCOMPARE(macPaths.configDirectory(), resources + QStringLiteral("/config"));
    QCOMPARE(macPaths.bundledWikiDirectory(), resources + QStringLiteral("/avwiki"));
    // Pass a profile to avoid the test runner's environment data override.
    const QString profile = root.filePath(QStringLiteral("profile"));
    const darkeye::settings::Paths writableMac(mac, profile);
    QVERIFY(writableMac.ensureRuntimeDirectories());
    QCOMPARE(writableMac.wikiDirectory(), profile + QStringLiteral("/avwiki"));

}

void AppPathsTest::detectsPortableConfigurationAndSystemDataLocation()
{
    const QByteArray previous = qgetenv("DARKEYE_DATA_DIR");
    const bool wasSet = qEnvironmentVariableIsSet("DARKEYE_DATA_DIR");
    const auto restore = qScopeGuard([&] {
        if (wasSet) qputenv("DARKEYE_DATA_DIR", previous);
        else qunsetenv("DARKEYE_DATA_DIR");
    });
    qunsetenv("DARKEYE_DATA_DIR");
    QTemporaryDir app;
    QVERIFY(app.isValid());
    const darkeye::settings::Paths paths(app.path());
    QVERIFY(!paths.isPortable());
    QCOMPARE(paths.dataDirectory(), QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation));
    {
        QSettings config(app.filePath(QStringLiteral("portable.ini")), QSettings::IniFormat);
        config.setValue(QStringLiteral("Portable/DataDirectory"), QStringLiteral("data"));
    }
    QVERIFY(paths.isPortable());
    QCOMPARE(paths.dataDirectory(), app.filePath(QStringLiteral("data")));
    qputenv("DARKEYE_DATA_DIR", app.filePath(QStringLiteral("override")).toUtf8());
    QCOMPARE(paths.dataDirectory(), app.filePath(QStringLiteral("override")));
}

void AppPathsTest::sharesPortableDataAcrossVersionsAndRelocation()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    const QByteArray previous = qgetenv("DARKEYE_DATA_DIR");
    const bool wasSet = qEnvironmentVariableIsSet("DARKEYE_DATA_DIR");
    const QString previousCwd = QDir::currentPath();
    const auto restore = qScopeGuard([&] {
        QDir::setCurrent(previousCwd);
        if (wasSet) qputenv("DARKEYE_DATA_DIR", previous);
        else qunsetenv("DARKEYE_DATA_DIR");
    });
    qunsetenv("DARKEYE_DATA_DIR");
    const QString bundle = root.filePath(QStringLiteral("DarkEye"));
    const QString oldApp = QDir(bundle).filePath(QStringLiteral("app-1.5.0"));
    const QString newApp = QDir(bundle).filePath(QStringLiteral("app-1.6.0"));
    QVERIFY(QDir().mkpath(oldApp));
    QVERIFY(QDir().mkpath(newApp));
    for (const auto &app : {oldApp, newApp}) {
        QSettings config(QDir(app).filePath(QStringLiteral("portable.ini")), QSettings::IniFormat);
        config.setValue(QStringLiteral("Portable/DataDirectory"), QStringLiteral("../data"));
    }
    // Resolution must not depend on the process working directory or version name.
    QVERIFY(QDir::setCurrent(root.path()));
    const darkeye::settings::Paths oldPaths(oldApp), newPaths(newApp);
    QVERIFY(oldPaths.isPortable());
    QVERIFY(newPaths.isPortable());
    QCOMPARE(oldPaths.dataDirectory(), QDir(bundle).filePath(QStringLiteral("data")));
    QCOMPARE(oldPaths.dataDirectory(), newPaths.dataDirectory());
    QVERIFY(oldPaths.ensureRuntimeDirectories());
    QVERIFY(darkeye::settings::ensureDefaults(oldPaths.settingsFile()));
    QFile sentinel(QDir(oldPaths.dataDirectory()).filePath(QStringLiteral("sentinel.txt")));
    QVERIFY(sentinel.open(QIODevice::WriteOnly));
    sentinel.write("portable user data");
    sentinel.close();
    QVERIFY(newPaths.ensureRuntimeDirectories());
    QVERIFY(!QFileInfo::exists(QDir(oldApp).filePath(QStringLiteral("data"))));
    QVERIFY(!QFileInfo::exists(QDir(newApp).filePath(QStringLiteral("data"))));
    const QString moved = root.filePath(QStringLiteral("Moved DarkEye"));
    QVERIFY(QDir().rename(bundle, moved));
    const darkeye::settings::Paths movedPaths(QDir(moved).filePath(QStringLiteral("app-1.6.0")));
    QCOMPARE(movedPaths.dataDirectory(), QDir(moved).filePath(QStringLiteral("data")));
    QFile movedSentinel(QDir(movedPaths.dataDirectory()).filePath(QStringLiteral("sentinel.txt")));
    QVERIFY(movedSentinel.open(QIODevice::ReadOnly));
    QCOMPARE(movedSentinel.readAll(), QByteArray("portable user data"));
    // A local explicit configuration can select a completely different data directory.
    const QString localConfig = QDir(movedPaths.applicationDirectory()).filePath(QStringLiteral("portable.ini"));
    {
        QSettings config(localConfig, QSettings::IniFormat);
        config.setValue(QStringLiteral("Portable/DataDirectory"), QStringLiteral("../profiles/user A"));
    }
    QCOMPARE(movedPaths.dataDirectory(), QDir(moved).filePath(QStringLiteral("profiles/user A")));
    {
        QSettings config(localConfig, QSettings::IniFormat);
        config.setValue(QStringLiteral("Portable/DataDirectory"), root.filePath(QStringLiteral("external data")));
    }
    QCOMPARE(movedPaths.dataDirectory(), root.filePath(QStringLiteral("external data")));
}

void AppPathsTest::rejectsInvalidPortableConfiguration()
{
    const QByteArray previous = qgetenv("DARKEYE_DATA_DIR");
    const bool wasSet = qEnvironmentVariableIsSet("DARKEYE_DATA_DIR");
    const auto restore = qScopeGuard([&] {
        if (wasSet) qputenv("DARKEYE_DATA_DIR", previous);
        else qunsetenv("DARKEYE_DATA_DIR");
    });
    qunsetenv("DARKEYE_DATA_DIR");
    QTemporaryDir root;
    QVERIFY(root.isValid());
    QFile config(root.filePath(QStringLiteral("portable.ini")));
    QVERIFY(config.open(QIODevice::WriteOnly));
    config.write("[Portable]\nDataDirectory=\n");
    config.close();
    const darkeye::settings::Paths paths(root.path());
    QVERIFY(paths.isPortable());
    QVERIFY(paths.dataDirectory().isEmpty());
    QString error;
    QVERIFY(!paths.ensureRuntimeDirectories(&error));
    QVERIFY(error.contains(QStringLiteral("portable.ini")));
    QVERIFY(!QFileInfo::exists(root.filePath(QStringLiteral("data"))));
    // An invalid local configuration must not silently fall back to a valid parent.
    const QString app = root.filePath(QStringLiteral("app-1.6.0"));
    QVERIFY(QDir().mkpath(app));
    {
        QSettings parent(config.fileName(), QSettings::IniFormat);
        parent.setValue(QStringLiteral("Portable/DataDirectory"), QStringLiteral("data"));
    }
    QFile broken(QDir(app).filePath(QStringLiteral("portable.ini")));
    QVERIFY(broken.open(QIODevice::WriteOnly));
    broken.write("[Portable]\nDataDirectory=\n");
    broken.close();
    const darkeye::settings::Paths child(app);
    QVERIFY(child.dataDirectory().isEmpty());
    QVERIFY(!child.ensureRuntimeDirectories());
    // No implicit recursive search for a portable root.
    QVERIFY(QDir().mkpath(QDir(app).filePath(QStringLiteral("nested"))));
    QVERIFY(broken.remove());
    QVERIFY(!child.isPortable());
    QCOMPARE(child.dataDirectory(), QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation));
    QVERIFY(!darkeye::settings::Paths(QDir(app).filePath(QStringLiteral("nested"))).isPortable());
    // The marker alone selects the default ../data layout.
    QVERIFY(broken.open(QIODevice::WriteOnly));
    broken.close();
    QVERIFY(child.isPortable());
    QCOMPARE(child.dataDirectory(), root.filePath(QStringLiteral("data")));
}

void AppPathsTest::separatesInstalledDataAndPreservesWikiEdits()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    const QString app = root.filePath(QStringLiteral("app"));
    const QString data = root.filePath(QStringLiteral("profile"));
    QVERIFY(QDir().mkpath(QDir(app).filePath(QStringLiteral("avwiki"))));
    QFile seed(QDir(app).filePath(QStringLiteral("avwiki/page.md")));
    QVERIFY(seed.open(QIODevice::WriteOnly));
    QCOMPARE(seed.write("# Seed"), qint64(6));
    seed.close();
    const darkeye::settings::Paths paths(app, data);
    QString error;
    QVERIFY2(paths.ensureRuntimeDirectories(&error), qPrintable(error));
    QVERIFY(darkeye::settings::ensureDefaults(paths.settingsFile(), &error));
    QVERIFY(!QFileInfo::exists(QDir(app).filePath(QStringLiteral("data"))));
    QCOMPARE(paths.publicDatabase(), QDir(data).filePath(QStringLiteral("public/public.db")));
    QCOMPARE(paths.configDirectory(), QDir(app).filePath(QStringLiteral("config")));
    QCOMPARE(paths.wikiDirectory(), QDir(data).filePath(QStringLiteral("avwiki")));
    QCOMPARE(paths.bundledWikiDirectory(), QDir(app).filePath(QStringLiteral("avwiki")));
    QVERIFY(QFileInfo(QDir(data).filePath(QStringLiteral("avwiki"))).isDir());
    QFile wiki(QDir(paths.wikiDirectory()).filePath(QStringLiteral("page.md")));
    QVERIFY(wiki.open(QIODevice::ReadOnly));
    QCOMPARE(wiki.readAll(), QByteArray("# Seed"));
    wiki.close();
    QVERIFY(wiki.open(QIODevice::WriteOnly | QIODevice::Truncate));
    wiki.write("User edit");
    wiki.close();
    QVERIFY(paths.ensureRuntimeDirectories());
    QVERIFY(wiki.open(QIODevice::ReadOnly));
    QCOMPARE(wiki.readAll(), QByteArray("User edit"));
    wiki.close();
    QVERIFY(wiki.remove());
    QVERIFY(paths.ensureRuntimeDirectories());
    QVERIFY(!wiki.exists());
    QVERIFY(seed.exists());
}

void AppPathsTest::honorsCommandLineDirectoriesAcrossModules()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    const QString previousCwd = QDir::currentPath();
    const auto restore = qScopeGuard([&] {
        QDir::setCurrent(previousCwd);
        darkeye::settings::Paths::configureFromArguments({QStringLiteral("Darkeye")});
    });
    QVERIFY(QDir::setCurrent(root.path()));
    const QString config = QDir::current().filePath(QStringLiteral("dev config"));
    const QString wiki = root.filePath(QStringLiteral("dev wiki"));
    const QString data = root.filePath(QStringLiteral("dev data"));
    QString error;
    QVERIFY2(darkeye::settings::Paths::configureFromArguments(
        {QStringLiteral("Darkeye"), QStringLiteral("--config-dir"), QStringLiteral("dev config"),
         QStringLiteral("--avwiki-dir=") + wiki, QStringLiteral("--data-dir"), data}, &error),
        qPrintable(error));
    QVERIFY(QDir::setCurrent(previousCwd));
    const darkeye::settings::Paths paths;
    QCOMPARE(paths.configDirectory(), config);
    QCOMPARE(paths.wikiDirectory(), wiki);
    QCOMPARE(paths.dataDirectory(), data); // Wins over the test runner's environment override.
    QCOMPARE(darkeye::settings::Paths().settingsFile(), QDir(data).filePath(QStringLiteral("settings.ini")));
    QVERIFY2(paths.ensureRuntimeDirectories(&error), qPrintable(error));
    QVERIFY(QFileInfo(QDir(data).filePath(QStringLiteral("public/workcovers"))).isDir());
    QVERIFY(!QFileInfo::exists(QDir(data).filePath(QStringLiteral("avwiki"))));
    QVERIFY(!darkeye::settings::Paths::configureFromArguments(
        {QStringLiteral("Darkeye"), QStringLiteral("--data-dir")}, &error));
    QVERIFY(!error.isEmpty());
    QVERIFY(!darkeye::settings::Paths::configureFromArguments(
        {QStringLiteral("Darkeye"), QStringLiteral("--config-dir=")}, &error));
    QCOMPARE(darkeye::settings::Paths().dataDirectory(), data); // Invalid arguments never partially apply.
    QVERIFY(darkeye::settings::Paths::configureFromArguments({QStringLiteral("Darkeye")}, &error));
    QCOMPARE(darkeye::settings::Paths().wikiDirectory(),
             QDir(darkeye::settings::Paths().dataDirectory()).filePath(QStringLiteral("avwiki")));
}

void AppPathsTest::migratesLegacyPathsAndResolvesNewRelativePaths()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    const QString app = root.filePath(QStringLiteral("app"));
    const QString data = root.filePath(QStringLiteral("profile"));
    QVERIFY(QDir().mkpath(data));
    const darkeye::settings::Paths paths(app, data);
    {
        QSettings settings(paths.settingsFile(), QSettings::IniFormat);
        settings.setValue(QStringLiteral("Paths/Database"), QStringLiteral("data/public/custom.db"));
        settings.setValue(QStringLiteral("Paths/PrivateDatabase"), root.filePath(QStringLiteral("external.db")));
        settings.setValue(QStringLiteral("Paths/WorkCovers"), QStringLiteral("legacy/covers"));
    }
    QString error;
    QVERIFY2(paths.ensureRuntimeDirectories(&error), qPrintable(error));
    QCOMPARE(paths.publicDatabase(), QDir(data).filePath(QStringLiteral("public/custom.db")));
    QCOMPARE(paths.privateDatabase(), root.filePath(QStringLiteral("external.db")));
    QCOMPARE(paths.workCoverDirectory(), QDir(app).filePath(QStringLiteral("legacy/covers")));
    {
        QSettings settings(paths.settingsFile(), QSettings::IniFormat);
        QCOMPARE(settings.value(QStringLiteral("Paths/Version")).toInt(), 2);
        QCOMPARE(settings.value(QStringLiteral("Paths/Database")).toString(), QStringLiteral("public/custom.db"));
        settings.setValue(QStringLiteral("Paths/WorkCovers"), QStringLiteral("images/covers"));
    }
    QVERIFY(paths.ensureRuntimeDirectories());
    QCOMPARE(paths.workCoverDirectory(), QDir(data).filePath(QStringLiteral("images/covers")));
    QVERIFY(!QFileInfo::exists(QDir(app).filePath(QStringLiteral("data"))));
}

void AppPathsTest::rejectsUnusableDataDirectory()
{
    QTemporaryDir root;
    QFile blocked(root.filePath(QStringLiteral("blocked")));
    QVERIFY(blocked.open(QIODevice::WriteOnly));
    blocked.close();
    const darkeye::settings::Paths paths(root.path(), blocked.fileName());
    QString error;
    QVERIFY(!paths.ensureRuntimeDirectories(&error));
    QVERIFY(!error.isEmpty());
}

void AppPathsTest::createsEveryRuntimeDirectory()
{
    QTemporaryDir temporaryDirectory;
    QVERIFY(temporaryDirectory.isValid());

    const QString applicationDirectory = temporaryDirectory.path();
    const darkeye::settings::Paths paths(applicationDirectory, QDir(applicationDirectory).filePath(QStringLiteral("data")));
    QString errorMessage;
    QVERIFY2(paths.ensureRuntimeDirectories(&errorMessage),
             qPrintable(errorMessage));

    const QStringList expectedDirectories = {
        QStringLiteral("public/public_backup"),
        QStringLiteral("public/workcovers"),
        QStringLiteral("public/fanart"),
        QStringLiteral("public/actressimages"),
        QStringLiteral("public/actorimages"),
        QStringLiteral("private/private_backup"),
        QStringLiteral("cache/images"),
        QStringLiteral("cache/graph"),
        QStringLiteral("cache/translations"),
        QStringLiteral("temp"),
        QStringLiteral("logs"),
    };

    for (const QString &relativeDirectory : expectedDirectories)
    {
        QVERIFY2(QFileInfo::exists(QDir(paths.dataDirectory()).filePath(relativeDirectory)),
                 qPrintable(relativeDirectory));
    }

    const auto readNavigationExample = [&paths](const QString &fileName) {
        QFile file(QDir(paths.dataDirectory()).filePath(fileName));
        if (!file.open(QIODevice::ReadOnly)) return QJsonArray{};
        return QJsonDocument::fromJson(file.readAll()).array();
    };
    const QJsonArray workNavigation = readNavigationExample(QStringLiteral("crawler_nav_buttons.json"));
    QCOMPARE(workNavigation.size(), 1);
    QCOMPARE(workNavigation.first().toObject().value(QStringLiteral("url")).toString(),
             QStringLiteral("https://www.google.com/search?q={serial}"));
    const QJsonArray actressNavigation = readNavigationExample(QStringLiteral("actress_nav_buttons.json"));
    QCOMPARE(actressNavigation.size(), 1);
    QCOMPARE(actressNavigation.first().toObject().value(QStringLiteral("url")).toString(),
             QStringLiteral("https://www.google.com/search?q={jp_name}"));

    // 用户自定义的导航配置不可被后续启动覆盖。
    QFile customFile(paths.crawlerNavButtonsFile());
    QVERIFY(customFile.open(QIODevice::WriteOnly | QIODevice::Truncate));
    QVERIFY(customFile.write("[]") == 2);
    customFile.close();
    QVERIFY(paths.ensureRuntimeDirectories());
    QVERIFY(customFile.open(QIODevice::ReadOnly));
    QCOMPARE(customFile.readAll(), QByteArray("[]"));
}

void AppPathsTest::initializesMissingSettingsFile()
{
    QTemporaryDir temporaryDirectory;
    QVERIFY(temporaryDirectory.isValid());
    const QString settingsFile =
        QDir(temporaryDirectory.path()).filePath(QStringLiteral("data/settings.ini"));
    QVERIFY(!QFileInfo::exists(settingsFile));
    QVERIFY(darkeye::settings::ensureDefaults(settingsFile));
    QVERIFY(QFileInfo::exists(settingsFile));

    QSettings ini(settingsFile, QSettings::IniFormat);
    QCOMPARE(ini.value(QStringLiteral("window/size")).toSize(), QSize(800, 600));
    QCOMPARE(ini.value(QStringLiteral("App/Theme")).toString(), QStringLiteral("LIGHT"));
    QCOMPARE(ini.value(QStringLiteral("Paths/Database")).toString(),
             QStringLiteral("public/public.db"));
    QCOMPARE(ini.value(QStringLiteral("Crawler/WorkApiBaseUrl")).toString(),
             QStringLiteral("http://127.0.0.1:56790/api/v1/work"));
    QCOMPARE(ini.value(QStringLiteral("WebDAV/RemoteRoot")).toString(),
             QStringLiteral("/darkeye"));
    QCOMPARE(ini.value(QStringLiteral("Translation/Engine")).toString(),
             QStringLiteral("llm"));
    QCOMPARE(ini.value(QStringLiteral("LlamaCpp/Mode")).toString(), QStringLiteral("cpu"));

    ini.setValue(QStringLiteral("App/Theme"), QStringLiteral("DARK"));
    ini.sync();
    QVERIFY(darkeye::settings::ensureDefaults(settingsFile));
    QCOMPARE(darkeye::settings::app(settingsFile).themeId, QStringLiteral("DARK"));
}

void AppPathsTest::honorsLegacyRelativeAndAbsoluteDatabasePaths()
{
    QTemporaryDir temporaryDirectory;
    QVERIFY(temporaryDirectory.isValid());
    const QString absolutePrivate =
        QDir(temporaryDirectory.path()).filePath(QStringLiteral("external/private.db"));

    const QString applicationDirectory = temporaryDirectory.path();
    const darkeye::settings::Paths paths(applicationDirectory, QDir(applicationDirectory).filePath(QStringLiteral("data")));
    QVERIFY(paths.ensureRuntimeDirectories());
    QSettings settings(paths.settingsFile(), QSettings::IniFormat);
    settings.setValue(QStringLiteral("Paths/Database"), QStringLiteral("legacy/public/custom.db"));
    settings.setValue(QStringLiteral("Paths/PrivateDatabase"), absolutePrivate);
    settings.setValue(QStringLiteral("Paths/Actressimages"),
                      QStringLiteral("legacy/public/actressimages"));
    settings.setValue(QStringLiteral("Paths/Actorimages"),
                      QStringLiteral("legacy/public/actorimages"));
    settings.setValue(QStringLiteral("Crawler/CoverFetchApiUrl"),
                      QStringLiteral("http://127.0.0.1:61234/custom/image"));
    settings.setValue(QStringLiteral("Crawler/WorkApiBaseUrl"),
                      QStringLiteral("http://127.0.0.1:61234/custom/work/"));
    settings.setValue(QStringLiteral("Crawler/ActressApiBaseUrl"), QString());
    settings.setValue(QStringLiteral("Crawler/TopActressesApiUrl"),
                      QStringLiteral("http://127.0.0.1:61234/custom/top/"));
    settings.sync();

    QCOMPARE(
        QDir::fromNativeSeparators(paths.publicDatabase()),
        QDir::fromNativeSeparators(
            QDir(temporaryDirectory.path()).filePath(QStringLiteral("legacy/public/custom.db"))));
    QCOMPARE(QDir::fromNativeSeparators(paths.privateDatabase()),
             QDir::fromNativeSeparators(absolutePrivate));
    QCOMPARE(QDir::fromNativeSeparators(paths.actressImageDirectory()),
             QDir::fromNativeSeparators(QDir(temporaryDirectory.path()).filePath(
                 QStringLiteral("legacy/public/actressimages"))));
    QCOMPARE(QDir::fromNativeSeparators(paths.actorImageDirectory()),
             QDir::fromNativeSeparators(QDir(temporaryDirectory.path()).filePath(
                 QStringLiteral("legacy/public/actorimages"))));
    const darkeye::CrawlerSettings crawler = darkeye::settings::crawler(paths.settingsFile());
    QCOMPARE(crawler.coverFetchApiUrl, QUrl(QStringLiteral("http://127.0.0.1:61234/custom/image")));
    QCOMPARE(crawler.workApiBaseUrl, QUrl(QStringLiteral("http://127.0.0.1:61234/custom/work")));
    QCOMPARE(crawler.actressApiBaseUrl,
             QUrl(QStringLiteral("http://127.0.0.1:56790/api/v1/actress")));
    QCOMPARE(crawler.topActressesApiUrl, QUrl(QStringLiteral("http://127.0.0.1:61234/custom/top")));
}

void AppPathsTest::readsAndWritesTypedSettings()
{
    QTemporaryDir temporaryDirectory;
    QVERIFY(temporaryDirectory.isValid());

    const QString settingsFile =
        QDir(temporaryDirectory.path()).filePath(QStringLiteral("settings.ini"));

    darkeye::AppSettings app = darkeye::settings::app(settingsFile);
    QCOMPARE(app.themeId, QStringLiteral("LIGHT"));
    QVERIFY(app.firstLaunch);
    QCOMPARE(app.windowSize, QSize(800, 600));
    app.themeId = QStringLiteral("DARK");
    app.customPrimary = QStringLiteral("#123456");
    app.firstLaunch = false;
    app.windowSize = QSize(1440, 900);
    app.videoPaths = {QStringLiteral(" C:/Videos "), QStringLiteral("."), QString()};
    darkeye::settings::saveApp(app, settingsFile);

    const darkeye::AppSettings loadedApp = darkeye::settings::app(settingsFile);
    QCOMPARE(loadedApp.themeId, QStringLiteral("DARK"));
    QCOMPARE(loadedApp.customPrimary, QStringLiteral("#123456"));
    QVERIFY(!loadedApp.firstLaunch);
    QCOMPARE(loadedApp.windowSize, QSize(1440, 900));
    QCOMPARE(loadedApp.videoPaths, QStringList{QStringLiteral("C:/Videos")});

    darkeye::CrawlerSettings crawler = darkeye::settings::crawler(settingsFile);
    crawler.workApiBaseUrl = QUrl(QStringLiteral("http://127.0.0.1:61234/work"));
    crawler.autoStartCollector = true;
    crawler.webDav.remoteRoot = QStringLiteral("/darkeye/backups/");
    darkeye::settings::saveCrawler(crawler, settingsFile);
    const darkeye::CrawlerSettings loadedCrawler = darkeye::settings::crawler(settingsFile);
    QCOMPARE(loadedCrawler.workApiBaseUrl,
             QUrl(QStringLiteral("http://127.0.0.1:61234/work")));
    QVERIFY(loadedCrawler.autoStartCollector);
    QCOMPARE(loadedCrawler.webDav.remoteRoot, QStringLiteral("/darkeye/backups"));

    darkeye::TranslationSettings translation = darkeye::settings::translation(settingsFile);
    QCOMPARE(translation.engine, QStringLiteral("llm"));
    QCOMPARE(translation.timeoutSeconds, 12);
    QCOMPARE(translation.retries, 2);
    QCOMPARE(translation.llama.mode, QStringLiteral("cpu"));
    QCOMPARE(translation.llama.gpuLayers, 99);
    QVERIFY(translation.llama.mlock);
    translation.timeoutSeconds = 90;
    translation.llama.contextSize = 4096;
    translation.llama.port = 0;
    translation.llama.mode = QStringLiteral("unsupported");
    darkeye::settings::saveTranslation(translation, settingsFile);
    const darkeye::TranslationSettings loadedTranslation = darkeye::settings::translation(settingsFile);
    QCOMPARE(loadedTranslation.timeoutSeconds, 90);
    QCOMPARE(loadedTranslation.llama.contextSize, 4096);
    QCOMPARE(loadedTranslation.llama.port, 1);
    QCOMPARE(loadedTranslation.llama.mode, QStringLiteral("cpu"));
}

QTEST_MAIN(AppPathsTest)
#include "AppPathsTest.moc"
