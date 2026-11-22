#include "database/DatabaseMaintenanceService.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <QTemporaryFile>
#include <QSqlError>
#include <QSqlQuery>
#include <QtCore/private/qzipwriter_p.h>

namespace darkeye
{
namespace
{

QString timestamp()
{
    return QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd-HH-mm-ss"));
}

QString quotedIdentifier(const QString &value)
{
    QString escaped = value;
    escaped.replace(QLatin1Char('"'), QStringLiteral("\"\""));
    return QLatin1Char('"') + escaped + QLatin1Char('"');
}

bool copyTree(const QString &source, const QString &destination, QString *errorMessage)
{
    const QDir sourceDir(source);
    if (!sourceDir.exists()) return true;
    if (!QDir().mkpath(destination))
    {
        if (errorMessage) *errorMessage = QStringLiteral("无法创建目录：%1").arg(destination);
        return false;
    }

    QDirIterator iterator(source, QDir::Files | QDir::Hidden, QDirIterator::Subdirectories);
    while (iterator.hasNext())
    {
        const QString sourceFile = iterator.next();
        const QString relativePath = sourceDir.relativeFilePath(sourceFile);
        const QString destinationFile = QDir(destination).filePath(relativePath);
        if (!QDir().mkpath(QFileInfo(destinationFile).absolutePath()))
        {
            if (errorMessage) *errorMessage = QStringLiteral("无法创建目录：%1")
                                                .arg(QFileInfo(destinationFile).absolutePath());
            return false;
        }
        QFile::remove(destinationFile);
        if (!QFile::copy(sourceFile, destinationFile))
        {
            if (errorMessage) *errorMessage = QStringLiteral("无法复制文件：%1").arg(sourceFile);
            return false;
        }
    }
    return true;
}

QStringList userTables(QSqlDatabase database, const QString &schema = {})
{
    const QString prefix = schema.isEmpty() ? QString() : schema + QLatin1Char('.');
    QSqlQuery query(database);
    if (!query.exec(QStringLiteral("SELECT name FROM %1sqlite_master WHERE type='table' "
                                   "AND name NOT LIKE 'sqlite_'").arg(prefix)))
        return {};
    QStringList tables;
    while (query.next()) tables.append(query.value(0).toString());
    return tables;
}

DatabaseMaintenanceResult failure(const QString &message)
{
    return {.succeeded = false, .message = message};
}

} // namespace

DatabaseMaintenanceResult DatabaseMaintenanceService::createBackup(
    QSqlDatabase database, const QString &backupDirectory, const QString &prefix)
{
    if (!database.isOpen()) return failure(QStringLiteral("数据库尚未打开。"));
    if (!QDir().mkpath(backupDirectory))
        return failure(QStringLiteral("无法创建备份目录：%1").arg(backupDirectory));

    const QString name = QStringLiteral("%1-backup-%2.db").arg(prefix, timestamp());
    const QString backupPath = QDir(backupDirectory).filePath(name);
    QString escapedPath = QFileInfo(backupPath).absoluteFilePath();
    escapedPath.replace(QLatin1Char('\''), QStringLiteral("''"));
    QSqlQuery query(database);
    if (!query.exec(QStringLiteral("VACUUM main INTO '%1'").arg(escapedPath)))
        return failure(QStringLiteral("数据库备份失败：%1").arg(query.lastError().text()));
    QFileInfo backupInfo(backupPath);
    if (!backupInfo.isFile() || backupInfo.size() <= 0)
        return failure(QStringLiteral("数据库备份未生成有效文件：%1").arg(backupPath));
    return {.succeeded = true,
            .message = QStringLiteral("本地备份成功。"),
            .outputPath = backupPath};
}

DatabaseMaintenanceResult DatabaseMaintenanceService::restoreBackup(QSqlDatabase database,
                                                                     const QString &backupPath)
{
    if (!database.isOpen()) return failure(QStringLiteral("数据库尚未打开。"));
    if (!QFileInfo(backupPath).isFile())
        return failure(QStringLiteral("备份文件不存在：%1").arg(backupPath));

    QSqlQuery pragma(database);
    if (!pragma.exec(QStringLiteral("PRAGMA foreign_keys")) || !pragma.next())
        return failure(QStringLiteral("无法读取外键设置。"));
    const bool foreignKeysEnabled = pragma.value(0).toBool();
    pragma.finish();
    const auto restoreForeignKeys = [&] {
        pragma.exec(foreignKeysEnabled ? QStringLiteral("PRAGMA foreign_keys=ON")
                                      : QStringLiteral("PRAGMA foreign_keys=OFF"));
    };
    if (!pragma.exec(QStringLiteral("PRAGMA foreign_keys=OFF")))
        return failure(QStringLiteral("无法暂停外键约束。"));
    // Begin before ATTACH: a caller-owned transaction must remain untouched,
    // and must not acquire an attachment that cannot be detached until it ends.
    if (!database.transaction())
    {
        restoreForeignKeys();
        return failure(QStringLiteral("无法开始恢复事务：%1").arg(database.lastError().text()));
    }
    QSqlQuery attach(database);
    attach.prepare(QStringLiteral("ATTACH DATABASE ? AS restore_source"));
    attach.addBindValue(backupPath);
    if (!attach.exec())
    {
        const QString error = attach.lastError().text();
        database.rollback();
        restoreForeignKeys();
        return failure(QStringLiteral("无法打开备份：%1").arg(error));
    }
    const auto detach = [&database] {
        QSqlQuery query(database);
        query.exec(QStringLiteral("DETACH DATABASE restore_source"));
    };
    const auto fail = [&](const QString &message) {
        database.rollback();
        detach();
        restoreForeignKeys();
        return failure(message);
    };
    const QStringList tables = userTables(database, QStringLiteral("restore_source"));
    if (tables.isEmpty())
        return fail(QStringLiteral("备份中没有可恢复的数据表。"));
    for (const QString &table : tables)
    {
        const QString identifier = quotedIdentifier(table);
        QSqlQuery query(database);
        if (!query.exec(QStringLiteral("DELETE FROM %1").arg(identifier))
            || !query.exec(QStringLiteral("INSERT INTO %1 SELECT * FROM restore_source.%1")
                               .arg(identifier)))
            return fail(QStringLiteral("恢复失败：%1").arg(query.lastError().text()));
    }
    // Re-enabling foreign_keys does not validate rows inserted while it was off.
    // Validate the restored data before committing so corruption rolls back.
    {
        QSqlQuery check(database);
        if (!check.exec(QStringLiteral("PRAGMA main.foreign_key_check")))
            return fail(QStringLiteral("恢复外键检查失败：%1").arg(check.lastError().text()));
        if (check.next())
        {
            check.finish();
            return fail(QStringLiteral("备份包含无效的外键关联，已取消恢复。"));
        }
    }
    if (!database.commit())
        return fail(QStringLiteral("恢复提交失败：%1").arg(database.lastError().text()));
    detach();
    restoreForeignKeys();
    QSqlQuery checkpoint(database);
    checkpoint.exec(QStringLiteral("PRAGMA wal_checkpoint(FULL)"));
    return {.succeeded = true, .message = QStringLiteral("数据库恢复完成。")};
}

DatabaseMaintenanceResult DatabaseMaintenanceService::backupAndVacuum(
    QSqlDatabase publicDatabase, QSqlDatabase privateDatabase, const QString &publicBackupDirectory,
    const QString &privateBackupDirectory)
{
    const auto publicBackup = createBackup(publicDatabase, publicBackupDirectory,
                                           QStringLiteral("darkeye-public-pre-vacuum"));
    if (!publicBackup.succeeded) return publicBackup;
    const auto privateBackup = createBackup(privateDatabase, privateBackupDirectory,
                                            QStringLiteral("darkeye-private-pre-vacuum"));
    if (!privateBackup.succeeded) return privateBackup;

    for (const QSqlDatabase &database : {publicDatabase, privateDatabase})
    {
        QSqlQuery query(database);
        if (!query.exec(QStringLiteral("VACUUM")))
            return failure(QStringLiteral("清理数据库碎片失败：%1").arg(query.lastError().text()));
    }
    return {.succeeded = true,
            .message = QStringLiteral("公共库和私库已备份并完成碎片整理。")};
}

DatabaseMaintenanceResult DatabaseMaintenanceService::createPublicSnapshot(
    QSqlDatabase database, const QString &snapshotRoot, const QString &workCoversDirectory,
    const QString &fanartDirectory, const QString &actressImagesDirectory,
    const QString &actorImagesDirectory)
{
    const QString snapshotDirectory = QDir(snapshotRoot).filePath(
        QStringLiteral("snapshot-%1").arg(timestamp()));
    if (QFileInfo::exists(snapshotDirectory) || QFileInfo::exists(snapshotDirectory + QStringLiteral(".zip")))
        return failure(QStringLiteral("拒绝覆盖已有快照：%1").arg(snapshotDirectory));
    if (!QDir().mkpath(snapshotDirectory))
        return failure(QStringLiteral("无法创建快照目录：%1").arg(snapshotDirectory));

    auto backup = createBackup(database, snapshotDirectory, QStringLiteral("public"));
    if (!backup.succeeded) return backup;
    const QList<QPair<QString, QString>> resources = {
        {actorImagesDirectory, QStringLiteral("actorimages")},
        {actressImagesDirectory, QStringLiteral("actressimages")},
        {workCoversDirectory, QStringLiteral("workcovers")},
        {fanartDirectory, QStringLiteral("fanart")},
    };
    QJsonArray resourceInfo;
    for (const auto &[source, name] : resources)
    {
        QString errorMessage;
        const QString resourceDirectory = QDir(snapshotDirectory).filePath(name);
        if (!copyTree(source, resourceDirectory, &errorMessage))
            return failure(errorMessage);
        qint64 fileCount = 0;
        qint64 totalSize = 0;
        QDirIterator iterator(resourceDirectory, QDir::Files | QDir::Hidden,
                              QDirIterator::Subdirectories);
        while (iterator.hasNext())
        {
            iterator.next();
            ++fileCount;
            totalSize += iterator.fileInfo().size();
        }
        resourceInfo.append(QJsonObject{
            {QStringLiteral("name"), name},
            {QStringLiteral("path"), name},
            {QStringLiteral("file_count"), fileCount},
            {QStringLiteral("total_size"), totalSize},
        });
    }
    QJsonObject metadata;
    metadata.insert(QStringLiteral("version"), 1);
    metadata.insert(QStringLiteral("created_at"), QDateTime::currentDateTime().toString(Qt::ISODate));
    metadata.insert(QStringLiteral("app_version"), QCoreApplication::applicationVersion());
    metadata.insert(QStringLiteral("db"), QJsonObject{
        {QStringLiteral("file"), QFileInfo(backup.outputPath).fileName()},
        {QStringLiteral("type"), QStringLiteral("sqlite")},
        {QStringLiteral("size"), QFileInfo(backup.outputPath).size()},
    });
    metadata.insert(QStringLiteral("resources"), resourceInfo);
    QFile meta(QDir(snapshotDirectory).filePath(QStringLiteral("meta.json")));
    if (!meta.open(QIODevice::WriteOnly)) return failure(QStringLiteral("无法写入快照元数据。"));
    const QByteArray metadataJson = QJsonDocument(metadata).toJson(QJsonDocument::Indented);
    if (meta.write(metadataJson) != metadataJson.size() || !meta.flush())
        return failure(QStringLiteral("无法写入快照元数据。"));
    return {.succeeded = true, .message = QStringLiteral("公共库完整快照成功。"),
            .outputPath = snapshotDirectory};
}

DatabaseMaintenanceResult DatabaseMaintenanceService::compressPublicSnapshot(const QString &snapshotDirectory)
{
    const QDir directory(snapshotDirectory);
    if (!directory.exists() || !QFileInfo(directory.filePath(QStringLiteral("meta.json"))).isFile())
        return failure(QStringLiteral("快照目录或元数据不存在：%1").arg(snapshotDirectory));
    const QString archivePath = QFileInfo(snapshotDirectory).absoluteFilePath() + QStringLiteral(".zip");
    if (QFileInfo::exists(archivePath))
        return failure(QStringLiteral("拒绝覆盖已有 ZIP：%1").arg(archivePath));

    // Commit only after all entries and the central directory have been written.
    QTemporaryFile output(archivePath + QStringLiteral(".XXXXXX"));
    if (!output.open())
        return {.message = QStringLiteral("无法创建快照 ZIP：%1").arg(output.errorString()),
                .outputPath = snapshotDirectory};
    QZipWriter archive(&output);
    archive.setCompressionPolicy(QZipWriter::AlwaysCompress);
    // Qt's writer emits classic ZIP, so never publish a truncated ZIP64-sized backup.
    constexpr qint64 zipSizeLimit = 0xffffffffLL;
    int entryCount = 0;
    QDirIterator iterator(directory.absolutePath(), QDir::Files | QDir::Hidden,
                          QDirIterator::Subdirectories);
    while (iterator.hasNext())
    {
        QFile file(iterator.next());
        if (++entryCount >= 65535 || QFileInfo(file).size() >= zipSizeLimit
            || output.pos() >= zipSizeLimit)
            return {.message = QStringLiteral("快照超过当前 ZIP 容量（4 GiB 或 65534 个文件），本地快照已保留。"),
                    .outputPath = snapshotDirectory};
        if (!file.open(QIODevice::ReadOnly))
            return {.message = QStringLiteral("无法读取快照文件：%1").arg(file.fileName()),
                    .outputPath = snapshotDirectory};
        archive.addFile(QDir::fromNativeSeparators(directory.relativeFilePath(file.fileName())), &file);
        if (file.error() != QFileDevice::NoError || archive.status() != QZipWriter::NoError)
            return {.message = QStringLiteral("快照 ZIP 压缩失败：%1").arg(file.fileName()),
                    .outputPath = snapshotDirectory};
    }
    archive.close();
    if (output.size() >= zipSizeLimit)
        return {.message = QStringLiteral("快照 ZIP 超过 4 GiB，本地快照已保留。"),
                .outputPath = snapshotDirectory};
    if (archive.status() != QZipWriter::NoError || !output.rename(archivePath))
        return {.message = QStringLiteral("无法保存快照 ZIP：%1").arg(archivePath),
                .outputPath = snapshotDirectory};
    output.setAutoRemove(false);
    return {.succeeded = true, .message = QStringLiteral("本地完整快照及 ZIP 压缩成功。"),
            .outputPath = archivePath};
}

DatabaseMaintenanceResult DatabaseMaintenanceService::restorePublicSnapshot(
    QSqlDatabase database, const QString &metaPath, const QString &workCoversDirectory,
    const QString &fanartDirectory, const QString &actressImagesDirectory,
    const QString &actorImagesDirectory)
{
    QFile meta(metaPath);
    if (!meta.open(QIODevice::ReadOnly)) return failure(QStringLiteral("无法读取快照元数据。"));
    const QJsonDocument document = QJsonDocument::fromJson(meta.readAll());
    if (!document.isObject() || document.object().value(QStringLiteral("version")).toInt() != 1)
        return failure(QStringLiteral("不支持的快照元数据。"));
    const QDir snapshotDirectory(QFileInfo(metaPath).absolutePath());
    const QJsonObject metadata = document.object();
    // Older C++ snapshots also used version 1, with a top-level database field.
    const QString databaseName = metadata.contains(QStringLiteral("db"))
        ? metadata.value(QStringLiteral("db")).toObject().value(QStringLiteral("file")).toString()
        : metadata.value(QStringLiteral("database")).toString();
    if (databaseName.isEmpty()) return failure(QStringLiteral("快照元数据缺少 db.file 信息。"));
    const auto restored = restoreBackup(database, snapshotDirectory.filePath(databaseName));
    if (!restored.succeeded) return restored;
    const QList<QPair<QString, QString>> resources = {
        {QStringLiteral("workcovers"), workCoversDirectory}, {QStringLiteral("fanart"), fanartDirectory},
        {QStringLiteral("actressimages"), actressImagesDirectory}, {QStringLiteral("actorimages"), actorImagesDirectory},
    };
    for (const auto &[name, destination] : resources)
    {
        QString errorMessage;
        if (!copyTree(snapshotDirectory.filePath(name), destination, &errorMessage)) return failure(errorMessage);
    }
    return {.succeeded = true, .message = QStringLiteral("公共库及图片快照恢复完成。")};
}

DatabaseMaintenanceResult DatabaseMaintenanceService::checkImageConsistency(
    QSqlDatabase database, const QString &directory, const QString &table, const QString &column)
{
    QSet<QString> referencedFiles;
    QSqlQuery query(database);
    if (!query.exec(QStringLiteral("SELECT %1 FROM %2 WHERE %1 IS NOT NULL AND %1 != ''")
                        .arg(quotedIdentifier(column), quotedIdentifier(table))))
        return failure(QStringLiteral("读取图片记录失败：%1").arg(query.lastError().text()));
    while (query.next())
    {
        const QString path = query.value(0).toString();
        if (!path.isEmpty()) referencedFiles.insert(QFileInfo(path).fileName());
    }
    QSet<QString> localFiles;
    QDirIterator iterator(directory, QDir::Files, QDirIterator::Subdirectories);
    while (iterator.hasNext()) localFiles.insert(QFileInfo(iterator.next()).fileName());
    const int missing = (referencedFiles - localFiles).size();
    const int extra = (localFiles - referencedFiles).size();
    return {.succeeded = true,
            .message = QStringLiteral("%1：缺失 %2 个，多余 %3 个。").arg(table).arg(missing).arg(extra),
            .missingFiles = missing, .extraFiles = extra};
}

DatabaseMaintenanceResult DatabaseMaintenanceService::rebuildPrivateLinks(
    QSqlDatabase publicDatabase, const QString &privateDatabasePath)
{
    if (!publicDatabase.isOpen()) return failure(QStringLiteral("公共数据库尚未打开。"));
    if (!QFileInfo(privateDatabasePath).isFile())
        return failure(QStringLiteral("私有数据库不存在：%1").arg(privateDatabasePath));
    QSqlQuery attach(publicDatabase);
    attach.prepare(QStringLiteral("ATTACH DATABASE ? AS private_db"));
    attach.addBindValue(privateDatabasePath);
    if (!attach.exec()) return failure(QStringLiteral("无法附加私有库：%1").arg(attach.lastError().text()));
    const auto detach = [&publicDatabase] { QSqlQuery(publicDatabase).exec(QStringLiteral("DETACH DATABASE private_db")); };
    if (!publicDatabase.transaction())
    {
        detach();
        return failure(QStringLiteral("无法开始重建事务：%1").arg(publicDatabase.lastError().text()));
    }
    int createdWorks = 0;
    int createdActresses = 0;
    const auto fail = [&](const QString &error) {
        publicDatabase.rollback();
        detach();
        return failure(error);
    };
    QSqlQuery actresses(publicDatabase);
    if (!actresses.exec(QStringLiteral("SELECT favorite_actress_id, actress_id, jp_name FROM private_db.favorite_actress")))
        return fail(actresses.lastError().text());
    while (actresses.next())
    {
        const qint64 favoriteId = actresses.value(0).toLongLong();
        const qint64 oldId = actresses.value(1).toLongLong();
        const QString japaneseName = actresses.value(2).toString();
        if (japaneseName.isEmpty()) continue;
        QSqlQuery find(publicDatabase);
        find.prepare(QStringLiteral("SELECT actress_id FROM actress_name WHERE jp=? LIMIT 1"));
        find.addBindValue(japaneseName);
        if (!find.exec()) return fail(find.lastError().text());
        qint64 newId = 0;
        if (find.next()) newId = find.value(0).toLongLong();
        else
        {
            QSqlQuery insertActress(publicDatabase);
            if (!insertActress.exec(QStringLiteral("INSERT INTO actress DEFAULT VALUES"))) return fail(insertActress.lastError().text());
            newId = insertActress.lastInsertId().toLongLong();
            QSqlQuery insertName(publicDatabase);
            insertName.prepare(QStringLiteral("INSERT INTO actress_name(actress_id,name_type,cn,jp) VALUES(?,1,?,?)"));
            insertName.addBindValue(newId); insertName.addBindValue(japaneseName); insertName.addBindValue(japaneseName);
            if (!insertName.exec()) return fail(insertName.lastError().text());
            ++createdActresses;
        }
        if (newId != oldId)
        {
            QSqlQuery update(publicDatabase);
            update.prepare(QStringLiteral("UPDATE private_db.favorite_actress SET actress_id=? WHERE favorite_actress_id=?"));
            update.addBindValue(newId); update.addBindValue(favoriteId);
            if (!update.exec()) return fail(update.lastError().text());
        }
    }
    const auto rebuildWorkTable = [&](const QString &table, const QString &primaryKey) -> QString {
        QSqlQuery rows(publicDatabase);
        if (!rows.exec(QStringLiteral("SELECT %1, work_id, serial_number FROM private_db.%2")
                           .arg(quotedIdentifier(primaryKey), quotedIdentifier(table)))) return rows.lastError().text();
        while (rows.next())
        {
            const qint64 recordId = rows.value(0).toLongLong();
            const qint64 oldId = rows.value(1).toLongLong();
            const QString serial = rows.value(2).toString();
            if (serial.isEmpty()) continue;
            QSqlQuery find(publicDatabase);
            find.prepare(QStringLiteral("SELECT work_id FROM work WHERE serial_number=? LIMIT 1"));
            find.addBindValue(serial);
            if (!find.exec()) return find.lastError().text();
            qint64 newId = 0;
            if (find.next()) newId = find.value(0).toLongLong();
            else
            {
                QSqlQuery insert(publicDatabase);
                insert.prepare(QStringLiteral("INSERT INTO work(serial_number) VALUES(?)"));
                insert.addBindValue(serial);
                if (!insert.exec()) return insert.lastError().text();
                newId = insert.lastInsertId().toLongLong();
                ++createdWorks;
            }
            if (newId != oldId)
            {
                QSqlQuery update(publicDatabase);
                update.prepare(QStringLiteral("UPDATE private_db.%1 SET work_id=? WHERE %2=?")
                                   .arg(quotedIdentifier(table), quotedIdentifier(primaryKey)));
                update.addBindValue(newId); update.addBindValue(recordId);
                if (!update.exec()) return update.lastError().text();
            }
        }
        return {};
    };
    if (const QString error = rebuildWorkTable(QStringLiteral("favorite_work"), QStringLiteral("favorite_work_id")); !error.isEmpty()) return fail(error);
    if (const QString error = rebuildWorkTable(QStringLiteral("masturbation"), QStringLiteral("masturbation_id")); !error.isEmpty()) return fail(error);
    if (!publicDatabase.commit())
    {
        const QString error = publicDatabase.lastError().text();
        detach();
        return failure(error);
    }
    detach();
    return {.succeeded = true,
            .message = QStringLiteral("私库关联已重建。"),
            .createdWorks = createdWorks,
            .createdActresses = createdActresses};
}

} // namespace darkeye
