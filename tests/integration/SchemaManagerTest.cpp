#include "settings/Paths.h"
#include "database/DatabaseManager.h"
#include "database/SchemaManager.h"
#include "database/SqlScriptRunner.h"

#include <QDir>
#include <QCryptographicHash>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSqlQuery>
#include <QSqlRecord>
#include <QTemporaryDir>
#include <QtTest>

class SchemaManagerTest final : public QObject
{
    Q_OBJECT

private slots:
    void scriptSplitterKeepsTriggerBodyTogether();
    void initializesAndValidatesPublicDatabase();
    void acceptsPublicUserVersionWithoutLegacyTable();
    void initializesAndValidatesPrivateDatabase();
    void migratesPublicV1DataToV2();
    void migratesPrivateV1DataToV11();
    void rejectsUnknownNonEmptySchema();
    void migratesFrozenLegacyDatabase_data();
    void migratesFrozenLegacyDatabase();
    void managerBacksUpBeforeMigratingLegacyDatabase_data();
    void managerBacksUpBeforeMigratingLegacyDatabase();
    void rejectsForeignKeyCorruption();
};

void SchemaManagerTest::scriptSplitterKeepsTriggerBodyTogether()
{
    const QString script = QStringLiteral(
        "CREATE TABLE item(id INTEGER, updated TEXT);"
        "CREATE TRIGGER touch_item AFTER UPDATE ON item BEGIN "
        "UPDATE item SET updated='a;b' WHERE id=OLD.id; END;"
        "PRAGMA user_version=2;");
    const QStringList statements = darkeye::SqlScriptRunner::splitStatements(script);
    QCOMPARE(statements.size(), 3);
    QVERIFY(statements.at(1).contains(QStringLiteral("UPDATE item")));
    QVERIFY(statements.at(1).endsWith(QStringLiteral("END")));
}

void SchemaManagerTest::initializesAndValidatesPublicDatabase()
{
    QTemporaryDir temporaryDirectory;
    QVERIFY(temporaryDirectory.isValid());
    const QString path = QDir(temporaryDirectory.path()).filePath(QStringLiteral("public.db"));

    darkeye::SqliteConnection connection;
    QString errorMessage;
    QVERIFY2(connection.open(path, false, &errorMessage), qPrintable(errorMessage));
    QVERIFY2(darkeye::SchemaManager::initializeEmptyDatabase(
                 connection, darkeye::DatabaseKind::Public, &errorMessage),
             qPrintable(errorMessage));
    QCOMPARE(connection.schemaVersion(), QStringLiteral("2"));
    QVERIFY(connection.database().tables(QSql::Tables).contains(QStringLiteral("work")));
    QVERIFY(connection.database().tables(QSql::Views).contains(QStringLiteral("v_work_all_info")));

    QSqlQuery contentCountQuery(connection.database());
    QVERIFY(contentCountQuery.exec(QStringLiteral("SELECT COUNT(*) FROM tag")));
    QVERIFY(contentCountQuery.next());
    QVERIFY(contentCountQuery.value(0).toInt() > 0);
    QVERIFY(contentCountQuery.exec(QStringLiteral("SELECT COUNT(*) FROM maker")));
    QVERIFY(contentCountQuery.next());
    QVERIFY(contentCountQuery.value(0).toInt() > 0);
}

void SchemaManagerTest::acceptsPublicUserVersionWithoutLegacyTable()
{
    QTemporaryDir temporaryDirectory;
    QVERIFY(temporaryDirectory.isValid());
    const QString path = QDir(temporaryDirectory.path()).filePath(QStringLiteral("public.db"));

    darkeye::SqliteConnection connection;
    QString errorMessage;
    QVERIFY2(connection.open(path, false, &errorMessage), qPrintable(errorMessage));
    QVERIFY2(darkeye::SchemaManager::initializeEmptyDatabase(
                 connection, darkeye::DatabaseKind::Public, &errorMessage),
             qPrintable(errorMessage));
    QSqlQuery query(connection.database());
    QVERIFY(query.exec(QStringLiteral("DROP TABLE db_version")));
    QCOMPARE(connection.schemaVersion(), QStringLiteral("2"));
    QVERIFY2(darkeye::SchemaManager::validateCurrentSchema(
                 connection, darkeye::DatabaseKind::Public, &errorMessage),
             qPrintable(errorMessage));
}

