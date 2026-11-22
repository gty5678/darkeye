#include "database/CsvExport.h"
#include "database/DatabaseMaintenanceService.h"
#include "database/SchemaManager.h"
#include "database/repositories/PersonRepository.h"
#include "database/repositories/PrivateRepository.h"
#include "database/repositories/WorkRepository.h"

#include <QFile>
#include <QSqlQuery>
#include <QStandardItemModel>
#include <QTemporaryDir>
#include <QtTest>
#include <memory>

class DatabaseOperationsTest final : public QObject
{
    Q_OBJECT
private slots:
    void init();
    void cleanup();
    void rebuildsLinksIdempotently();
    void failedRebuildRollsBackBothDatabases();
    void rebuildRefusesCallerTransaction();
    void rebuildRefusesMissingPrivateDatabase();
    void imageConsistencyCountsMissingAndExtraFiles();
    void backupAndVacuumPreservesBothDatabases();
    void csvEscapesUnicodeQuotesAndNewlines();
    void csvSqlPreservesNullsAndEmptyResultHeaders();
    void failedCsvQueryPreservesExistingOutput();
    void csvReadErrorDoesNotPublishPartialOutput();
    void deletesPeopleWithoutOrphaningRelations_data();
    void deletesPeopleWithoutOrphaningRelations();
private:
    std::unique_ptr<QTemporaryDir> m_root;
    darkeye::SqliteConnection m_public;
    darkeye::SqliteConnection m_private;
    QString m_error;
    QString privatePath() const { return m_root->filePath(QStringLiteral("private.db")); }
    int scalar(QSqlDatabase db, const QString &sql) const;
};

int DatabaseOperationsTest::scalar(QSqlDatabase db, const QString &sql) const
{
    QSqlQuery query(db);
    if (!query.exec(sql) || !query.next()) return -1;
    return query.value(0).toInt();
}

void DatabaseOperationsTest::init()
{
    m_root = std::make_unique<QTemporaryDir>();
    QVERIFY(m_root->isValid());
    m_error.clear();
    QVERIFY2(m_public.open(m_root->filePath(QStringLiteral("public.db")), false, &m_error), qPrintable(m_error));
    QVERIFY2(m_private.open(privatePath(), false, &m_error), qPrintable(m_error));
    QVERIFY2(darkeye::SchemaManager::initializeEmptyDatabase(m_public, darkeye::DatabaseKind::Public, &m_error), qPrintable(m_error));
    QVERIFY2(darkeye::SchemaManager::initializeEmptyDatabase(m_private, darkeye::DatabaseKind::Private, &m_error), qPrintable(m_error));
}

void DatabaseOperationsTest::cleanup()
{
    m_private.close();
    m_public.close();
    m_root.reset();
}

void DatabaseOperationsTest::rebuildsLinksIdempotently()
{
    darkeye::WorkRepository works(m_public.database());
    darkeye::PersonRepository people(m_public.database());
    darkeye::PrivateRepository privateData(m_private.database());
    const auto workId = works.insertSerial(QStringLiteral("LINK-001"), &m_error);
    const auto personId = people.create(darkeye::PersonKind::Actress, {}, QStringLiteral("既存名"), &m_error);
    QVERIFY(workId.has_value());
    QVERIFY(personId.has_value());
    QVERIFY(privateData.addFavoriteWork(9001, QStringLiteral("LINK-001"), &m_error));
    QVERIFY(privateData.addFavoriteWork(9002, QStringLiteral("LINK-002"), &m_error));
    QVERIFY(privateData.addFavoriteActress(9001, QStringLiteral("既存名"), &m_error));
    QVERIFY(privateData.addFavoriteActress(9002, QStringLiteral("新しい名前"), &m_error));
    QVERIFY(privateData.addMasturbationRecord(9002, QStringLiteral("LINK-002"), QStringLiteral("2024-01-01"), {}, 3, {}, &m_error));
    QVERIFY(privateData.addMasturbationRecord(0, {}, QStringLiteral("2024-01-01"), {}, 3, {}, &m_error));
    const auto result = darkeye::DatabaseMaintenanceService::rebuildPrivateLinks(m_public.database(), privatePath());
    QVERIFY2(result.succeeded, qPrintable(result.message));
    QCOMPARE(result.createdWorks, 1);
    QCOMPARE(result.createdActresses, 1);
    const auto newWorkId = works.findIdBySerial(QStringLiteral("LINK-002"), &m_error);
    const auto newPersonId = people.findByName(darkeye::PersonKind::Actress, QStringLiteral("新しい名前"), &m_error);
    QVERIFY(newWorkId.has_value());
    QVERIFY(newPersonId.has_value());
    QVERIFY(privateData.isFavoriteWork(*workId));
    QVERIFY(privateData.isFavoriteWork(*newWorkId));
    QVERIFY(privateData.isFavoriteActress(*personId));
    QVERIFY(privateData.isFavoriteActress(*newPersonId));
    QCOMPARE(privateData.masturbationWorkIds(), QList<qint64>{*newWorkId});
    QCOMPARE(scalar(m_private.database(), QStringLiteral("SELECT COUNT(*) FROM masturbation WHERE work_id IS NULL")), 1);
    QCOMPARE(scalar(m_public.database(), QStringLiteral("SELECT COUNT(*) FROM pragma_database_list WHERE name='private_db'")), 0);
    const auto repeated = darkeye::DatabaseMaintenanceService::rebuildPrivateLinks(m_public.database(), privatePath());
    QVERIFY2(repeated.succeeded, qPrintable(repeated.message));
    QCOMPARE(repeated.createdWorks, 0);
    QCOMPARE(repeated.createdActresses, 0);
}

