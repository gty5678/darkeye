#include "database/DatabaseBackupService.h"
#include "database/DatabaseMaintenanceService.h"
#include "database/SchemaManager.h"

#include <QDir>
#include <QFileInfo>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QtTest>

class DatabaseBackupServiceTest final : public QObject
{
    Q_OBJECT

private slots:
    void createsReadableConsistentBackup();
    void refusesToOverwriteExistingBackup();
    void restoresBackupIntoOpenDatabase();
    void createsPythonCompatibleSnapshot();
    void restoresVersionOneSnapshot_data();
    void restoresVersionOneSnapshot();
    void rejectsForeignKeyCorruptionWithoutChangingData();
    void restoresFailedCopyAtomically();
    void refusesRestoreInsideTransaction();
    void restoresCompleteDatabase_data();
    void restoresCompleteDatabase();
};

void DatabaseBackupServiceTest::createsReadableConsistentBackup()
{
    QTemporaryDir temporaryDirectory;
    QVERIFY(temporaryDirectory.isValid());
    const QDir root(temporaryDirectory.path());
    const QString sourcePath = root.filePath(QStringLiteral("source.db"));
    const QString backupPath = root.filePath(QStringLiteral("backups/source.db"));

    darkeye::SqliteConnection source;
    QString errorMessage;
    QVERIFY2(source.open(sourcePath, false, &errorMessage), qPrintable(errorMessage));
    QSqlQuery createQuery(source.database());
    QVERIFY(createQuery.exec(QStringLiteral("CREATE TABLE item(value TEXT NOT NULL)")));
    QVERIFY(createQuery.exec(QStringLiteral("INSERT INTO item(value) VALUES('preserved')")));

    QVERIFY2(darkeye::DatabaseBackupService::createConsistentBackup(
                 source, backupPath, &errorMessage),
             qPrintable(errorMessage));
    QVERIFY(QFileInfo(backupPath).size() > 0);

    darkeye::SqliteConnection backup;
    QVERIFY2(backup.open(backupPath, true, &errorMessage), qPrintable(errorMessage));
    QSqlQuery readQuery(backup.database());
    QVERIFY(readQuery.exec(QStringLiteral("SELECT value FROM item")));
    QVERIFY(readQuery.next());
    QCOMPARE(readQuery.value(0).toString(), QStringLiteral("preserved"));
}

void DatabaseBackupServiceTest::refusesToOverwriteExistingBackup()
{
    QTemporaryDir temporaryDirectory;
    QVERIFY(temporaryDirectory.isValid());
    const QDir root(temporaryDirectory.path());
    const QString sourcePath = root.filePath(QStringLiteral("source.db"));
    const QString backupPath = root.filePath(QStringLiteral("source-backup.db"));

    darkeye::SqliteConnection source;
    QString errorMessage;
    QVERIFY2(source.open(sourcePath, false, &errorMessage), qPrintable(errorMessage));
    QSqlQuery query(source.database());
    QVERIFY(query.exec(QStringLiteral("CREATE TABLE item(value TEXT)")));
    QVERIFY2(darkeye::DatabaseBackupService::createConsistentBackup(
                 source, backupPath, &errorMessage),
             qPrintable(errorMessage));
    QVERIFY(!darkeye::DatabaseBackupService::createConsistentBackup(
        source, backupPath, &errorMessage));
    QVERIFY(errorMessage.contains(QStringLiteral("拒绝覆盖")));
}

void DatabaseBackupServiceTest::restoresBackupIntoOpenDatabase()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    const QString databasePath = root.filePath(QStringLiteral("source.db"));
    darkeye::SqliteConnection source;
    QString errorMessage;
    QVERIFY2(source.open(databasePath, false, &errorMessage), qPrintable(errorMessage));
    QSqlQuery query(source.database());
    QVERIFY(query.exec(QStringLiteral("CREATE TABLE sample(value TEXT)")));
    QVERIFY(query.exec(QStringLiteral("INSERT INTO sample VALUES('before')")));

    const auto backup = darkeye::DatabaseMaintenanceService::createBackup(
        source.database(), root.filePath(QStringLiteral("backups")), QStringLiteral("test"));
    QVERIFY2(backup.succeeded, qPrintable(backup.message));
    QVERIFY(query.exec(QStringLiteral("UPDATE sample SET value='after'")));

    const auto restored = darkeye::DatabaseMaintenanceService::restoreBackup(
        source.database(), backup.outputPath);
    QVERIFY2(restored.succeeded, qPrintable(restored.message));
    QVERIFY(query.exec(QStringLiteral("SELECT value FROM sample")));
    QVERIFY(query.next());
    QCOMPARE(query.value(0).toString(), QStringLiteral("before"));
}