void SchemaManagerTest::initializesAndValidatesPrivateDatabase()
{
    QTemporaryDir temporaryDirectory;
    QVERIFY(temporaryDirectory.isValid());
    const QString path = QDir(temporaryDirectory.path()).filePath(QStringLiteral("private.db"));

    darkeye::SqliteConnection connection;
    QString errorMessage;
    QVERIFY2(connection.open(path, false, &errorMessage), qPrintable(errorMessage));
    QVERIFY2(darkeye::SchemaManager::migrateToCurrent(
                 connection, darkeye::DatabaseKind::Private, &errorMessage),
             qPrintable(errorMessage));
    QCOMPARE(connection.schemaVersion(), QStringLiteral("1.1"));
    QVERIFY(connection.database().tables(QSql::Tables).contains(QStringLiteral("masturbation")));
}

void SchemaManagerTest::migratesPublicV1DataToV2()
{
    QTemporaryDir temporaryDirectory;
    QVERIFY(temporaryDirectory.isValid());
    const QString path = QDir(temporaryDirectory.path()).filePath(QStringLiteral("public-v1.db"));

    darkeye::SqliteConnection connection;
    QString errorMessage;
    QVERIFY2(connection.open(path, false, &errorMessage), qPrintable(errorMessage));
    QVERIFY2(darkeye::SqlScriptRunner::executeResource(
                 connection.database(), QStringLiteral(":/sql/fixtures/initPublicTable-v1.0.sql"),
                 &errorMessage),
             qPrintable(errorMessage));
    QCOMPARE(connection.schemaVersion(), QStringLiteral("1.0"));

    QSqlQuery insertQuery(connection.database());
    QVERIFY(insertQuery.exec(QStringLiteral(
        "INSERT INTO work(work_id, serial_number, story, cn_title, create_time, update_time) "
        "VALUES(42, 'LEGACY-1', 'legacy notes', '旧标题', '2025-01-02', '2025-03-04')")));
    QVERIFY(insertQuery.exec(QStringLiteral("INSERT INTO actress(actress_id) VALUES(8)")));
    QVERIFY(insertQuery.exec(QStringLiteral(
        "INSERT INTO work_actress_relation(work_id, actress_id) VALUES(42, 8)")));

    QVERIFY2(darkeye::SchemaManager::migrateToCurrent(
                 connection, darkeye::DatabaseKind::Public, &errorMessage),
             qPrintable(errorMessage));
    QCOMPARE(connection.schemaVersion(), QStringLiteral("2"));

    QSqlQuery migratedQuery(connection.database());
    QVERIFY(migratedQuery.exec(QStringLiteral(
        "SELECT notes, runtime, maker_id, label_id, series_id FROM work "
        "WHERE serial_number='LEGACY-1'")));
    QVERIFY(migratedQuery.next());
    QCOMPARE(migratedQuery.value(0).toString(), QStringLiteral("legacy notes"));
    QVERIFY(migratedQuery.value(1).isNull());
    QVERIFY(migratedQuery.value(2).isNull());
    QVERIFY(migratedQuery.value(3).isNull());
    QVERIFY(migratedQuery.value(4).isNull());
    QVERIFY(migratedQuery.exec(QStringLiteral(
        "SELECT work_id, cn_title, create_time, update_time FROM work WHERE work_id=42")));
    QVERIFY(migratedQuery.next());
    QCOMPARE(migratedQuery.value(1).toString(), QStringLiteral("旧标题"));
    QCOMPARE(migratedQuery.value(2).toString(), QStringLiteral("2025-01-02"));
    QCOMPARE(migratedQuery.value(3).toString(), QStringLiteral("2025-03-04"));
    QVERIFY(migratedQuery.exec(QStringLiteral(
        "SELECT actress_id FROM work_actress_relation WHERE work_id=42")));
    QVERIFY(migratedQuery.next());
    QCOMPARE(migratedQuery.value(0).toInt(), 8);

    QVERIFY(migratedQuery.exec(QStringLiteral(
        "SELECT COUNT(*) FROM label WHERE cn_name='S1 NO.1 STYLE'")));
    QVERIFY(migratedQuery.next());
    QVERIFY(migratedQuery.value(0).toInt() > 0);
    QVERIFY(migratedQuery.exec(QStringLiteral(
        "SELECT COUNT(*) FROM prefix_maker_relation p "
        "JOIN maker m ON m.maker_id=p.maker_id "
        "WHERE p.prefix='SONE' AND m.cn_name='S1 NO.1 STYLE'")));
    QVERIFY(migratedQuery.next());
    QCOMPARE(migratedQuery.value(0).toInt(), 1);
    QFile makerConfig(QDir(darkeye::settings::Paths().configDirectory())
                         .filePath(QStringLiteral("maker_prefix.json")));
    QVERIFY(makerConfig.open(QIODevice::ReadOnly));
    const QJsonDocument makers = QJsonDocument::fromJson(makerConfig.readAll());
    QVERIFY(makers.isArray());
    for (const QJsonValue &entry : makers.array()) {
        const QJsonObject maker = entry.toObject();
        for (const QJsonValue &prefix : maker.value(QStringLiteral("prefixes")).toArray()) {
            migratedQuery.prepare(QStringLiteral(
                "SELECT COUNT(*) FROM prefix_maker_relation p "
                "JOIN maker m ON m.maker_id=p.maker_id "
                "WHERE p.prefix=? AND m.cn_name=? AND m.jp_name=?"));
            migratedQuery.addBindValue(prefix.toString().trimmed());
            migratedQuery.addBindValue(maker.value(QStringLiteral("cn_name")).toString());
            migratedQuery.addBindValue(maker.value(QStringLiteral("jp_name")).toString());
            QVERIFY(migratedQuery.exec());
            QVERIFY(migratedQuery.next());
            QCOMPARE(migratedQuery.value(0).toInt(), 1);
        }
    }
    QVERIFY(migratedQuery.exec(QStringLiteral(
        "SELECT COUNT(*) FROM series WHERE cn_name='出張先相部屋NTR'")));
    QVERIFY(migratedQuery.next());
    QVERIFY(migratedQuery.value(0).toInt() > 0);
    QVERIFY2(darkeye::SchemaManager::migrateToCurrent(
                 connection, darkeye::DatabaseKind::Public, &errorMessage),
             qPrintable(errorMessage));
}