void DatabaseOperationsTest::failedRebuildRollsBackBothDatabases()
{
    darkeye::PrivateRepository privateData(m_private.database());
    QVERIFY(privateData.addFavoriteActress(9001, QStringLiteral("新しい名前"), &m_error));
    QVERIFY(privateData.addFavoriteWork(9001, QStringLiteral("ROLLBACK-LINK"), &m_error));
    QSqlQuery trigger(m_private.database());
    QVERIFY(trigger.exec(QStringLiteral("CREATE TRIGGER reject_relink BEFORE UPDATE ON favorite_work "
                                       "BEGIN SELECT RAISE(ABORT, 'injected failure'); END")));
    const auto result = darkeye::DatabaseMaintenanceService::rebuildPrivateLinks(m_public.database(), privatePath());
    QVERIFY(!result.succeeded);
    QCOMPARE(scalar(m_public.database(), QStringLiteral("SELECT COUNT(*) FROM actress")), 0);
    QCOMPARE(scalar(m_public.database(), QStringLiteral("SELECT COUNT(*) FROM actress_name")), 0);
    QCOMPARE(scalar(m_public.database(), QStringLiteral("SELECT COUNT(*) FROM work")), 0);
    QVERIFY(privateData.isFavoriteActress(9001));
    QVERIFY(privateData.isFavoriteWork(9001));
    QCOMPARE(scalar(m_public.database(), QStringLiteral("SELECT COUNT(*) FROM pragma_database_list WHERE name='private_db'")), 0);
    QVERIFY(trigger.exec(QStringLiteral("DROP TRIGGER reject_relink")));
    const auto retry = darkeye::DatabaseMaintenanceService::rebuildPrivateLinks(m_public.database(), privatePath());
    QVERIFY2(retry.succeeded, qPrintable(retry.message));
}

void DatabaseOperationsTest::rebuildRefusesCallerTransaction()
{
    QVERIFY(m_public.database().transaction());
    QSqlQuery query(m_public.database());
    QVERIFY(query.exec(QStringLiteral("INSERT INTO work(serial_number) VALUES('PENDING')")));
    const auto result = darkeye::DatabaseMaintenanceService::rebuildPrivateLinks(m_public.database(), privatePath());
    QVERIFY(!result.succeeded);
    QCOMPARE(scalar(m_public.database(), QStringLiteral("SELECT COUNT(*) FROM work")), 1);
    QVERIFY(m_public.database().rollback());
    QCOMPARE(scalar(m_public.database(), QStringLiteral("SELECT COUNT(*) FROM work")), 0);
    QCOMPARE(scalar(m_public.database(), QStringLiteral("SELECT COUNT(*) FROM pragma_database_list WHERE name='private_db'")), 0);
}

void DatabaseOperationsTest::rebuildRefusesMissingPrivateDatabase()
{
    const QString missing = m_root->filePath(QStringLiteral("missing.db"));
    QVERIFY(!darkeye::DatabaseMaintenanceService::rebuildPrivateLinks(m_public.database(), missing).succeeded);
    QVERIFY(!QFileInfo::exists(missing));
}