void DatabaseBackupServiceTest::createsPythonCompatibleSnapshot()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    darkeye::SqliteConnection source;
    QString error;
    QVERIFY2(source.open(root.filePath(QStringLiteral("source.db")), false, &error), qPrintable(error));
    QSqlQuery query(source.database());
    QVERIFY(query.exec(QStringLiteral("CREATE TABLE sample(value TEXT)")));
    QVERIFY(QDir().mkpath(root.filePath(QStringLiteral("covers/nested"))));
    QFile cover(root.filePath(QStringLiteral("covers/nested/cover.jpg")));
    QVERIFY(cover.open(QIODevice::WriteOnly));
    QCOMPARE(cover.write("image"), qint64(5));
    cover.close();
    QCoreApplication::setApplicationVersion(QStringLiteral("test-version"));
    const auto snapshot = darkeye::DatabaseMaintenanceService::createPublicSnapshot(
        source.database(), root.filePath(QStringLiteral("snapshots")),
        root.filePath(QStringLiteral("covers")), root.filePath(QStringLiteral("fanart")),
        root.filePath(QStringLiteral("actresses")), root.filePath(QStringLiteral("actors")));
    QVERIFY2(snapshot.succeeded, qPrintable(snapshot.message));
    QFile meta(QDir(snapshot.outputPath).filePath(QStringLiteral("meta.json")));
    QVERIFY(meta.open(QIODevice::ReadOnly));
    const QJsonObject metadata = QJsonDocument::fromJson(meta.readAll()).object();
    QCOMPARE(metadata.value(QStringLiteral("version")).toInt(), 1);
    QVERIFY(!metadata.contains(QStringLiteral("database")));
    QVERIFY(!metadata.value(QStringLiteral("created_at")).toString().isEmpty());
    QCOMPARE(metadata.value(QStringLiteral("app_version")).toString(), QStringLiteral("test-version"));
    const QJsonObject db = metadata.value(QStringLiteral("db")).toObject();
    QCOMPARE(db.value(QStringLiteral("type")).toString(), QStringLiteral("sqlite"));
    const QFileInfo dbFile(QDir(snapshot.outputPath).filePath(db.value(QStringLiteral("file")).toString()));
    QVERIFY(dbFile.isFile());
    QCOMPARE(db.value(QStringLiteral("size")).toInteger(), dbFile.size());
    const QJsonArray resources = metadata.value(QStringLiteral("resources")).toArray();
    const QStringList names = {QStringLiteral("actorimages"), QStringLiteral("actressimages"),
                               QStringLiteral("workcovers"), QStringLiteral("fanart")};
    QCOMPARE(resources.size(), names.size());
    for (int i = 0; i < names.size(); ++i)
    {
        const QJsonObject resource = resources.at(i).toObject();
        QCOMPARE(resource.value(QStringLiteral("name")).toString(), names.at(i));
        QCOMPARE(resource.value(QStringLiteral("path")).toString(), names.at(i));
        QCOMPARE(resource.value(QStringLiteral("file_count")).toInteger(), qint64(i == 2 ? 1 : 0));
        QCOMPARE(resource.value(QStringLiteral("total_size")).toInteger(), qint64(i == 2 ? 5 : 0));
    }
}

void DatabaseBackupServiceTest::restoresVersionOneSnapshot_data()
{
    QTest::addColumn<QByteArray>("metadataJson");
    QTest::newRow("python") << QByteArray(R"({"version":1,"db":{"file":"snapshot.db","type":"sqlite","size":0},"resources":[{"name":"workcovers","path":"workcovers","file_count":1,"total_size":5}]})");
    QTest::newRow("legacy-cpp") << QByteArray(R"({"version":1,"database":"snapshot.db","resources":["workcovers","fanart","actressimages","actorimages"]})");
}