void SchemaManagerTest::migratesPrivateV1DataToV11()
{
    QTemporaryDir temporaryDirectory;
    QVERIFY(temporaryDirectory.isValid());
    const QString path = QDir(temporaryDirectory.path()).filePath(QStringLiteral("private-v1.db"));

    darkeye::SqliteConnection connection;
    QString errorMessage;
    QVERIFY2(connection.open(path, false, &errorMessage), qPrintable(errorMessage));
    QVERIFY2(darkeye::SqlScriptRunner::executeResource(
                 connection.database(), QStringLiteral(":/sql/fixtures/initPrivateTable-v1.0.sql"),
                 &errorMessage),
             qPrintable(errorMessage));
    QCOMPARE(connection.schemaVersion(), QStringLiteral("1.0"));

    QSqlQuery insertQuery(connection.database());
    QVERIFY(insertQuery.exec(QStringLiteral(
        "INSERT INTO favorite_actress(actress_id, jp_name) VALUES(8, 'legacy')")));
    QVERIFY(insertQuery.exec(QStringLiteral(
        "INSERT INTO favorite_work(favorite_work_id, work_id, serial_number, added_time) "
        "VALUES(7, 42, 'LEGACY-1', '2025-01-02')")));
    QVERIFY(insertQuery.exec(QStringLiteral(
        "INSERT INTO masturbation(work_id, serial_number, start_time, rating, comment) "
        "VALUES(42, 'LEGACY-1', '2025-02-03 12:00:00', 4, '保留记录')")));
    QVERIFY(insertQuery.exec(QStringLiteral(
        "INSERT INTO love_making(event_time, rating, comment) "
        "VALUES('2025-02-04', 5, '保留评价')")));
    QVERIFY(insertQuery.exec(QStringLiteral(
        "INSERT INTO sexual_arousal(arousal_time, comment) VALUES('2025-02-05', '保留备注')")));
    const QStringList tables{QStringLiteral("favorite_actress"), QStringLiteral("favorite_work"),
                             QStringLiteral("masturbation"), QStringLiteral("love_making"),
                             QStringLiteral("sexual_arousal")};
    QMap<QString, QList<QVariantList>> originalRows;
    for (const QString &table : tables) {
        QVERIFY(insertQuery.exec(QStringLiteral("SELECT * FROM %1 ORDER BY 1").arg(table)));
        while (insertQuery.next()) {
            QVariantList row;
            for (int column = 0; column < insertQuery.record().count(); ++column)
                row.append(insertQuery.value(column));
            originalRows[table].append(row);
        }
        QCOMPARE(originalRows[table].size(), 1);
    }

    QVERIFY2(darkeye::SchemaManager::migrateToCurrent(
                 connection, darkeye::DatabaseKind::Private, &errorMessage),
             qPrintable(errorMessage));
    QCOMPARE(connection.schemaVersion(), QStringLiteral("1.1"));
    for (const QString &table : tables) {
        QVERIFY(insertQuery.exec(QStringLiteral("SELECT * FROM %1 ORDER BY 1").arg(table)));
        QList<QVariantList> migratedRows;
        while (insertQuery.next()) {
            QVariantList row;
            for (int column = 0; column < insertQuery.record().count(); ++column)
                row.append(insertQuery.value(column));
            migratedRows.append(row);
        }
        QCOMPARE(migratedRows, originalRows[table]);
    }
    QVERIFY2(darkeye::SchemaManager::migrateToCurrent(
                 connection, darkeye::DatabaseKind::Private, &errorMessage),
             qPrintable(errorMessage));

    QVERIFY(insertQuery.exec(QStringLiteral(
        "INSERT INTO favorite_actress(actress_id, jp_name) VALUES(8, 'duplicate allowed')")));
    QVERIFY(insertQuery.exec(QStringLiteral(
        "SELECT COUNT(*) FROM favorite_actress WHERE actress_id=8")));
    QVERIFY(insertQuery.next());
    QCOMPARE(insertQuery.value(0).toInt(), 2);
    QVERIFY(insertQuery.exec(QStringLiteral(
        "INSERT INTO favorite_work(work_id, serial_number) VALUES(42, 'LEGACY-2')")));
    QVERIFY(!insertQuery.exec(QStringLiteral(
        "INSERT INTO favorite_work(work_id, serial_number) VALUES(43, 'LEGACY-1')")));
}