void DatabaseOperationsTest::imageConsistencyCountsMissingAndExtraFiles()
{
    QSqlQuery query(m_public.database());
    QVERIFY(query.exec(QStringLiteral("INSERT INTO work(serial_number,image_url) VALUES"
        "('IMG-1','present.jpg'),('IMG-2','missing.jpg'),('IMG-3','missing.jpg'),('IMG-4','')")));
    const QString directory = m_root->filePath(QStringLiteral("images"));
    QVERIFY(QDir().mkpath(directory));
    for (const auto &name : {QStringLiteral("present.jpg"), QStringLiteral("extra.jpg")})
    {
        QFile file(QDir(directory).filePath(name));
        QVERIFY(file.open(QIODevice::WriteOnly));
        QCOMPARE(file.write("image"), 5);
    }
    const auto result = darkeye::DatabaseMaintenanceService::checkImageConsistency(m_public.database(), directory, QStringLiteral("work"), QStringLiteral("image_url"));
    QVERIFY2(result.succeeded, qPrintable(result.message));
    QCOMPARE(result.missingFiles, 1);
    QCOMPARE(result.extraFiles, 1);
    QVERIFY(!darkeye::DatabaseMaintenanceService::checkImageConsistency(m_public.database(), directory, QStringLiteral("missing_table"), QStringLiteral("image_url")).succeeded);
}

void DatabaseOperationsTest::backupAndVacuumPreservesBothDatabases()
{
    darkeye::WorkRepository works(m_public.database());
    darkeye::PrivateRepository privateData(m_private.database());
    QVERIFY(works.insertSerial(QStringLiteral("VACUUM-001"), &m_error).has_value());
    QVERIFY(privateData.addFavoriteWork(42, QStringLiteral("VACUUM-001"), &m_error));
    const QString publicBackups = m_root->filePath(QStringLiteral("public-backups"));
    const QString privateBackups = m_root->filePath(QStringLiteral("private-backups"));
    const auto result = darkeye::DatabaseMaintenanceService::backupAndVacuum(m_public.database(), m_private.database(), publicBackups, privateBackups);
    QVERIFY2(result.succeeded, qPrintable(result.message));
    QCOMPARE(QDir(publicBackups).entryList({QStringLiteral("*.db")}, QDir::Files).size(), 1);
    QCOMPARE(QDir(privateBackups).entryList({QStringLiteral("*.db")}, QDir::Files).size(), 1);
    QVERIFY(works.existsSerial(QStringLiteral("VACUUM-001")));
    QVERIFY(privateData.isFavoriteWork(42));
    QVERIFY(darkeye::SchemaManager::validateCurrentSchema(m_public, darkeye::DatabaseKind::Public, &m_error));
    QVERIFY(darkeye::SchemaManager::validateCurrentSchema(m_private, darkeye::DatabaseKind::Private, &m_error));
}

void DatabaseOperationsTest::csvEscapesUnicodeQuotesAndNewlines()
{
    QStandardItemModel model(1, 3);
    model.setHorizontalHeaderLabels({QStringLiteral("名称"), QStringLiteral("备注"), QStringLiteral("空值")});
    model.setData(model.index(0, 0), QStringLiteral("中文,日本語"));
    model.setData(model.index(0, 1), QStringLiteral("say \"hello\"\nnext"));
    const QString path = m_root->filePath(QStringLiteral("escaped.csv"));
    QVERIFY2(darkeye::exportModelToCsv(&model, path, &m_error), qPrintable(m_error));
    QFile file(path);
    QVERIFY(file.open(QIODevice::ReadOnly | QIODevice::Text));
    QCOMPARE(QString::fromUtf8(file.readAll()), QStringLiteral("名称,备注,空值\n\"中文,日本語\",\"say \"\"hello\"\"\nnext\",\n"));
}

void DatabaseOperationsTest::csvSqlPreservesNullsAndEmptyResultHeaders()
{
    QSqlQuery query(m_public.database());
    QVERIFY(query.exec(QStringLiteral("CREATE TABLE csv_item(id INTEGER, text TEXT)")));
    QVERIFY(query.exec(QStringLiteral("INSERT INTO csv_item VALUES(1,'a,b'),(2,NULL)")));
    const QString path = m_root->filePath(QStringLiteral("query.csv"));
    QVERIFY2(darkeye::exportSqlToCsv(QStringLiteral("SELECT id,text FROM csv_item ORDER BY id"),
        path, m_public.databasePath(), &m_error), qPrintable(m_error));
    {
        QFile file(path);
        QVERIFY(file.open(QIODevice::ReadOnly | QIODevice::Text));
        QCOMPARE(file.readAll(), QByteArray("id,text\n1,\"a,b\"\n2,\n"));
    }
    QVERIFY2(darkeye::exportSqlToCsv(QStringLiteral("SELECT id,text FROM csv_item WHERE 0"),
        path, m_public.databasePath(), &m_error), qPrintable(m_error));
    QFile file(path);
    QVERIFY(file.open(QIODevice::ReadOnly | QIODevice::Text));
    QCOMPARE(file.readAll(), QByteArray("id,text\n"));
}

