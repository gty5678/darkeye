#include "database/SchemaManager.h"
#include "database/repositories/PrivateRepository.h"

#include <QDir>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QtTest>

class PrivateRepositoryTest final : public QObject
{
    Q_OBJECT

private slots:
    void managesFavoritesIdempotently();
    void writesAndAggregatesPersonalRecords();
    void aggregatesLeapDayAndYearBoundaries();
    void preservesQuotedUnicodeAndRejectsInvalidRatings();
};

void PrivateRepositoryTest::managesFavoritesIdempotently()
{
    QTemporaryDir temporaryDirectory;
    QVERIFY(temporaryDirectory.isValid());
    darkeye::SqliteConnection connection;
    QString errorMessage;
    QVERIFY2(connection.open(QDir(temporaryDirectory.path()).filePath("private.db"), false,
                             &errorMessage),
             qPrintable(errorMessage));
    QVERIFY2(darkeye::SchemaManager::initializeEmptyDatabase(
                 connection, darkeye::DatabaseKind::Private, &errorMessage),
             qPrintable(errorMessage));

    darkeye::PrivateRepository repository(connection.database());
    QVERIFY(repository.addFavoriteWork(7, QStringLiteral("ABC-7"), &errorMessage));
    QVERIFY(repository.addFavoriteWork(8, QStringLiteral("ABC-8"), &errorMessage));
    QVERIFY(repository.addFavoriteWork(7, QStringLiteral("ABC-7"), &errorMessage));
    QVERIFY(repository.isFavoriteWork(7, &errorMessage));
    QVERIFY(repository.addFavoriteActress(3, QStringLiteral("名前"), &errorMessage));
    QVERIFY(repository.isFavoriteActress(3, &errorMessage));

    QSqlQuery query(connection.database());
    QVERIFY(query.exec(QStringLiteral("SELECT COUNT(*) FROM favorite_work")));
    QVERIFY(query.next());
    QCOMPARE(query.value(0).toInt(), 2);
    QVERIFY(repository.addMasturbationRecord(7, QStringLiteral("ABC-7"),
                                             QStringLiteral("2026-09-01"),
                                             QString(), 3, QString(), &errorMessage));
    QCOMPARE(repository.favoriteWorkIds(&errorMessage), QList<qint64>({7, 8}));
    QCOMPARE(repository.masturbationWorkIds(&errorMessage), QList<qint64>({7}));
    QCOMPARE(repository.favoriteUnwatchedWorkIds(&errorMessage), QList<qint64>({8}));

    QVERIFY(repository.removeFavoriteWork(7, &errorMessage));
    QVERIFY(!repository.isFavoriteWork(7, &errorMessage));
}

void PrivateRepositoryTest::writesAndAggregatesPersonalRecords()
{
    QTemporaryDir temporaryDirectory;
    QVERIFY(temporaryDirectory.isValid());
    darkeye::SqliteConnection connection;
    QString errorMessage;
    QVERIFY2(connection.open(QDir(temporaryDirectory.path()).filePath("private.db"), false,
                             &errorMessage),
             qPrintable(errorMessage));
    QVERIFY2(darkeye::SchemaManager::initializeEmptyDatabase(
                 connection, darkeye::DatabaseKind::Private, &errorMessage),
             qPrintable(errorMessage));

    darkeye::PrivateRepository repository(connection.database());
    QVERIFY(repository.addMasturbationRecord(1, QStringLiteral("ABC-1"),
                                             QStringLiteral("2026-09-01 20:30"),
                                             QStringLiteral("tool"), 4,
                                             QStringLiteral("note"), &errorMessage));
    QVERIFY(repository.addMasturbationRecord(0, QString(),
                                             QStringLiteral("2026-09-01 22:00"),
                                             QString(), 3, QString(), &errorMessage));
    QVERIFY(!repository.addMasturbationRecord(0, QString(),
                                              QStringLiteral("2026-09-02"), QString(), 6,
                                              QString(), &errorMessage));

    const QMap<QDate, int> counts = repository.dailyCounts(
        2026, darkeye::PersonalRecordKind::Masturbation, &errorMessage);
    QCOMPARE(counts.value(QDate(2026, 9, 1)), 2);

    QVERIFY(repository.addLoveMakingRecord(QStringLiteral("2026-09-02 21:15"), 5,
                                           QStringLiteral("love note"), &errorMessage));
    QVERIFY(!repository.addLoveMakingRecord(QStringLiteral("2026-09-02 21:15"), 0,
                                            QString(), &errorMessage));
    QVERIFY(repository.addSexualArousalRecord(QStringLiteral("2026-09-03 06:00"),
                                              QStringLiteral("dream"), &errorMessage));
    QCOMPARE(repository.dailyCounts(2026, darkeye::PersonalRecordKind::LoveMaking,
                                    &errorMessage)
                 .value(QDate(2026, 9, 2)),
             1);
    QCOMPARE(repository.dailyCounts(2026, darkeye::PersonalRecordKind::SexualArousal,
                                    &errorMessage)
                 .value(QDate(2026, 9, 3)),
             1);
    QCOMPARE(repository.masturbationToolSuggestions(&errorMessage),
             QStringList({QStringLiteral("tool"), QString()}));
}