void SchemaManagerTest::migratesFrozenLegacyDatabase_data()
{
    QTest::addColumn<int>("kind");
    QTest::addColumn<QString>("fixtureName");
    QTest::newRow("public-v1") << static_cast<int>(darkeye::DatabaseKind::Public)
                               << QStringLiteral("public-legacy-v1.0.db");
    QTest::newRow("private-v1") << static_cast<int>(darkeye::DatabaseKind::Private)
                                << QStringLiteral("private-legacy-v1.0.db");
}

void SchemaManagerTest::migratesFrozenLegacyDatabase()
{
    QFETCH(int, kind);
    QFETCH(QString, fixtureName);
    const auto databaseKind = static_cast<darkeye::DatabaseKind>(kind);
    const QString fixturePath = QDir(QStringLiteral(DARKEYE_TEST_DATABASE_DIR)).filePath(fixtureName);
    QFile original(fixturePath);
    QVERIFY(original.open(QIODevice::ReadOnly));
    const QByteArray before = QCryptographicHash::hash(original.readAll(), QCryptographicHash::Sha256);
    original.close();
    QTemporaryDir temporaryDirectory;
    QVERIFY(temporaryDirectory.isValid());
    const QString path = temporaryDirectory.filePath(fixtureName);
    QVERIFY(QFile::copy(fixturePath, path));
    darkeye::SqliteConnection connection;
    QString errorMessage;
    QVERIFY2(connection.open(path, false, &errorMessage), qPrintable(errorMessage));
    QCOMPARE(connection.schemaVersion(), QStringLiteral("1.0"));
    QVERIFY2(darkeye::SchemaManager::migrateToCurrent(connection, databaseKind, &errorMessage),
             qPrintable(errorMessage));
    QCOMPARE(connection.schemaVersion(), darkeye::SchemaManager::requiredVersion(databaseKind));
    QVERIFY2(darkeye::SchemaManager::migrateToCurrent(connection, databaseKind, &errorMessage),
             qPrintable(errorMessage));
    QVERIFY(original.open(QIODevice::ReadOnly));
    QCOMPARE(QCryptographicHash::hash(original.readAll(), QCryptographicHash::Sha256), before);
}

void SchemaManagerTest::managerBacksUpBeforeMigratingLegacyDatabase_data()
{
    QTest::addColumn<bool>("privateDatabase");
    QTest::newRow("public") << false;
    QTest::newRow("private") << true;
}

void SchemaManagerTest::rejectsUnknownNonEmptySchema()
{
    QTemporaryDir temporaryDirectory;
    QVERIFY(temporaryDirectory.isValid());
    const QString path = QDir(temporaryDirectory.path()).filePath(QStringLiteral("unknown.db"));

    darkeye::SqliteConnection connection;
    QString errorMessage;
    QVERIFY2(connection.open(path, false, &errorMessage), qPrintable(errorMessage));
    QSqlQuery query(connection.database());
    QVERIFY(query.exec(QStringLiteral("CREATE TABLE unexpected(id INTEGER)")));

    QVERIFY(!darkeye::SchemaManager::migrateToCurrent(
        connection, darkeye::DatabaseKind::Public, &errorMessage));
    QVERIFY(errorMessage.contains(QStringLiteral("不支持")));
}