void DatabaseOperationsTest::failedCsvQueryPreservesExistingOutput()
{
    const QString path = m_root->filePath(QStringLiteral("existing.csv"));
    {
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        QCOMPARE(file.write("preserve"), 8);
    }
    QVERIFY(!darkeye::exportSqlToCsv(QStringLiteral("SELECT * FROM nonexistent"), path, m_public.databasePath(), &m_error));
    QVERIFY(!m_error.isEmpty());
    QVERIFY(!darkeye::exportSqlToCsv(QStringLiteral("DELETE FROM maker"), path, m_public.databasePath(), &m_error));
    QVERIFY(!darkeye::exportModelToCsv(nullptr, path, &m_error));
    QFile file(path);
    QVERIFY(file.open(QIODevice::ReadOnly));
    QCOMPARE(file.readAll(), QByteArray("preserve"));
}

void DatabaseOperationsTest::csvReadErrorDoesNotPublishPartialOutput()
{
    QSqlQuery query(m_public.database());
    QVERIFY(query.exec(QStringLiteral("CREATE TABLE csv_fault(id INTEGER)")));
    QVERIFY(query.exec(QStringLiteral("INSERT INTO csv_fault VALUES(1),(2)")));
    const QString path = m_root->filePath(QStringLiteral("existing.csv"));
    {
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        QCOMPARE(file.write("preserve"), 8);
    }
    // SQLite returns the first row, then raises integer overflow on the second.
    const QString sql = QStringLiteral("SELECT CASE WHEN id=2 THEN abs(-9223372036854775808) "
                                       "ELSE id END AS value FROM csv_fault");
    QVERIFY(!darkeye::exportSqlToCsv(sql, path, m_public.databasePath(), &m_error));
    QVERIFY(!m_error.isEmpty());
    QFile file(path);
    QVERIFY(file.open(QIODevice::ReadOnly));
    QCOMPARE(file.readAll(), QByteArray("preserve"));
}

void DatabaseOperationsTest::deletesPeopleWithoutOrphaningRelations_data()
{
    QTest::addColumn<bool>("actress");
    QTest::newRow("actress") << true;
    QTest::newRow("actor") << false;
}

void DatabaseOperationsTest::deletesPeopleWithoutOrphaningRelations()
{
    QFETCH(bool, actress);
    const auto kind = actress ? darkeye::PersonKind::Actress : darkeye::PersonKind::Actor;
    darkeye::PersonRepository people(m_public.database());
    darkeye::WorkRepository works(m_public.database());
    const auto personId = people.create(kind, QStringLiteral("中文姓名"), QStringLiteral("日本語名"), &m_error);
    QVERIFY(personId.has_value());
    auto details = people.findDetails(kind, *personId, &m_error);
    QVERIFY(details.has_value());
    details->names.append({0, QStringLiteral("旧中文姓名"), QStringLiteral("旧日本語名"), {}, {}});
    QVERIFY(people.updateDetails(*details, &m_error));
    darkeye::Work work;
    work.serialNumber = QStringLiteral("PERSON-001");
    const QList<qint64> ids{*personId};
    const auto workId = works.insertComplete(work, actress ? ids : QList<qint64>{}, actress ? QList<qint64>{} : ids, {}, &m_error);
    QVERIFY(workId.has_value());
    QVERIFY(!people.deletePerson(kind, *personId, &m_error));
    QCOMPARE(people.findDetails(kind, *personId)->names.size(), 2);
    QVERIFY(works.setDeleted(*workId, true, &m_error));
    QVERIFY(!people.deletePerson(kind, *personId, &m_error));
    QVERIFY(works.permanentlyRemoveDeletedMany({*workId}, nullptr, &m_error));
    QVERIFY2(people.deletePerson(kind, *personId, &m_error), qPrintable(m_error));
    QVERIFY(!people.findDetails(kind, *personId).has_value());
    const QString table = actress ? QStringLiteral("actress_name") : QStringLiteral("actor_name");
    QCOMPARE(scalar(m_public.database(), QStringLiteral("SELECT COUNT(*) FROM %1").arg(table)), 0);
    QVERIFY(!people.deletePerson(kind, *personId, &m_error));
    QSqlQuery check(m_public.database());
    QVERIFY(check.exec(QStringLiteral("PRAGMA foreign_key_check")));
    QVERIFY(!check.next());
}

QTEST_GUILESS_MAIN(DatabaseOperationsTest)
#include "DatabaseOperationsTest.moc"