void PrivateRepositoryTest::aggregatesLeapDayAndYearBoundaries()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    darkeye::SqliteConnection connection;
    QString error;
    QVERIFY(connection.open(root.filePath(QStringLiteral("private.db")), false, &error));
    QVERIFY(darkeye::SchemaManager::initializeEmptyDatabase(connection, darkeye::DatabaseKind::Private, &error));
    darkeye::PrivateRepository repository(connection.database());
    const QStringList dates = {QStringLiteral("2023-12-31 23:59:59"),
                               QStringLiteral("2024-01-01 00:00:00"),
                               QStringLiteral("2024-02-29 12:00:00"),
                               QStringLiteral("2024-02-29 13:00:00"),
                               QStringLiteral("2024-12-31 23:59:59"),
                               QStringLiteral("2025-01-01 00:00:00")};
    for (const auto &date : dates)
    {
        QVERIFY(repository.addMasturbationRecord(0, {}, date, {}, 1, {}, &error));
        QVERIFY(repository.addLoveMakingRecord(date, 5, {}, &error));
        QVERIFY(repository.addSexualArousalRecord(date, {}, &error));
    }
    for (const auto kind : {darkeye::PersonalRecordKind::Masturbation,
                            darkeye::PersonalRecordKind::LoveMaking,
                            darkeye::PersonalRecordKind::SexualArousal})
    {
        const auto counts = repository.dailyCounts(2024, kind, &error);
        QCOMPARE(counts.size(), 3);
        QCOMPARE(counts.value(QDate(2024, 1, 1)), 1);
        QCOMPARE(counts.value(QDate(2024, 2, 29)), 2);
        QCOMPARE(counts.value(QDate(2024, 12, 31)), 1);
        QVERIFY(repository.dailyCounts(2022, kind, &error).isEmpty());
    }
    QVERIFY(repository.masturbationWorkIds(&error).isEmpty());
}

void PrivateRepositoryTest::preservesQuotedUnicodeAndRejectsInvalidRatings()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    darkeye::SqliteConnection connection;
    QString error;
    QVERIFY(connection.open(root.filePath(QStringLiteral("private.db")), false, &error));
    QVERIFY(darkeye::SchemaManager::initializeEmptyDatabase(connection, darkeye::DatabaseKind::Private, &error));
    darkeye::PrivateRepository repository(connection.database());
    const QString comment = QStringLiteral("中文・日本語 O'Brien\n'; DROP TABLE masturbation; --");
    QVERIFY(repository.addMasturbationRecord(42, QStringLiteral("ABC-42"),
        QStringLiteral("2024-02-29 12:00"), QStringLiteral("O'Brien"), 5, comment, &error));
    for (const int rating : {-1, 0, 6})
    {
        QVERIFY(!repository.addMasturbationRecord(0, {}, QStringLiteral("2024-02-29"), {}, rating, {}, &error));
        QVERIFY(!error.isEmpty());
        QVERIFY(!repository.addLoveMakingRecord(QStringLiteral("2024-02-29"), rating, {}, &error));
    }
    QSqlQuery query(connection.database());
    QVERIFY(query.exec(QStringLiteral("SELECT comment, rating FROM masturbation")));
    QVERIFY(query.next());
    QCOMPARE(query.value(0).toString(), comment);
    QCOMPARE(query.value(1).toInt(), 5);
    QVERIFY(!query.next());
    QVERIFY(query.exec(QStringLiteral("SELECT COUNT(*) FROM love_making")));
    QVERIFY(query.next());
    QCOMPARE(query.value(0).toInt(), 0);
}

QTEST_MAIN(PrivateRepositoryTest)
#include "PrivateRepositoryTest.moc"