void DatabaseBackupServiceTest::restoresVersionOneSnapshot()
{
    QFETCH(QByteArray, metadataJson);
    QTemporaryDir root;
    QVERIFY(root.isValid());
    darkeye::SqliteConnection source;
    QString error;
    QVERIFY2(source.open(root.filePath(QStringLiteral("source.db")), false, &error), qPrintable(error));
    QSqlQuery query(source.database());
    QVERIFY(query.exec(QStringLiteral("CREATE TABLE sample(value TEXT)")));
    QVERIFY(query.exec(QStringLiteral("INSERT INTO sample VALUES('snapshot')")));
    QVERIFY2(darkeye::DatabaseBackupService::createConsistentBackup(
                 source, root.filePath(QStringLiteral("snapshot.db")), &error), qPrintable(error));
    QVERIFY(query.exec(QStringLiteral("UPDATE sample SET value='changed'")));
    QFile meta(root.filePath(QStringLiteral("meta.json")));
    QVERIFY(meta.open(QIODevice::WriteOnly));
    QCOMPARE(meta.write(metadataJson), qint64(metadataJson.size()));
    meta.close();
    const QStringList names = {QStringLiteral("workcovers"), QStringLiteral("fanart"),
                               QStringLiteral("actressimages"), QStringLiteral("actorimages")};
    for (const QString &name : names)
    {
        QVERIFY(QDir().mkpath(root.filePath(name)));
        QFile image(root.filePath(name + QStringLiteral("/image.jpg")));
        QVERIFY(image.open(QIODevice::WriteOnly));
        QCOMPARE(image.write("image"), qint64(5));
    }
    const auto restored = darkeye::DatabaseMaintenanceService::restorePublicSnapshot(
        source.database(), meta.fileName(), root.filePath(QStringLiteral("restored/workcovers")),
        root.filePath(QStringLiteral("restored/fanart")), root.filePath(QStringLiteral("restored/actressimages")),
        root.filePath(QStringLiteral("restored/actorimages")));
    QVERIFY2(restored.succeeded, qPrintable(restored.message));
    QVERIFY(query.exec(QStringLiteral("SELECT value FROM sample")));
    QVERIFY(query.next());
    QCOMPARE(query.value(0).toString(), QStringLiteral("snapshot"));
    for (const QString &name : names)
    {
        QFile image(root.filePath(QStringLiteral("restored/") + name + QStringLiteral("/image.jpg")));
        QVERIFY(image.open(QIODevice::ReadOnly));
        QCOMPARE(image.readAll(), QByteArray("image"));
    }
}

void DatabaseBackupServiceTest::rejectsForeignKeyCorruptionWithoutChangingData()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    darkeye::SqliteConnection source;
    QVERIFY(source.open(root.filePath(QStringLiteral("source.db")), false));
    QSqlQuery query(source.database());
    QVERIFY(query.exec(QStringLiteral("CREATE TABLE parent(id INTEGER PRIMARY KEY)")));
    QVERIFY(query.exec(QStringLiteral("CREATE TABLE child(parent_id INTEGER REFERENCES parent(id))")));
    QVERIFY(query.exec(QStringLiteral("INSERT INTO parent VALUES(1)")));
    QVERIFY(query.exec(QStringLiteral("INSERT INTO child VALUES(1)")));
    const QString backupPath = root.filePath(QStringLiteral("corrupt.db"));
    QVERIFY(darkeye::DatabaseBackupService::createConsistentBackup(source, backupPath));
    {
        darkeye::SqliteConnection backup;
        QVERIFY(backup.open(backupPath, false));
        QSqlQuery corrupt(backup.database());
        QVERIFY(corrupt.exec(QStringLiteral("PRAGMA foreign_keys=OFF")));
        QVERIFY(corrupt.exec(QStringLiteral("UPDATE child SET parent_id=999")));
    }
    const auto result = darkeye::DatabaseMaintenanceService::restoreBackup(source.database(), backupPath);
    QVERIFY2(!result.succeeded, "Restore must reject orphaned foreign keys");
    QVERIFY(query.exec(QStringLiteral("SELECT parent_id FROM child")));
    QVERIFY(query.next());
    QCOMPARE(query.value(0).toInt(), 1);
    query.finish();
    QVERIFY(query.exec(QStringLiteral("PRAGMA foreign_key_check")));
    QVERIFY(!query.next());
    QVERIFY(!query.exec(QStringLiteral("INSERT INTO child VALUES(999)")));
}

void DatabaseBackupServiceTest::restoresFailedCopyAtomically()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    darkeye::SqliteConnection source;
    QVERIFY(source.open(root.filePath(QStringLiteral("source.db")), false));
    QSqlQuery query(source.database());
    QVERIFY(query.exec(QStringLiteral("CREATE TABLE first(value TEXT)")));
    QVERIFY(query.exec(QStringLiteral("CREATE TABLE second(value TEXT NOT NULL)")));
    QVERIFY(query.exec(QStringLiteral("INSERT INTO first VALUES('backup')")));
    QVERIFY(query.exec(QStringLiteral("INSERT INTO second VALUES('backup')")));
    const QString backupPath = root.filePath(QStringLiteral("incompatible.db"));
    QVERIFY(darkeye::DatabaseBackupService::createConsistentBackup(source, backupPath));
    {
        darkeye::SqliteConnection backup;
        QVERIFY(backup.open(backupPath, false));
        QSqlQuery change(backup.database());
        QVERIFY(change.exec(QStringLiteral("ALTER TABLE second ADD COLUMN extra TEXT")));
    }
    QVERIFY(query.exec(QStringLiteral("UPDATE first SET value='current'")));
    const auto result = darkeye::DatabaseMaintenanceService::restoreBackup(source.database(), backupPath);
    QVERIFY(!result.succeeded);
    QVERIFY(query.exec(QStringLiteral("SELECT value FROM first")));
    QVERIFY(query.next());
    QCOMPARE(query.value(0).toString(), QStringLiteral("current"));
    query.finish();
    QVERIFY(query.exec(QStringLiteral("PRAGMA foreign_keys")));
    QVERIFY(query.next());
    QCOMPARE(query.value(0).toInt(), 1);
    query.finish();
    QVERIFY(source.database().transaction());
    QVERIFY(source.database().rollback());
    QVERIFY(query.exec(QStringLiteral("SELECT COUNT(*) FROM pragma_database_list WHERE name='restore_source'")));
    QVERIFY(query.next());
    QCOMPARE(query.value(0).toInt(), 0);
}

