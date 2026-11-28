#include "settings/Settings.h"

#include "settings/Paths.h"

#include <QDir>
#include <QFileInfo>
#include <QSet>
#include <QSettings>
#include <QVariantMap>

namespace darkeye::settings
{

namespace
{

QString normalizedUrl(const QString &value, const QUrl &fallback)
{
    QString normalized = value.trimmed();
    while (normalized.endsWith(QChar('/')))
        normalized.chop(1);
    return normalized.isEmpty() ? fallback.toString() : normalized;
}

QString normalizedFallback(const QString &value)
{
    const QString normalized = value.trimmed().toLower();
    return normalized == QStringLiteral("source") ? normalized : QStringLiteral("empty");
}

QString normalizedMode(const QString &value)
{
    const QString normalized = value.trimmed().toLower();
    return normalized == QStringLiteral("gpu") ? normalized : QStringLiteral("cpu");
}

QString normalizedWebDavRoot(const QString &value)
{
    QString normalized = value;
    normalized.replace(u'\\', u'/');
    normalized = normalized.trimmed();
    if (normalized.isEmpty())
        normalized = QStringLiteral("/darkeye");
    while (normalized.startsWith(u'/'))
        normalized.remove(0, 1);
    while (normalized.endsWith(u'/'))
        normalized.chop(1);
    return QStringLiteral("/") + normalized;
}

QStringList readVideoPaths(const QString &value)
{
    QStringList paths;
    for (const QString &path : value.split(u',', Qt::SkipEmptyParts))
    {
        const QString trimmed = path.trimmed();
        if (!trimmed.isEmpty())
            paths.append(trimmed);
    }
    return paths;
}

QStringList readUnfinishedSerials(const QString &value)
{
    QStringList serials;
    QSet<QString> seen;
    for (const QString &part : value.split(u',', Qt::SkipEmptyParts))
    {
        const QString serial = part.trimmed();
        if (!serial.isEmpty() && !seen.contains(serial))
        {
            serials.append(serial);
            seen.insert(serial);
        }
    }
    return serials;
}

QStringList normalizedVideoPaths(const QStringList &paths)
{
    QStringList normalized;
    for (const QString &path : paths)
    {
        const QString trimmed = path.trimmed();
        if (!trimmed.isEmpty() && trimmed != QStringLiteral("."))
            normalized.append(trimmed);
    }
    return normalized;
}

void saveValues(const QString &settingsFile, const QVariantMap &values)
{
    QSettings settings(settingsFile, QSettings::IniFormat);
    for (auto it = values.cbegin(); it != values.cend(); ++it)
        settings.setValue(it.key(), it.value());
    settings.sync();
}

bool migrateAppSettings(const QString &settingsFile, QString *errorMessage)
{
    QSettings settings(settingsFile, QSettings::IniFormat);
    const auto copyIfMissing = [&settings](const QString &key, const QVariant &value) {
        if (value.isValid() && !settings.contains(key))
            settings.setValue(key, value);
    };

    copyIfMissing(QStringLiteral("window/first_launch"),
                  settings.value(QStringLiteral("window/first_lunch")));
    const QVariant sidebar = settings.value(QStringLiteral("Workspace/SidebarAutoCollapse"));
    copyIfMissing(QStringLiteral("Workspace/LeftSidebarAutoCollapse"), sidebar);
    copyIfMissing(QStringLiteral("Workspace/RightSidebarAutoCollapse"), sidebar);
    const auto check = QStringLiteral("Update/AutomaticCheck");
    const auto notify = QStringLiteral("Update/UpdateNotification");
    if (settings.contains(check) || settings.contains(notify))
        copyIfMissing(QStringLiteral("Update/CheckAndNotify"),
                      settings.value(check).toBool() && settings.value(notify).toBool());

    for (const QString &key : {QStringLiteral("window/first_lunch"),
                               QStringLiteral("window/size"), QStringLiteral("window/pos"),
                               QStringLiteral("Workspace/SidebarAutoCollapse"), check, notify,
                               QStringLiteral("App/GreenMode"), QStringLiteral("Paths/Temp"),
                               QStringLiteral("Translation/Engine")}) {
        settings.remove(key);
    }
    settings.sync();
    if (settings.status() == QSettings::NoError) return true;
    if (errorMessage)
        *errorMessage = QStringLiteral("无法迁移设置文件：%1").arg(settingsFile);
    return false;
}

} // namespace

QString resolvedSettingsFile(QString settingsFile) { return settingsFile.isEmpty() ? Paths{}.settingsFile() : std::move(settingsFile); }

bool ensureDefaults(QString settingsFile, QString *errorMessage)
{
    settingsFile = resolvedSettingsFile(std::move(settingsFile));
    const QFileInfo fileInfo(settingsFile);
    if (fileInfo.exists())
        return migrateAppSettings(settingsFile, errorMessage);
    if (!QDir().mkpath(fileInfo.absolutePath()))
    {
        if (errorMessage != nullptr)
            *errorMessage = QStringLiteral("无法创建设置目录：%1").arg(fileInfo.absolutePath());
        return false;
    }

    saveApp(AppSettings{}, settingsFile);
    saveCrawler(CrawlerSettings{}, settingsFile);
    saveTranslation(TranslationSettings{}, settingsFile);
    saveValues(settingsFile, {
        {QStringLiteral("Paths/Version"), 2},
        {QStringLiteral("Paths/Database"), QStringLiteral("public/public.db")},
        {QStringLiteral("Paths/DatabaseBackups"), QStringLiteral("public/public_backup/")},
        {QStringLiteral("Paths/Actressimages"), QStringLiteral("public/actressimages/")},
        {QStringLiteral("Paths/Actorimages"), QStringLiteral("public/actorimages/")},
        {QStringLiteral("Paths/WorkCovers"), QStringLiteral("public/workcovers/")},
        {QStringLiteral("Paths/Fanart"), QStringLiteral("public/fanart/")},
        {QStringLiteral("Paths/PrivateDatabase"), QStringLiteral("private/private.db")},
        {QStringLiteral("Paths/PrivateDatabaseBackups"),
         QStringLiteral("private/private_backup/")},
    });
    return QFileInfo::exists(settingsFile);
}

AppSettings app(QString settingsFile)
{
    const QSettings m_store(resolvedSettingsFile(std::move(settingsFile)), QSettings::IniFormat);
    AppSettings settings;
    settings.themeId = m_store.value(QStringLiteral("App/Theme"), settings.themeId).toString();
    settings.customPrimary = m_store.value(QStringLiteral("App/CustomPrimary")).toString();
    settings.customPrimaryOklch = m_store.value(QStringLiteral("App/CustomPrimaryOklch")).toString();
    settings.colorMode = m_store.value(QStringLiteral("App/ColorMode"), settings.colorMode).toString();
    settings.firstLaunch =
        m_store.value(QStringLiteral("window/first_launch"), settings.firstLaunch).toBool();
    settings.maximized =
        m_store.value(QStringLiteral("window/maximized"), settings.maximized).toBool();
    settings.workLargeCoverView = m_store
                                      .value(QStringLiteral("WorkPage/LargeCoverView"),
                                             settings.workLargeCoverView)
                                      .toBool();
    settings.navigationBarVisible = m_store.value(QStringLiteral("Workspace/NavigationBarVisible"),
                                                  settings.navigationBarVisible).toBool();
    settings.hiddenNavigationItems = m_store.value(QStringLiteral("Workspace/HiddenNavigationItems")).toStringList();
    settings.leftSidebarAutoCollapse =
        m_store.value(QStringLiteral("Workspace/LeftSidebarAutoCollapse"),
                      settings.leftSidebarAutoCollapse).toBool();
    settings.rightSidebarAutoCollapse =
        m_store.value(QStringLiteral("Workspace/RightSidebarAutoCollapse"),
                      settings.rightSidebarAutoCollapse).toBool();
    settings.leftSidebarToggleVisible = m_store.value(QStringLiteral("Workspace/LeftSidebarToggleVisible"),
                                                     settings.leftSidebarToggleVisible).toBool();
    settings.rightSidebarToggleVisible = m_store.value(QStringLiteral("Workspace/RightSidebarToggleVisible"),
                                                      settings.rightSidebarToggleVisible).toBool();
    settings.workFilterAddGreenCover = m_store.value(QStringLiteral("WorkFilter/AddGreenCover"),
                                                    settings.workFilterAddGreenCover).toBool();
    bool validOption = false;
    const int scope = m_store.value(QStringLiteral("WorkFilter/InitialScope")).toInt(&validOption);
    if (validOption) settings.workFilterInitialScope = scope;
    const int sortOrder = m_store.value(QStringLiteral("WorkFilter/InitialSortOrder")).toInt(&validOption);
    if (validOption) settings.workFilterInitialSortOrder = sortOrder;
    settings.workTagSelectorVisible = m_store
                                          .value(QStringLiteral("WorkPage/TagSelectorVisible"),
                                                 settings.workTagSelectorVisible)
                                          .toBool();
    settings.shelfTagSelectorVisible = m_store
                                           .value(QStringLiteral("ShelfPage/TagSelectorVisible"),
                                                  settings.shelfTagSelectorVisible)
                                           .toBool();
    settings.localVideoPlayer =
        m_store.value(QStringLiteral("Video/LocalPlayerExe")).toString().trimmed();
    settings.videoPaths = readVideoPaths(m_store.value(QStringLiteral("Paths/Videos")).toString());
    settings.update.lastAutoCheckWeek =
        m_store.value(QStringLiteral("Update/LastAutoCheckWeek")).toString().trimmed();
    settings.update.checkAndNotify =
        m_store.value(QStringLiteral("Update/CheckAndNotify"),
                      settings.update.checkAndNotify).toBool();
    return settings;
}

CrawlerSettings crawler(QString settingsFile)
{
    const QSettings m_store(resolvedSettingsFile(std::move(settingsFile)), QSettings::IniFormat);
    CrawlerSettings settings;
    settings.workApiBaseUrl = QUrl(normalizedUrl(
        m_store.value(QStringLiteral("Crawler/WorkApiBaseUrl"),
                      settings.workApiBaseUrl.toString())
            .toString(),
        settings.workApiBaseUrl));
    settings.actressApiBaseUrl = QUrl(normalizedUrl(
        m_store.value(QStringLiteral("Crawler/ActressApiBaseUrl"),
                      settings.actressApiBaseUrl.toString())
            .toString(),
        settings.actressApiBaseUrl));
    settings.coverFetchApiUrl = QUrl(normalizedUrl(
        m_store.value(QStringLiteral("Crawler/CoverFetchApiUrl"),
                      settings.coverFetchApiUrl.toString())
            .toString(),
        settings.coverFetchApiUrl));
    settings.topActressesApiUrl = QUrl(normalizedUrl(
        m_store.value(QStringLiteral("Crawler/TopActressesApiUrl"),
                      settings.topActressesApiUrl.toString())
            .toString(),
        settings.topActressesApiUrl));
    settings.collectorExecutable =
        m_store.value(QStringLiteral("Crawler/CollectorExe")).toString().trimmed();
    settings.autoStartCollector =
        m_store.value(QStringLiteral("Crawler/AutoStartCollector"),
                      settings.autoStartCollector)
            .toBool();
    settings.unfinishedSerials = readUnfinishedSerials(
        m_store.value(QStringLiteral("Inbox/UnfinishedSerials")).toString());
    settings.webDav.enabled =
        m_store.value(QStringLiteral("WebDAV/Enabled"), settings.webDav.enabled).toBool();
    settings.webDav.profileName =
        m_store.value(QStringLiteral("WebDAV/ProfileName"), settings.webDav.profileName)
            .toString()
            .trimmed();
    if (settings.webDav.profileName.isEmpty())
        settings.webDav.profileName = QStringLiteral("default");
    settings.webDav.baseUrl = QUrl(m_store.value(QStringLiteral("WebDAV/BaseUrl")).toString().trimmed());
    settings.webDav.remoteRoot = normalizedWebDavRoot(
        m_store.value(QStringLiteral("WebDAV/RemoteRoot"), settings.webDav.remoteRoot).toString());
    settings.webDav.timeoutSeconds = qBound(
        3,
        m_store.value(QStringLiteral("WebDAV/TimeoutSeconds"), settings.webDav.timeoutSeconds)
            .toInt(),
        300);
    settings.webDav.autoUploadOnBackup = m_store
                                             .value(QStringLiteral("WebDAV/AutoUploadOnBackup"),
                                                    settings.webDav.autoUploadOnBackup)
                                             .toBool();
    return settings;
}

TranslationSettings translation(QString settingsFile)
{
    const QSettings m_store(resolvedSettingsFile(std::move(settingsFile)), QSettings::IniFormat);
    TranslationSettings settings;
    settings.model = m_store.value(QStringLiteral("Translation/Model")).toString().trimmed();
    settings.baseUrl = m_store.value(QStringLiteral("Translation/BaseUrl"), settings.baseUrl)
                           .toString()
                           .trimmed();
    settings.apiKey = m_store.value(QStringLiteral("Translation/ApiKey")).toString().trimmed();
    settings.timeoutSeconds = qMax(
        1, m_store.value(QStringLiteral("Translation/TimeoutS"), settings.timeoutSeconds).toInt());
    settings.retries = qMax(
        0, m_store.value(QStringLiteral("Translation/Retries"), settings.retries).toInt());
    settings.fallback = normalizedFallback(
        m_store.value(QStringLiteral("Translation/Fallback"), settings.fallback).toString());
    settings.llama.serverExecutable =
        m_store.value(QStringLiteral("LlamaCpp/ServerExePath")).toString().trimmed();
    settings.llama.modelPath =
        m_store.value(QStringLiteral("LlamaCpp/ModelPath")).toString().trimmed();
    settings.llama.host = m_store.value(QStringLiteral("LlamaCpp/Host"), settings.llama.host)
                              .toString()
                              .trimmed();
    if (settings.llama.host.isEmpty())
        settings.llama.host = QStringLiteral("127.0.0.1");
    settings.llama.port = qBound(
        1, m_store.value(QStringLiteral("LlamaCpp/Port"), settings.llama.port).toInt(), 65535);
    settings.llama.mode = normalizedMode(
        m_store.value(QStringLiteral("LlamaCpp/Mode"), settings.llama.mode).toString());
    settings.llama.contextSize = qMax(
        256, m_store.value(QStringLiteral("LlamaCpp/CtxSize"), settings.llama.contextSize).toInt());
    settings.llama.gpuLayers = qMax(
        0, m_store.value(QStringLiteral("LlamaCpp/GpuLayers"), settings.llama.gpuLayers).toInt());
    settings.llama.threads = qMax(
        1, m_store.value(QStringLiteral("LlamaCpp/Threads"), settings.llama.threads).toInt());
    settings.llama.threadsBatch = qMax(
        1, m_store.value(QStringLiteral("LlamaCpp/ThreadsBatch"), settings.llama.threadsBatch)
               .toInt());
    settings.llama.batchSize = qMax(
        1, m_store.value(QStringLiteral("LlamaCpp/BatchSize"), settings.llama.batchSize).toInt());
    settings.llama.microBatchSize = qMax(
        1, m_store.value(QStringLiteral("LlamaCpp/UBatchSize"), settings.llama.microBatchSize)
               .toInt());
    settings.llama.mlock =
        m_store.value(QStringLiteral("LlamaCpp/Mlock"), settings.llama.mlock).toBool();
    settings.llama.autoSyncTranslation = m_store
                                             .value(QStringLiteral("LlamaCpp/AutoSyncTranslation"),
                                                    settings.llama.autoSyncTranslation)
                                             .toBool();
    settings.llama.autoStart =
        m_store.value(QStringLiteral("LlamaCpp/AutoStart"), settings.llama.autoStart).toBool();
    return settings;
}

void saveApp(const AppSettings &settings, QString settingsFile)
{
    saveValues(resolvedSettingsFile(std::move(settingsFile)), {
        {QStringLiteral("App/Theme"), settings.themeId},
        {QStringLiteral("App/CustomPrimary"), settings.customPrimary},
        {QStringLiteral("App/CustomPrimaryOklch"), settings.customPrimaryOklch},
        {QStringLiteral("App/ColorMode"), settings.colorMode},
        {QStringLiteral("window/first_launch"), settings.firstLaunch},
        {QStringLiteral("window/maximized"), settings.maximized},
        {QStringLiteral("WorkPage/LargeCoverView"), settings.workLargeCoverView},
        {QStringLiteral("Workspace/NavigationBarVisible"), settings.navigationBarVisible},
        {QStringLiteral("Workspace/HiddenNavigationItems"), settings.hiddenNavigationItems},
        {QStringLiteral("Workspace/LeftSidebarAutoCollapse"), settings.leftSidebarAutoCollapse},
        {QStringLiteral("Workspace/RightSidebarAutoCollapse"), settings.rightSidebarAutoCollapse},
        {QStringLiteral("Workspace/LeftSidebarToggleVisible"), settings.leftSidebarToggleVisible},
        {QStringLiteral("Workspace/RightSidebarToggleVisible"), settings.rightSidebarToggleVisible},
        {QStringLiteral("WorkFilter/AddGreenCover"), settings.workFilterAddGreenCover},
        {QStringLiteral("WorkFilter/InitialScope"), settings.workFilterInitialScope},
        {QStringLiteral("WorkFilter/InitialSortOrder"), settings.workFilterInitialSortOrder},
        {QStringLiteral("WorkPage/TagSelectorVisible"), settings.workTagSelectorVisible},
        {QStringLiteral("ShelfPage/TagSelectorVisible"), settings.shelfTagSelectorVisible},
        {QStringLiteral("Video/LocalPlayerExe"), settings.localVideoPlayer.trimmed()},
        {QStringLiteral("Paths/Videos"), normalizedVideoPaths(settings.videoPaths).join(u',')},
        {QStringLiteral("Update/LastAutoCheckWeek"), settings.update.lastAutoCheckWeek.trimmed()},
        {QStringLiteral("Update/CheckAndNotify"), settings.update.checkAndNotify},
    });
}

void saveCrawler(const CrawlerSettings &settings, QString settingsFile)
{
    saveValues(resolvedSettingsFile(std::move(settingsFile)), {
        {QStringLiteral("Crawler/WorkApiBaseUrl"),
         normalizedUrl(settings.workApiBaseUrl.toString(), CrawlerSettings{}.workApiBaseUrl)},
        {QStringLiteral("Crawler/ActressApiBaseUrl"),
         normalizedUrl(settings.actressApiBaseUrl.toString(), CrawlerSettings{}.actressApiBaseUrl)},
        {QStringLiteral("Crawler/CoverFetchApiUrl"),
         normalizedUrl(settings.coverFetchApiUrl.toString(), CrawlerSettings{}.coverFetchApiUrl)},
        {QStringLiteral("Crawler/TopActressesApiUrl"),
         normalizedUrl(settings.topActressesApiUrl.toString(),
                       CrawlerSettings{}.topActressesApiUrl)},
        {QStringLiteral("Crawler/CollectorExe"), settings.collectorExecutable.trimmed()},
        {QStringLiteral("Crawler/AutoStartCollector"), settings.autoStartCollector},
        {QStringLiteral("Inbox/UnfinishedSerials"), settings.unfinishedSerials.join(u',')},
        {QStringLiteral("WebDAV/Enabled"), settings.webDav.enabled},
        {QStringLiteral("WebDAV/ProfileName"), settings.webDav.profileName.trimmed().isEmpty()
                                                    ? QStringLiteral("default")
                                                    : settings.webDav.profileName.trimmed()},
        {QStringLiteral("WebDAV/BaseUrl"), settings.webDav.baseUrl.toString().trimmed()},
        {QStringLiteral("WebDAV/RemoteRoot"), normalizedWebDavRoot(settings.webDav.remoteRoot)},
        {QStringLiteral("WebDAV/TimeoutSeconds"),
         qBound(settings.webDav.timeoutSeconds, 3, 300)},
        {QStringLiteral("WebDAV/AutoUploadOnBackup"), settings.webDav.autoUploadOnBackup},
    });
}

void saveTranslation(const TranslationSettings &settings, QString settingsFile)
{
    saveValues(resolvedSettingsFile(std::move(settingsFile)), {
        {QStringLiteral("Translation/Model"), settings.model.trimmed()},
        {QStringLiteral("Translation/BaseUrl"), settings.baseUrl.trimmed()},
        {QStringLiteral("Translation/ApiKey"), settings.apiKey.trimmed()},
        {QStringLiteral("Translation/TimeoutS"), qMax(1, settings.timeoutSeconds)},
        {QStringLiteral("Translation/Retries"), qMax(0, settings.retries)},
        {QStringLiteral("Translation/Fallback"), normalizedFallback(settings.fallback)},
        {QStringLiteral("LlamaCpp/ServerExePath"), settings.llama.serverExecutable.trimmed()},
        {QStringLiteral("LlamaCpp/ModelPath"), settings.llama.modelPath.trimmed()},
        {QStringLiteral("LlamaCpp/Host"),
         settings.llama.host.trimmed().isEmpty() ? QStringLiteral("127.0.0.1")
                                                 : settings.llama.host.trimmed()},
        {QStringLiteral("LlamaCpp/Port"), qBound(settings.llama.port, 1, 65535)},
        {QStringLiteral("LlamaCpp/Mode"), normalizedMode(settings.llama.mode)},
        {QStringLiteral("LlamaCpp/CtxSize"), qMax(256, settings.llama.contextSize)},
        {QStringLiteral("LlamaCpp/GpuLayers"), qMax(0, settings.llama.gpuLayers)},
        {QStringLiteral("LlamaCpp/Threads"), qMax(1, settings.llama.threads)},
        {QStringLiteral("LlamaCpp/ThreadsBatch"), qMax(1, settings.llama.threadsBatch)},
        {QStringLiteral("LlamaCpp/BatchSize"), qMax(1, settings.llama.batchSize)},
        {QStringLiteral("LlamaCpp/UBatchSize"), qMax(1, settings.llama.microBatchSize)},
        {QStringLiteral("LlamaCpp/Mlock"), settings.llama.mlock},
        {QStringLiteral("LlamaCpp/AutoSyncTranslation"), settings.llama.autoSyncTranslation},
        {QStringLiteral("LlamaCpp/AutoStart"), settings.llama.autoStart},
    });
}


bool ensureDefaults(QString *errorMessage)
{
    return ensureDefaults({}, errorMessage);
}

} // namespace darkeye
