#include "database/SchemaManager.h"
#include "database/SqliteConnection.h"
#include "database/repositories/PrivateRepository.h"
#include "database/repositories/WorkRepository.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSqlQuery>
#include <QtTest>

class DatabaseFixtureTest final : public QObject
{
    Q_OBJECT

private slots:
    void emptyAndHistoricalFixturesHaveExpectedVersions();
    void typicalFixturesMatchGoldenQueries();
    void invalidFixturesAreRejected();
};

QString fixturePath(const QString &name)
{
    return QDir(QStringLiteral(DARKEYE_TEST_DATABASE_DIR)).filePath(name);
}

void DatabaseFixtureTest::emptyAndHistoricalFixturesHaveExpectedVersions()
{
    struct Expectation
    {
        QString name;
        QString version;
    };
    const QList<Expectation> expectations = {
        {QStringLiteral("public-empty-v2.db"), QStringLiteral("2")},
        {QStringLiteral("private-empty-v1.1.db"), QStringLiteral("1.1")},
        {QStringLiteral("public-legacy-v1.0.db"), QStringLiteral("1.0")},
        {QStringLiteral("private-legacy-v1.0.db"), QStringLiteral("1.0")},
    };
    for (const auto &expectation : expectations) {
        darkeye::SqliteConnection connection;
        QString errorMessage;
        QVERIFY2(connection.open(fixturePath(expectation.name), true, &errorMessage),
                 qPrintable(errorMessage));
        QCOMPARE(connection.schemaVersion(&errorMessage), expectation.version);
    }
}

void DatabaseFixtureTest::typicalFixturesMatchGoldenQueries()
{
    QString errorMessage;
    darkeye::SqliteConnection publicConnection;
    QVERIFY2(publicConnection.open(fixturePath(QStringLiteral("public-typical-v2.db")),
                                   true, &errorMessage),
             qPrintable(errorMessage));
    darkeye::WorkRepository works(publicConnection.database());
    const auto work = works.findById(1, &errorMessage);
    QVERIFY2(work.has_value(), qPrintable(errorMessage));

    QJsonObject workObject{
        {QStringLiteral("serialNumber"), work->serialNumber},
        {QStringLiteral("title"), work->chineseTitle},
        {QStringLiteral("runtime"), work->runtime.value_or(0)},
        {QStringLiteral("deleted"), work->deleted},
    };
    QSqlQuery relationQuery(publicConnection.database());
    QVERIFY(relationQuery.exec(QStringLiteral(
        "SELECT an.cn FROM work_actress_relation r "
        "JOIN actress_name an ON an.actress_id=r.actress_id AND an.name_type=1 "
        "WHERE r.work_id=1 ORDER BY an.actress_name_id")));
    QJsonArray actresses;
    while (relationQuery.next()) actresses.append(relationQuery.value(0).toString());
    workObject.insert(QStringLiteral("actresses"), actresses);
    QVERIFY(relationQuery.exec(QStringLiteral(
        "SELECT an.cn FROM work_actor_relation r "
        "JOIN actor_name an ON an.actor_id=r.actor_id AND an.name_type=1 "
        "WHERE r.work_id=1 ORDER BY an.actor_name_id")));
    QJsonArray actors;
    while (relationQuery.next()) actors.append(relationQuery.value(0).toString());
    workObject.insert(QStringLiteral("actors"), actors);
    QVERIFY(relationQuery.exec(QStringLiteral(
        "SELECT t.tag_name FROM work_tag_relation r "
        "JOIN tag t ON t.tag_id=r.tag_id WHERE r.work_id=1 ORDER BY t.tag_id")));
    QJsonArray tags;
    while (relationQuery.next()) tags.append(relationQuery.value(0).toString());
    workObject.insert(QStringLiteral("tags"), tags);

    QJsonObject publicObject{
        {QStringLiteral("allWorkIds"), QJsonArray{1, 2}},
        {QStringLiteral("activeWorkIds"), QJsonArray{1}},
        {QStringLiteral("work1"), workObject},
    };
    QCOMPARE(works.allIds(true, &errorMessage), QList<qint64>({1, 2}));
    QCOMPARE(works.allIds(false, &errorMessage), QList<qint64>({1}));

    darkeye::SqliteConnection privateConnection;
    QVERIFY2(privateConnection.open(
                 fixturePath(QStringLiteral("private-typical-v1.1.db")), true,
                 &errorMessage),
             qPrintable(errorMessage));
    darkeye::PrivateRepository records(privateConnection.database());
    QVERIFY(records.isFavoriteWork(1, &errorMessage));
    QVERIFY(records.isFavoriteActress(1, &errorMessage));
    const QMap<QDate, int> counts =
        records.dailyCounts(2026, darkeye::PersonalRecordKind::Masturbation,
                            &errorMessage);
    QJsonObject countObject;
    for (auto iterator = counts.cbegin(); iterator != counts.cend(); ++iterator) {
        countObject.insert(iterator.key().toString(Qt::ISODate), iterator.value());
    }
    const QJsonObject privateObject{
        {QStringLiteral("favoriteWorkIds"), QJsonArray{1}},
        {QStringLiteral("favoriteActressIds"), QJsonArray{1}},
        {QStringLiteral("dailyMasturbationCounts2026"), countObject},
    };

    QFile goldenFile(QDir(QStringLiteral(DARKEYE_SOURCE_DIR)).filePath(
        QStringLiteral("tests/fixtures/golden/core-queries.json")));
    QVERIFY2(goldenFile.open(QIODevice::ReadOnly), qPrintable(goldenFile.errorString()));
    const QJsonObject golden = QJsonDocument::fromJson(goldenFile.readAll()).object();
    QCOMPARE(publicObject, golden.value(QStringLiteral("public")).toObject());
    QCOMPARE(privateObject, golden.value(QStringLiteral("private")).toObject());
}

void DatabaseFixtureTest::invalidFixturesAreRejected()
{
    QString errorMessage;
    darkeye::SqliteConnection unknown;
    QVERIFY2(unknown.open(fixturePath(QStringLiteral("unknown-v99.db")), true,
                          &errorMessage),
             qPrintable(errorMessage));
    QCOMPARE(unknown.schemaVersion(&errorMessage), QStringLiteral("99"));
    QVERIFY(!darkeye::SchemaManager::validateCurrentSchema(
        unknown, darkeye::DatabaseKind::Public, &errorMessage));

    darkeye::SqliteConnection corrupt;
    QVERIFY2(corrupt.open(
                 fixturePath(QStringLiteral("public-corrupt-foreign-key.db")), true,
                 &errorMessage),
             qPrintable(errorMessage));
    QVERIFY(!darkeye::SchemaManager::validateCurrentSchema(
        corrupt, darkeye::DatabaseKind::Public, &errorMessage));
    QVERIFY(errorMessage.contains(QStringLiteral("外键损坏")));
}

QTEST_MAIN(DatabaseFixtureTest)
#include "DatabaseFixtureTest.moc"
