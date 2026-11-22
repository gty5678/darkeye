#include "database/SqliteConnection.h"
#include "database/Transaction.h"

#include <QDir>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QtTest>

class SqliteConnectionTest final : public QObject
{
    Q_OBJECT

private slots:
    void enablesRequiredPragmasAndReadsUserVersion();
    void readsLegacyTextVersion();
    void refusesMissingReadOnlyDatabase();
    void readOnlyConnectionRejectsWrites();
    void transactionsCommitAndRollback();
    void failedCommitRollsBackOnDestruction();
    void reopeningDoesNotLeakConnections();
};

void SqliteConnectionTest::enablesRequiredPragmasAndReadsUserVersion()
{
    QTemporaryDir temporaryDirectory;
    QVERIFY(temporaryDirectory.isValid());
    const QString databasePath = QDir(temporaryDirectory.path()).filePath(QStringLiteral("test.db"));

    darkeye::SqliteConnection connection;
    QString errorMessage;
    QVERIFY2(connection.open(databasePath, false, &errorMessage), qPrintable(errorMessage));
    QVERIFY(connection.isOpen());
    QVERIFY(!connection.isReadOnly());

    QSqlQuery query(connection.database());
    QVERIFY(query.exec(QStringLiteral("PRAGMA foreign_keys")));
    QVERIFY(query.next());
    QCOMPARE(query.value(0).toInt(), 1);

    QVERIFY(query.exec(QStringLiteral("PRAGMA journal_mode")));
    QVERIFY(query.next());
    QCOMPARE(query.value(0).toString().toLower(), QStringLiteral("wal"));

    QVERIFY(query.exec(QStringLiteral("PRAGMA user_version=2")));
    QCOMPARE(connection.schemaVersion(&errorMessage), QStringLiteral("2"));
}

void SqliteConnectionTest::readsLegacyTextVersion()
{
    QTemporaryDir temporaryDirectory;
    QVERIFY(temporaryDirectory.isValid());
    const QString databasePath = QDir(temporaryDirectory.path()).filePath(QStringLiteral("legacy.db"));

    darkeye::SqliteConnection connection;
    QString errorMessage;
    QVERIFY2(connection.open(databasePath, false, &errorMessage), qPrintable(errorMessage));

    QSqlQuery query(connection.database());
    QVERIFY(query.exec(QStringLiteral(
        "CREATE TABLE db_version(version TEXT NOT NULL, applied_at TEXT NOT NULL)")));
    QVERIFY(query.exec(QStringLiteral(
        "INSERT INTO db_version(version, applied_at) VALUES('1.1', '2026-03-12 09:28:01')")));
    QCOMPARE(connection.schemaVersion(&errorMessage), QStringLiteral("1.1"));
}

void SqliteConnectionTest::refusesMissingReadOnlyDatabase()
{
    QTemporaryDir temporaryDirectory;
    QVERIFY(temporaryDirectory.isValid());
    const QString databasePath = QDir(temporaryDirectory.path()).filePath(QStringLiteral("missing.db"));

    darkeye::SqliteConnection connection;
    QString errorMessage;
    QVERIFY(!connection.open(databasePath, true, &errorMessage));
    QVERIFY(!errorMessage.isEmpty());
    QVERIFY(!QFileInfo::exists(databasePath));
}

void SqliteConnectionTest::readOnlyConnectionRejectsWrites()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    const QString path = root.filePath(QStringLiteral("readonly.db"));
    QString error;
    darkeye::SqliteConnection connection;
    QVERIFY2(connection.open(path, false, &error), qPrintable(error));
    {
        QSqlQuery query(connection.database());
        QVERIFY(query.exec(QStringLiteral("CREATE TABLE item(value TEXT)")));
        QVERIFY(query.exec(QStringLiteral("INSERT INTO item VALUES('original')")));
    }
    QVERIFY2(connection.open(path, true, &error), qPrintable(error));
    QVERIFY(connection.isReadOnly());
    QSqlQuery query(connection.database());
    QVERIFY(!query.exec(QStringLiteral("UPDATE item SET value='changed'")));
    QVERIFY(query.exec(QStringLiteral("SELECT value FROM item")));
    QVERIFY(query.next());
    QCOMPARE(query.value(0).toString(), QStringLiteral("original"));
}

