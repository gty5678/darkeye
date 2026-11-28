#include "database/SchemaManager.h"
#include "database/SqlScriptRunner.h"
#include "graph/GraphRepository.h"

#include <QSqlError>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QtTest>

class GraphSchemaTest final : public QObject
{
    Q_OBJECT

private slots:
    void readsProductionSchema_data();
    void readsProductionSchema();
};

void GraphSchemaTest::readsProductionSchema_data()
{
    QTest::addColumn<bool>("migrateLegacy");
    QTest::newRow("fresh") << false;
    QTest::newRow("migrated") << true;
}

void GraphSchemaTest::readsProductionSchema()
{
    QFETCH(bool, migrateLegacy);
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    darkeye::SqliteConnection publicConnection;
    darkeye::SqliteConnection privateConnection;
    QString error;
    QVERIFY2(publicConnection.open(directory.filePath(QStringLiteral("public.db")), false, &error),
             qPrintable(error));
    QVERIFY2(privateConnection.open(directory.filePath(QStringLiteral("private.db")), false, &error),
             qPrintable(error));

    if (migrateLegacy)
    {
        QVERIFY2(darkeye::SqlScriptRunner::executeResource(
                     publicConnection.database(), QStringLiteral(":/sql/fixtures/initPublicTable-v1.0.sql"),
                     &error), qPrintable(error));
        QVERIFY2(darkeye::SqlScriptRunner::executeResource(
                     privateConnection.database(), QStringLiteral(":/sql/fixtures/initPrivateTable-v1.0.sql"),
                     &error), qPrintable(error));
    }
    else
    {
        QVERIFY2(darkeye::SchemaManager::initializeEmptyDatabase(
                     publicConnection, darkeye::DatabaseKind::Public, &error), qPrintable(error));
        QVERIFY2(darkeye::SchemaManager::initializeEmptyDatabase(
                     privateConnection, darkeye::DatabaseKind::Private, &error), qPrintable(error));
    }

    QSqlQuery publicQuery(publicConnection.database());
    for (const QString &statement : {
             QStringLiteral("INSERT INTO actress(actress_id) VALUES(8)"),
             QStringLiteral("INSERT INTO actress_name(actress_id, cn) VALUES(8, '演员')"),
             QStringLiteral("INSERT INTO work(work_id, serial_number, image_url) "
                            "VALUES(42, 'GRAPH-1', 'cover.png')"),
             QStringLiteral("INSERT INTO work_actress_relation(work_id, actress_id) VALUES(42, 8)")})
    {
        QVERIFY2(publicQuery.exec(statement), qPrintable(publicQuery.lastError().text()));
    }
    QSqlQuery privateQuery(privateConnection.database());
    QVERIFY2(privateQuery.exec(QStringLiteral("INSERT INTO favorite_work(work_id, serial_number) "
                                              "VALUES(42, 'GRAPH-1')")),
             qPrintable(privateQuery.lastError().text()));

    if (migrateLegacy)
    {
        QVERIFY2(darkeye::SchemaManager::migrateToCurrent(
                     publicConnection, darkeye::DatabaseKind::Public, &error), qPrintable(error));
        QVERIFY2(darkeye::SchemaManager::migrateToCurrent(
                     privateConnection, darkeye::DatabaseKind::Private, &error), qPrintable(error));
    }

    const darkeye::graph::GraphRepository repository(publicConnection.database(),
                                                    privateConnection.database());
    darkeye::graph::GraphStore store;
    QVERIFY2(repository.load(store, &error), qPrintable(error));
    QVERIFY(store.containsNode(QStringLiteral("a8")));
    QVERIFY(store.containsNode(QStringLiteral("w42")));
    QVERIFY(store.edges().contains({QStringLiteral("w42"), QStringLiteral("a8"), QStringLiteral("cast")}));
    QCOMPARE(repository.imagePathForNode(QStringLiteral("w42")), QStringLiteral("cover.png"));
    QCOMPARE(repository.favoriteWorkNodeIds(&error), QSet<QString>{QStringLiteral("w42")});
    QVERIFY2(error.isEmpty(), qPrintable(error));
}

QTEST_GUILESS_MAIN(GraphSchemaTest)
#include "GraphSchemaTest.moc"
