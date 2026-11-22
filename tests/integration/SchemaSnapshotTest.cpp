#include "database/SchemaManager.h"
#include "database/SchemaSnapshot.h"
#include "database/SqliteConnection.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QtTest>

class SchemaSnapshotTest final : public QObject
{
    Q_OBJECT

private slots:
    void currentSchemaMatchesFrozenSnapshot_data();
    void currentSchemaMatchesFrozenSnapshot();
};

void SchemaSnapshotTest::currentSchemaMatchesFrozenSnapshot_data()
{
    QTest::addColumn<int>("kind");
    QTest::addColumn<QString>("fixtureName");
    QTest::newRow("public-v2") << static_cast<int>(darkeye::DatabaseKind::Public)
                               << QStringLiteral("public-v2.json");
    QTest::newRow("private-v1.1") << static_cast<int>(darkeye::DatabaseKind::Private)
                                  << QStringLiteral("private-v1.1.json");
}

void SchemaSnapshotTest::currentSchemaMatchesFrozenSnapshot()
{
    QFETCH(int, kind);
    QFETCH(QString, fixtureName);
    const auto databaseKind = static_cast<darkeye::DatabaseKind>(kind);

    QTemporaryDir temporaryDirectory;
    QVERIFY(temporaryDirectory.isValid());
    darkeye::SqliteConnection connection;
    QString errorMessage;
    const QString databasePath =
        QDir(temporaryDirectory.path()).filePath(QStringLiteral("fixture.db"));
    QVERIFY2(connection.open(databasePath, false, &errorMessage),
             qPrintable(errorMessage));
    QVERIFY2(darkeye::SchemaManager::initializeEmptyDatabase(
                 connection, databaseKind, &errorMessage),
             qPrintable(errorMessage));

    const QJsonDocument actual =
        darkeye::SchemaSnapshot::capture(connection.database(), &errorMessage);
    QVERIFY2(!actual.isNull(), qPrintable(errorMessage));

    const QString fixturePath =
        QDir(QStringLiteral(DARKEYE_SOURCE_DIR))
            .filePath(QStringLiteral("tests/fixtures/schema/%1").arg(fixtureName));
    QFile fixture(fixturePath);
    QVERIFY2(fixture.open(QIODevice::ReadOnly), qPrintable(fixture.errorString()));
    QJsonParseError parseError;
    const QJsonDocument expected =
        QJsonDocument::fromJson(fixture.readAll(), &parseError);
    QCOMPARE(parseError.error, QJsonParseError::NoError);
    QCOMPARE(actual, expected);
}

QTEST_MAIN(SchemaSnapshotTest)
#include "SchemaSnapshotTest.moc"
