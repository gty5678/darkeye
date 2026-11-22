#pragma once

#include <QSqlDatabase>
#include <QStringList>
#include <QUrl>

#include "settings/Settings.h"

namespace darkeye
{

struct WebDavActionResult final
{
    bool succeeded = false;
    QString message{};
    QString localPath{};
    QString remotePath{};
};

class WebDavBackupService final
{
public:
    static WebDavActionResult testConnection(const CrawlerSettings::WebDav &settings);
    static WebDavActionResult uploadDatabaseBackup(QSqlDatabase database, const QString &backupDirectory,
                                                   const QString &prefix, const CrawlerSettings::WebDav &settings);
    static WebDavActionResult uploadFile(const QString &localPath, const CrawlerSettings::WebDav &settings);
    static WebDavActionResult uploadPublicSnapshot(
        QSqlDatabase database, const QString &snapshotRoot, const QString &workCoversDirectory,
        const QString &fanartDirectory, const QString &actressImagesDirectory,
        const QString &actorImagesDirectory, const CrawlerSettings::WebDav &settings);
    static WebDavActionResult listBackups(const CrawlerSettings::WebDav &settings, QStringList *files);
    static WebDavActionResult restoreDatabaseBackup(QSqlDatabase database, const QString &remotePath,
                                                    const QString &temporaryDirectory,
                                                    const CrawlerSettings::WebDav &settings);
};

} // namespace darkeye