void SqliteConnectionTest::transactionsCommitAndRollback()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    darkeye::SqliteConnection connection;
    QVERIFY(connection.open(root.filePath(QStringLiteral("transactions.db")), false));
    QSqlQuery query(connection.database());
    QVERIFY(query.exec(QStringLiteral("CREATE TABLE item(value INTEGER)")));
    {
        darkeye::Transaction transaction(connection.database());
        QVERIFY(transaction.isActive());
        QVERIFY(query.exec(QStringLiteral("INSERT INTO item VALUES(1)")));
        QVERIFY(transaction.commit());
        QVERIFY(!transaction.isActive());
        QVERIFY(!transaction.commit());
    }
    {
        darkeye::Transaction transaction(connection.database());
        QVERIFY(transaction.isActive());
        QVERIFY(query.exec(QStringLiteral("INSERT INTO item VALUES(2)")));
        transaction.rollback();
        QVERIFY(!transaction.isActive());
    }
    {
        darkeye::Transaction transaction(connection.database());
        QVERIFY(transaction.isActive());
        QVERIFY(query.exec(QStringLiteral("INSERT INTO item VALUES(3)")));
    }
    QVERIFY(query.exec(QStringLiteral("SELECT value FROM item")));
    QVERIFY(query.next());
    QCOMPARE(query.value(0).toInt(), 1);
    QVERIFY(!query.next());
}

void SqliteConnectionTest::failedCommitRollsBackOnDestruction()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    darkeye::SqliteConnection connection;
    QVERIFY(connection.open(root.filePath(QStringLiteral("deferred.db")), false));
    QSqlQuery query(connection.database());
    QVERIFY(query.exec(QStringLiteral("CREATE TABLE parent(id INTEGER PRIMARY KEY)")));
    QVERIFY(query.exec(QStringLiteral("CREATE TABLE child(parent_id INTEGER REFERENCES parent(id) "
                                      "DEFERRABLE INITIALLY DEFERRED)")));
    {
        darkeye::Transaction transaction(connection.database());
        QVERIFY(transaction.isActive());
        QVERIFY(query.exec(QStringLiteral("INSERT INTO child VALUES(999)")));
        QVERIFY(!transaction.commit());
        QVERIFY(!transaction.errorString().isEmpty());
    }
    QVERIFY(query.exec(QStringLiteral("SELECT COUNT(*) FROM child")));
    QVERIFY(query.next());
    QCOMPARE(query.value(0).toInt(), 0);
    query.finish();
    darkeye::Transaction next(connection.database());
    QVERIFY(next.isActive());
    QVERIFY(next.commit());
}

void SqliteConnectionTest::reopeningDoesNotLeakConnections()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    const auto before = QSqlDatabase::connectionNames();
    {
        darkeye::SqliteConnection connection;
        QVERIFY(connection.open(root.filePath(QStringLiteral("first.db")), false));
        QVERIFY(connection.open(root.filePath(QStringLiteral("second.db")), false));
        QCOMPARE(QSqlDatabase::connectionNames().size(), before.size() + 1);
        QVERIFY(!connection.open(root.filePath(QStringLiteral("missing.db")), true));
        QVERIFY(!connection.isOpen());
        QVERIFY(connection.databasePath().isEmpty());
        QCOMPARE(QSqlDatabase::connectionNames(), before);
    }
    QCOMPARE(QSqlDatabase::connectionNames(), before);
}

QTEST_MAIN(SqliteConnectionTest)
#include "SqliteConnectionTest.moc"

