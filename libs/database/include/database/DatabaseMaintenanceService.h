#pragma once

#include <QSqlDatabase>
#include <QString>

namespace darkeye
{

struct DatabaseMaintenanceResult final
{
    bool succeeded = false;
    QString message;
    QString outputPath;
    int missingFiles = 0;
    int extraFiles = 0;
    int createdWorks = 0;
    int createdActresses = 0;
};

class DatabaseMaintenanceService final
{
public:
    static DatabaseMaintenanceResult createBackup(QSqlDatabase database,
                                                  const QString &backupDirectory,
                                                  const QString &prefix);
    static DatabaseMaintenanceResult restoreBackup(QSqlDatabase database,
                                                   const QString &backupPath);
    static DatabaseMaintenanceResult backupAndVacuum(
        QSqlDatabase publicDatabase, QSqlDatabase privateDatabase,
        const QString &publicBackupDirectory, const QString &privateBackupDirectory);
    static DatabaseMaintenanceResult createPublicSnapshot(
        QSqlDatabase database, const QString &snapshotRoot, const QString &workCoversDirectory,
        const QString &fanartDirectory, const QString &actressImagesDirectory,
        const QString &actorImagesDirectory);
    static DatabaseMaintenanceResult restorePublicSnapshot(
        QSqlDatabase database, const QString &metaPath, const QString &workCoversDirectory,
        const QString &fanartDirectory, const QString &actressImagesDirectory,
        const QString &actorImagesDirectory);
    static DatabaseMaintenanceResult compressPublicSnapshot(const QString &snapshotDirectory);
    static DatabaseMaintenanceResult checkImageConsistency(
        QSqlDatabase database, const QString &directory, const QString &table,
        const QString &column);
    static DatabaseMaintenanceResult rebuildPrivateLinks(QSqlDatabase publicDatabase,
                                                          const QString &privateDatabasePath);
};

} // namespace darkeye
