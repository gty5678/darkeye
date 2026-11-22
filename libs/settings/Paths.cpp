#include "settings/Paths.h"

#include "settings/SettingsStore.h"

#include <QCoreApplication>
#include <QCommandLineParser>
#include <QDir>
#include <QDirIterator>
#include <QSettings>
#include <QStandardPaths>
#include <QSaveFile>
#include <QTemporaryFile>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

namespace
{

struct DirectoryOverrides {
    QString data;
    QString config;
    QString wiki;
};

DirectoryOverrides runtimeDirectories;

QString absoluteOverride(const QString &path)
{
    return path.isEmpty() ? QString{} : QDir(path).absolutePath();
}

bool createNavigationExampleIfMissing(const QString &filePath, const QJsonObject &example,
                                      QString *errorMessage)
{
    if (QFileInfo::exists(filePath)) return true;

    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::NewOnly))
    {
        if (errorMessage != nullptr)
            *errorMessage = QStringLiteral("无法创建导航配置文件：%1").arg(filePath);
        return false;
    }

    const QJsonDocument document(QJsonArray{example});
    if (file.write(document.toJson(QJsonDocument::Indented)) < 0)
    {
        if (errorMessage != nullptr)
            *errorMessage = QStringLiteral("无法写入导航配置文件：%1").arg(filePath);
        return false;
    }
    return true;
}

// Stage a complete initial tree before making it visible to the application.
bool initializeDirectory(const QString &source, const QString &target, QString *errorMessage)
{
    if (QFileInfo::exists(target)) return true;
    const QString staging = target + QStringLiteral(".initializing");
    if (!QDir().mkpath(staging)) {
        if (errorMessage) *errorMessage = QStringLiteral("无法创建初始化目录：%1").arg(staging);
        return false;
    }
    QDirIterator files(source, QDir::Files | QDir::NoSymLinks, QDirIterator::Subdirectories);
    while (files.hasNext()) {
        const QString from = files.next();
        const QString to = QDir(staging).filePath(QDir(source).relativeFilePath(from));
        QFile input(from);
        QSaveFile output(to);
        if (!QDir().mkpath(QFileInfo(to).absolutePath()) || !input.open(QIODevice::ReadOnly)
            || !output.open(QIODevice::WriteOnly)) {
            if (errorMessage) *errorMessage = QStringLiteral("无法初始化数据文件：%1").arg(to);
            return false;
        }
        const QByteArray bytes = input.readAll();
        if (input.error() != QFile::NoError || output.write(bytes) != bytes.size() || !output.commit()) {
            if (errorMessage) *errorMessage = QStringLiteral("无法复制数据文件：%1").arg(to);
            return false;
        }
    }
    if (!QDir().rename(staging, target)) {
        if (errorMessage) *errorMessage = QStringLiteral("无法完成数据初始化：%1").arg(target);
        return false;
    }
    return true;
}

} // namespace

//主要是给软件各种需要路径的地方提供路径