void SchemaManagerTest::managerBacksUpBeforeMigratingLegacyDatabase()
{
    QFETCH(bool, privateDatabase);
    QTemporaryDir temporaryDirectory;
    QVERIFY(temporaryDirectory.isValid());
    const QString applicationDirectory = temporaryDirectory.path();
    const darkeye::settings::Paths paths(applicationDirectory, QDir(applicationDirectory).filePath(QStringLiteral("data")));
    QVERIFY(paths.ensureRuntimeDirectories());

    {
        darkeye::SqliteConnection legacyConnection;
        QString errorMessage;
        const QString databasePath = privateDatabase ? paths.privateDatabase()
                                                     : paths.publicDatabase();
        QVERIFY2(legacyConnection.open(databasePath, false, &errorMessage),
                 qPrintable(errorMessage));
        QVERIFY2(darkeye::SqlScriptRunner::executeResource(
                     legacyConnection.database(),
                     privateDatabase ? QStringLiteral(":/sql/fixtures/initPrivateTable-v1.0.sql")
                                     : QStringLiteral(":/sql/fixtures/initPublicTable-v1.0.sql"),
                     &errorMessage),
                 qPrintable(errorMessage));
        QSqlQuery insertQuery(legacyConnection.database());
        QVERIFY(insertQuery.exec(privateDatabase ? QStringLiteral(
            "INSERT INTO favorite_work(work_id, serial_number) VALUES(42, 'BACKUP-1')")
            : QStringLiteral("INSERT INTO work(serial_number, story) VALUES('BACKUP-1', 'preserve me')")));
    }

    darkeye::DatabaseManager manager;
    QString errorMessage;
    QVERIFY2(manager.initialize(paths, &errorMessage), qPrintable(errorMessage));
    QCOMPARE(manager.publicConnection().schemaVersion(), QStringLiteral("2"));
    QCOMPARE(manager.privateConnection().schemaVersion(), QStringLiteral("1.1"));
    const QString backupDirectory = privateDatabase ? paths.privateBackupDirectory()
                                                    : paths.publicBackupDirectory();
    const QString pattern = privateDatabase ? QStringLiteral("private-pre-migration-*.db")
                                           : QStringLiteral("public-pre-migration-*.db");

    const QStringList backups = QDir(backupDirectory).entryList({pattern}, QDir::Files);
    QCOMPARE(backups.size(), 1);

    darkeye::SqliteConnection backupConnection;
    QVERIFY2(backupConnection.open(
                 QDir(backupDirectory).filePath(backups.constFirst()), true,
                 &errorMessage),
             qPrintable(errorMessage));
    QCOMPARE(backupConnection.schemaVersion(), QStringLiteral("1.0"));
    QSqlQuery backupQuery(backupConnection.database());
    QVERIFY(backupQuery.exec(privateDatabase ? QStringLiteral(
        "SELECT serial_number FROM favorite_work WHERE work_id=42")
        : QStringLiteral("SELECT story FROM work WHERE serial_number='BACKUP-1'")));
    QVERIFY(backupQuery.next());
    QCOMPARE(backupQuery.value(0).toString(), privateDatabase ? QStringLiteral("BACKUP-1")
                                                           : QStringLiteral("preserve me"));
    QVERIFY2(manager.initialize(paths, &errorMessage), qPrintable(errorMessage));
    QCOMPARE(QDir(backupDirectory).entryList({pattern}, QDir::Files), backups);
}

void SchemaManagerTest::rejectsForeignKeyCorruption()
{
    QTemporaryDir temporaryDirectory;
    QVERIFY(temporaryDirectory.isValid());
    darkeye::SqliteConnection connection;
    QString errorMessage;
    QVERIFY2(connection.open(QDir(temporaryDirectory.path()).filePath("public.db"), false,
                             &errorMessage),
             qPrintable(errorMessage));
    QVERIFY2(darkeye::SchemaManager::initializeEmptyDatabase(
                 connection, darkeye::DatabaseKind::Public, &errorMessage),
             qPrintable(errorMessage));

    QSqlQuery query(connection.database());
    QVERIFY(query.exec(QStringLiteral("PRAGMA foreign_keys=OFF")));
    QVERIFY(query.exec(QStringLiteral(
        "INSERT INTO work_actress_relation(work_id, actress_id) VALUES(999, 999)")));
    QVERIFY(query.exec(QStringLiteral("PRAGMA foreign_keys=ON")));

    QVERIFY(!darkeye::SchemaManager::validateCurrentSchema(
        connection, darkeye::DatabaseKind::Public, &errorMessage));
    QVERIFY(errorMessage.contains(QStringLiteral("外键损坏")));
}

QTEST_MAIN(SchemaManagerTest)
#include "SchemaManagerTest.moc"