void DatabaseBackupServiceTest::refusesRestoreInsideTransaction()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    darkeye::SqliteConnection source;
    QVERIFY(source.open(root.filePath(QStringLiteral("source.db")), false));
    QSqlQuery query(source.database());
    QVERIFY(query.exec(QStringLiteral("CREATE TABLE item(value TEXT)")));
    QVERIFY(query.exec(QStringLiteral("INSERT INTO item VALUES('backup')")));
    const QString backupPath = root.filePath(QStringLiteral("backup.db"));
    QVERIFY(darkeye::DatabaseBackupService::createConsistentBackup(source, backupPath));
    QVERIFY(source.database().transaction());
    QVERIFY(query.exec(QStringLiteral("UPDATE item SET value='pending'")));
    QVERIFY(!darkeye::DatabaseMaintenanceService::restoreBackup(source.database(), backupPath).succeeded);
    QVERIFY(query.exec(QStringLiteral("SELECT value FROM item")));
    QVERIFY(query.next());
    QCOMPARE(query.value(0).toString(), QStringLiteral("pending"));
    query.finish();
    QVERIFY(source.database().rollback());
    QVERIFY(query.exec(QStringLiteral("SELECT COUNT(*) FROM pragma_database_list WHERE name='restore_source'")));
    QVERIFY(query.next());
    QCOMPARE(query.value(0).toInt(), 0);
}

void DatabaseBackupServiceTest::restoresCompleteDatabase_data()
{
    QTest::addColumn<bool>("isPublic");
    QTest::newRow("public") << true;
    QTest::newRow("private") << false;
}

void DatabaseBackupServiceTest::restoresCompleteDatabase()
{
    QFETCH(bool, isPublic);
    QTemporaryDir root;
    QVERIFY(root.isValid());
    darkeye::SqliteConnection connection;
    QString error;
    QVERIFY(connection.open(root.filePath(QStringLiteral("source.db")), false, &error));
    const auto kind = isPublic ? darkeye::DatabaseKind::Public : darkeye::DatabaseKind::Private;
    QVERIFY2(darkeye::SchemaManager::initializeEmptyDatabase(connection, kind, &error), qPrintable(error));
    QSqlQuery query(connection.database());
    const QString table = isPublic ? QStringLiteral("work") : QStringLiteral("favorite_work");
    QVERIFY(query.exec(isPublic ? QStringLiteral("INSERT INTO work(serial_number) VALUES('RESTORE-001')")
                               : QStringLiteral("INSERT INTO favorite_work(work_id,serial_number) VALUES(42,'RESTORE-001')")));
    const auto backup = darkeye::DatabaseMaintenanceService::createBackup(
        connection.database(), root.filePath(QStringLiteral("backups")), QStringLiteral("complete"));
    QVERIFY2(backup.succeeded, qPrintable(backup.message));
    QVERIFY(query.exec(QStringLiteral("DELETE FROM %1").arg(table)));
    const auto restored = darkeye::DatabaseMaintenanceService::restoreBackup(connection.database(), backup.outputPath);
    QVERIFY2(restored.succeeded, qPrintable(restored.message));
    QVERIFY2(darkeye::SchemaManager::validateCurrentSchema(connection, kind, &error), qPrintable(error));
    QVERIFY(query.exec(QStringLiteral("SELECT serial_number FROM %1").arg(table)));
    QVERIFY(query.next());
    QCOMPARE(query.value(0).toString(), QStringLiteral("RESTORE-001"));
    QVERIFY(!query.next());
    QVERIFY(query.exec(isPublic ? QStringLiteral("INSERT INTO work(serial_number) VALUES('RESTORE-002')")
                               : QStringLiteral("INSERT INTO favorite_work(work_id,serial_number) VALUES(43,'RESTORE-002')")));
}

QTEST_MAIN(DatabaseBackupServiceTest)
#include "DatabaseBackupServiceTest.moc"