namespace darkeye::settings
{

Paths::Paths() : Paths(QCoreApplication::applicationDirPath(), runtimeDirectories.data,
                       runtimeDirectories.config, runtimeDirectories.wiki)
{
}

bool Paths::configureFromArguments(const QStringList &arguments, QString *errorMessage)
{
    QCommandLineParser parser;
    parser.addOptions({
        {QStringLiteral("config-dir"), QStringLiteral("配置资源目录"), QStringLiteral("directory")},
        {QStringLiteral("avwiki-dir"), QStringLiteral("知识库目录"), QStringLiteral("directory")},
        {QStringLiteral("data-dir"), QStringLiteral("运行数据目录"), QStringLiteral("directory")},
    });
    if (!parser.parse(arguments)) {
        if (errorMessage) *errorMessage = parser.errorText();
        return false;
    }
    DirectoryOverrides directories;
    const auto read = [&](const QString &option, QString &target) {
        if (!parser.isSet(option)) return true;
        const QString value = parser.value(option);
        if (value.trimmed().isEmpty()) {
            if (errorMessage) *errorMessage = QStringLiteral("--%1 不能使用空目录。").arg(option);
            return false;
        }
        target = absoluteOverride(value);
        return true;
    };
    if (!read(QStringLiteral("config-dir"), directories.config)
        || !read(QStringLiteral("avwiki-dir"), directories.wiki)
        || !read(QStringLiteral("data-dir"), directories.data)) return false;
    runtimeDirectories = std::move(directories);
    return true;
}

Paths::Paths(QString applicationDirectory, QString dataDirectoryOverride,
             QString configDirectoryOverride, QString wikiDirectoryOverride)
    : m_applicationDirectory(applicationDirectory.isEmpty()
                                 ? QCoreApplication::applicationDirPath()
                                 : QDir(applicationDirectory).absolutePath()),
      m_dataDirectoryOverride(absoluteOverride(dataDirectoryOverride)),
      m_configDirectoryOverride(absoluteOverride(configDirectoryOverride)),
      m_wikiDirectoryOverride(absoluteOverride(wikiDirectoryOverride))
{
}

const QString &Paths::applicationDirectory() const noexcept
{
    return m_applicationDirectory;
}

bool Paths::isPortable() const
{
    return !portableConfigurationFile().isEmpty();
}

QString Paths::portableConfigurationFile() const
{
    const QDir app(m_applicationDirectory);
    const QString local = app.filePath(QStringLiteral("portable.ini"));
    if (QFileInfo::exists(local)) return local;
    // Only inspect the enclosing bundle's parent; never search arbitrary ancestors.
    if (m_applicationDirectory.endsWith(QStringLiteral(".app/Contents/MacOS"))) {
        const QString external = app.absoluteFilePath(QStringLiteral("../../../portable.ini"));
        if (QFileInfo::exists(external)) return QDir::cleanPath(external);
    }
    return {};
}

QString Paths::resourceRootDirectory() const
{
    const QDir app(m_applicationDirectory);
    if (m_applicationDirectory.endsWith(QStringLiteral(".app/Contents/MacOS")))
        return QDir::cleanPath(app.absoluteFilePath(QStringLiteral("../Resources")));
    return m_applicationDirectory;
}

QString Paths::configuredPath(const QString &key, const QString &defaultRelativePath) const
{
    const SettingsStore store(settingsFile());
    const QString configured = store.value(key).toString().trimmed();
    if (configured.isEmpty()) return QDir(dataDirectory()).absoluteFilePath(defaultRelativePath);
    if (QFileInfo(configured).isAbsolute()) return QDir::cleanPath(configured);
    // 旧配置尚未完成迁移时仍保持旧语义，避免读到错误的数据库。
    const QString root = store.value(QStringLiteral("Paths/Version"), 1).toInt() < 2
                             ? m_applicationDirectory : dataDirectory();
    return QDir::cleanPath(QDir(root).absoluteFilePath(configured));
}

bool Paths::migratePathSettings(QString *errorMessage) const
{
    if (!QFileInfo::exists(settingsFile())) return true;
    QSettings settings(settingsFile(), QSettings::IniFormat);
    if (settings.value(QStringLiteral("Paths/Version"), 1).toInt() >= 2) return true;
    const QStringList keys = {QStringLiteral("Database"), QStringLiteral("PrivateDatabase"),
        QStringLiteral("DatabaseBackups"), QStringLiteral("PrivateDatabaseBackups"),
        QStringLiteral("Actressimages"), QStringLiteral("Actorimages"), QStringLiteral("WorkCovers"),
        QStringLiteral("Fanart"), QStringLiteral("Temp")};
    for (const auto &name : keys) {
        const QString key = QStringLiteral("Paths/") + name;
        QString path = settings.value(key).toString().trimmed();
        if (path.isEmpty() || QFileInfo(path).isAbsolute()) continue;
        path = QDir::cleanPath(path.replace(u'\\', u'/'));
        // 默认 data/ 前缀随整个数据目录一起迁移；其他自定义相对位置保持实际位置。
        if (path.startsWith(QStringLiteral("data/"))) path.remove(0, 5);
        else path = QDir(m_applicationDirectory).absoluteFilePath(path);
        settings.setValue(key, QDir::cleanPath(path));
    }
    settings.setValue(QStringLiteral("Paths/Version"), 2);
    settings.sync();
    if (settings.status() == QSettings::NoError) return true;
    if (errorMessage) *errorMessage = QStringLiteral("无法迁移路径配置：%1").arg(settingsFile());
    return false;
}

QString Paths::resourcesDirectory() const { return QDir(resourceRootDirectory()).filePath(QStringLiteral("resources")); }
QString Paths::configDirectory() const
{
    return m_configDirectoryOverride.isEmpty()
        ? QDir(resourceRootDirectory()).filePath(QStringLiteral("config")) : m_configDirectoryOverride;
}
QString Paths::dataDirectory() const
{
    if (!m_dataDirectoryOverride.isEmpty()) return QDir(m_dataDirectoryOverride).absolutePath();
    const QString override = qEnvironmentVariable("DARKEYE_DATA_DIR").trimmed();
    if (!override.isEmpty()) return QDir::cleanPath(QDir(m_applicationDirectory).absoluteFilePath(override));
    const QString portableConfig = portableConfigurationFile();
    if (!portableConfig.isEmpty()) {
        QSettings settings(portableConfig, QSettings::IniFormat);
        const QString data = settings.value(QStringLiteral("Portable/DataDirectory"),
                                            QStringLiteral("../data")).toString().trimmed();
        // 配置缺失或损坏时禁止回退，避免悄悄打开另一套数据库。
        if (settings.status() != QSettings::NoError || data.isEmpty()) return {};
        return QDir::cleanPath(QDir(QFileInfo(portableConfig).absolutePath()).absoluteFilePath(data));
    }
    // 延迟到使用时查询，确保 Application 已设置组织名和应用名。
    return QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
}
QString Paths::cacheDirectory() const { return QDir(dataDirectory()).filePath(QStringLiteral("cache")); }
QString Paths::temporaryDirectory() const { return QDir(dataDirectory()).filePath(QStringLiteral("temp")); }
QString Paths::wikiDirectory() const
{
    return m_wikiDirectoryOverride.isEmpty()
        ? QDir(dataDirectory()).filePath(QStringLiteral("avwiki")) : m_wikiDirectoryOverride;
}
QString Paths::bundledWikiDirectory() const
{
    return QDir(resourceRootDirectory()).filePath(QStringLiteral("avwiki"));
}
QString Paths::settingsFile() const { return QDir(dataDirectory()).filePath(QStringLiteral("settings.ini")); }
QString Paths::publicDatabase() const { return configuredPath(QStringLiteral("Paths/Database"), QStringLiteral("public/public.db")); }
QString Paths::privateDatabase() const { return configuredPath(QStringLiteral("Paths/PrivateDatabase"), QStringLiteral("private/private.db")); }
QString Paths::publicBackupDirectory() const { return configuredPath(QStringLiteral("Paths/DatabaseBackups"), QStringLiteral("public/public_backup")); }
QString Paths::workCoverDirectory() const { return configuredPath(QStringLiteral("Paths/WorkCovers"), QStringLiteral("public/workcovers")); }
QString Paths::fanartDirectory() const { return configuredPath(QStringLiteral("Paths/Fanart"), QStringLiteral("public/fanart")); }
QString Paths::actressImageDirectory() const { return configuredPath(QStringLiteral("Paths/Actressimages"), QStringLiteral("public/actressimages")); }
QString Paths::actorImageDirectory() const { return configuredPath(QStringLiteral("Paths/Actorimages"), QStringLiteral("public/actorimages")); }
QString Paths::privateBackupDirectory() const { return configuredPath(QStringLiteral("Paths/PrivateDatabaseBackups"), QStringLiteral("private/private_backup")); }
QString Paths::shortcutsFile() const { return QDir(dataDirectory()).filePath(QStringLiteral("shortcuts.json")); }
QString Paths::crawlerNavButtonsFile() const { return QDir(dataDirectory()).filePath(QStringLiteral("crawler_nav_buttons.json")); }
QString Paths::actressNavButtonsFile() const { return QDir(dataDirectory()).filePath(QStringLiteral("actress_nav_buttons.json")); }
QString Paths::addWorkWorkspaceLayoutFile() const { return QDir(dataDirectory()).filePath(QStringLiteral("add_work_workspace_layout.json")); }
QString Paths::logFile() const { return QDir(dataDirectory()).filePath(QStringLiteral("logs/darkeye.log")); }

bool Paths::ensureRuntimeDirectories(QString *errorMessage) const
{
    if (dataDirectory().isEmpty()) {
        if (errorMessage) *errorMessage = QStringLiteral("无法确定数据目录，请检查 portable.ini 中的 Portable/DataDirectory 配置。");
        return false;
    }
    const QString seed = QDir(resourceRootDirectory()).filePath(QStringLiteral("seed-data"));
    if (QFileInfo(seed).isDir() && !initializeDirectory(seed, dataDirectory(), errorMessage)) return false;
    // 仅创建应用托管的运行目录，不创建用户配置的外部数据路径。
    const QStringList directories = {QStringLiteral("public/public_backup"), QStringLiteral("public/workcovers"), QStringLiteral("public/fanart"), QStringLiteral("public/actressimages"), QStringLiteral("public/actorimages"), QStringLiteral("private/private_backup"), QStringLiteral("cache/images"), QStringLiteral("cache/graph"), QStringLiteral("cache/translations"), QStringLiteral("temp"), QStringLiteral("logs")};
    QDir dataRoot(dataDirectory());
    for (const QString &directory : directories)
    {
        if (dataRoot.mkpath(directory)) continue;
        // 返回首个失败目录，便于调用方将原因展示给用户或写入日志。
        if (errorMessage != nullptr)
            *errorMessage = QStringLiteral("无法创建运行数据目录：%1").arg(dataRoot.filePath(directory));
        return false;
    }

    // 实际写入探测；便携目录不可写时直接报错，不切换数据位置。
    QTemporaryFile probe(dataRoot.filePath(QStringLiteral(".write-test-XXXXXX")));
    if (!probe.open()) {
        if (errorMessage) *errorMessage = QStringLiteral("数据目录不可写：%1").arg(dataDirectory());
        return false;
    }
    if (!migratePathSettings(errorMessage)) return false;

    // Keep mutable content outside the installation (.app signatures and /opt
    // permissions must survive editing and updates). Never overwrite user edits.
    if (m_wikiDirectoryOverride.isEmpty() && !QFileInfo::exists(wikiDirectory())) {
        if (QFileInfo(bundledWikiDirectory()).isDir()) {
            if (!initializeDirectory(bundledWikiDirectory(), wikiDirectory(), errorMessage)) return false;
        } else if (!QDir().mkpath(wikiDirectory())) {
            if (errorMessage) *errorMessage = QStringLiteral("无法创建知识库目录：%1").arg(wikiDirectory());
            return false;
        }
    }

    // 首次运行时提供可直接使用、也便于用户照着扩展的外部导航样例。已有文件始终保留。
    if (!createNavigationExampleIfMissing(
            crawlerNavButtonsFile(),
            {{QStringLiteral("name"), QStringLiteral("Google 搜索")},
             {QStringLiteral("url"), QStringLiteral("https://www.google.com/search?q={serial}")},
             {QStringLiteral("description"), QStringLiteral("按作品番号搜索")}}, errorMessage))
        return false;
    if (!createNavigationExampleIfMissing(
            actressNavButtonsFile(),
            {{QStringLiteral("name"), QStringLiteral("Google 搜索")},
             {QStringLiteral("url"), QStringLiteral("https://www.google.com/search?q={jp_name}")},
             {QStringLiteral("quote"), QJsonArray{QStringLiteral("jp_name")}},
             {QStringLiteral("description"), QStringLiteral("按女优日文名搜索")}}, errorMessage))
        return false;
    return true;
}

} // namespace darkeye::settings
