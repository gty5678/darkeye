#include "services/WorkMaintenanceService.h"

#include "database/SchemaManager.h"
#include "database/SqliteConnection.h"
#include "database/repositories/WorkRepository.h"

#include <QDir>
#include <QFile>
#include <QImage>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QtTest>

class WorkMaintenanceServiceTest final : public QObject
{
    Q_OBJECT

private slots:
    void assignsMakersUsingPythonPrefixSemantics();
    void normalizesCoverNamesAndSkipsUnsafeCases();
    void rollsBackAllCoverMovesWhenDatabaseUpdateFails();
};

void WorkMaintenanceServiceTest::assignsMakersUsingPythonPrefixSemantics()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    darkeye::SqliteConnection connection;
    QString errorMessage;
    QVERIFY(connection.open(directory.filePath("public.db"), false, &errorMessage));
    QVERIFY(darkeye::SchemaManager::initializeEmptyDatabase(
        connection, darkeye::DatabaseKind::Public, &errorMessage));
    QSqlQuery query(connection.database());
    QVERIFY(query.exec("INSERT INTO maker(cn_name) VALUES('旧片商')"));
    const qint64 firstMaker = query.lastInsertId().toLongLong();
    QVERIFY(query.exec("INSERT INTO maker(cn_name) VALUES('后写规则片商')"));
    const qint64 lastMaker = query.lastInsertId().toLongLong();
    query.prepare("INSERT INTO prefix_maker_relation(prefix, maker_id) VALUES(?, ?)");
    for (const qint64 maker : {firstMaker, lastMaker})
    {
        query.bindValue(0, QStringLiteral("AAA"));
        query.bindValue(1, maker);
        QVERIFY(query.exec());
    }

    darkeye::WorkRepository works(connection.database());
    darkeye::Work work;
    work.serialNumber = QStringLiteral("AAA-001");
    const qint64 changedId = *works.insertComplete(work, {}, {}, {}, &errorMessage);
    work.serialNumber = QStringLiteral("AAA-002");
    work.makerId = lastMaker;
    const qint64 sameId = *works.insertComplete(work, {}, {}, {}, &errorMessage);
    work.serialNumber = QStringLiteral("CCC-001");
    work.makerId.reset();
    QVERIFY(works.insertComplete(work, {}, {}, {}, &errorMessage).has_value());
    work.serialNumber = QStringLiteral("NOHYPHEN");
    QVERIFY(works.insertComplete(work, {}, {}, {}, &errorMessage).has_value());
    work.serialNumber = QStringLiteral("-EMPTY-PREFIX");
    QVERIFY(works.insertComplete(work, {}, {}, {}, &errorMessage).has_value());
    work.serialNumber = QStringLiteral("AAA-REMOVED");
    const qint64 deletedId = *works.insertComplete(work, {}, {}, {}, &errorMessage);
    QVERIFY(works.setDeleted(deletedId, true, &errorMessage));

    const darkeye::MakerAssignmentResult result =
        darkeye::WorkMaintenanceService(connection.database()).assignMakersFromPrefixes();
    QVERIFY2(result.succeeded, qPrintable(result.errorMessage));
    QCOMPARE(result.updated, 1);
    QCOMPARE(result.alreadyMatched, 1);
    QCOMPARE(result.noRule, 1);
    QCOMPARE(result.noPrefix, 2);
    QCOMPARE(works.findById(changedId)->makerId, std::optional<qint64>(lastMaker));
    QCOMPARE(works.findById(sameId)->makerId, std::optional<qint64>(lastMaker));
    QVERIFY(!works.findById(deletedId)->makerId.has_value());
}

void WorkMaintenanceServiceTest::normalizesCoverNamesAndSkipsUnsafeCases()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString covers = directory.filePath("covers");
    QVERIFY(QDir().mkpath(covers));
    darkeye::SqliteConnection connection;
    QString errorMessage;
    QVERIFY(connection.open(directory.filePath("public.db"), false, &errorMessage));
    QVERIFY(darkeye::SchemaManager::initializeEmptyDatabase(
        connection, darkeye::DatabaseKind::Public, &errorMessage));
    darkeye::WorkRepository works(connection.database());
    const auto addWork = [&](const QString &serial, const QString &imageUrl)
    {
        darkeye::Work work;
        work.serialNumber = serial;
        work.imageUrl = imageUrl;
        return *works.insertComplete(work, {}, {}, {}, &errorMessage);
    };
    const auto saveImage = [](const QString &path)
    {
        QImage image(20, 20, QImage::Format_RGB32);
        image.fill(Qt::red);
        return image.save(path);
    };

    const qint64 renamedId = addWork(QStringLiteral("ABC-001"), QStringLiteral("legacy.png"));
    QVERIFY(saveImage(QDir(covers).filePath("legacy.png")));
    const qint64 databaseOnlyId = addWork(QStringLiteral("NORMAL"), QStringLiteral("./NORMAL.jpg"));
    QVERIFY(saveImage(QDir(covers).filePath("NORMAL.jpg")));
    addWork(QStringLiteral("CONFLICT"), QStringLiteral("conflict-source.jpg"));
    QVERIFY(saveImage(QDir(covers).filePath("conflict-source.jpg")));
    QVERIFY(saveImage(QDir(covers).filePath("CONFLICT.jpg")));
    addWork(QStringLiteral("MISSING"), QStringLiteral("missing.jpg"));
    addWork(QStringLiteral("OUTSIDE"), QStringLiteral("../outside.jpg"));
    QVERIFY(saveImage(directory.filePath("outside.jpg")));

    const darkeye::CoverNormalizationResult result =
        darkeye::WorkMaintenanceService(connection.database(), covers).normalizeCoverFileNames();
    QVERIFY2(result.succeeded, qPrintable(result.errorMessage));
    QCOMPARE(result.renamed, 1);
    QCOMPARE(result.databaseOnly, 1);
    QCOMPARE(result.conflict, 1);
    QCOMPARE(result.missing, 1);
    QCOMPARE(result.invalidOrOutside, 1);
    QVERIFY(!QFileInfo::exists(QDir(covers).filePath("legacy.png")));
    QVERIFY(QFileInfo::exists(QDir(covers).filePath("ABC-001.jpg")));
    QCOMPARE(works.findById(renamedId)->imageUrl, QStringLiteral("ABC-001.jpg"));
    QCOMPARE(works.findById(databaseOnlyId)->imageUrl, QStringLiteral("NORMAL.jpg"));
}

void WorkMaintenanceServiceTest::rollsBackAllCoverMovesWhenDatabaseUpdateFails()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString covers = directory.filePath("covers");
    QVERIFY(QDir().mkpath(covers));
    darkeye::SqliteConnection connection;
    QString errorMessage;
    QVERIFY(connection.open(directory.filePath("public.db"), false, &errorMessage));
    QVERIFY(darkeye::SchemaManager::initializeEmptyDatabase(
        connection, darkeye::DatabaseKind::Public, &errorMessage));
    darkeye::WorkRepository works(connection.database());
    darkeye::Work first;
    first.serialNumber = QStringLiteral("ROLL-001");
    first.imageUrl = QStringLiteral("first.png");
    const qint64 firstId = *works.insertComplete(first, {}, {}, {}, &errorMessage);
    darkeye::Work second;
    second.serialNumber = QStringLiteral("ROLL-002");
    second.imageUrl = QStringLiteral("second.png");
    const qint64 secondId = *works.insertComplete(second, {}, {}, {}, &errorMessage);
    QImage image(20, 20, QImage::Format_RGB32);
    image.fill(Qt::blue);
    QVERIFY(image.save(QDir(covers).filePath("first.png")));
    QVERIFY(image.save(QDir(covers).filePath("second.png")));
    QSqlQuery trigger(connection.database());
    QVERIFY(trigger.exec(
        QStringLiteral("CREATE TRIGGER fail_cover_update BEFORE UPDATE OF image_url ON work "
                       "WHEN OLD.work_id=%1 BEGIN SELECT RAISE(ABORT, 'forced failure'); END;")
            .arg(secondId)));

    const darkeye::CoverNormalizationResult result =
        darkeye::WorkMaintenanceService(connection.database(), covers).normalizeCoverFileNames();
    QVERIFY(!result.succeeded);
    QVERIFY(QFileInfo::exists(QDir(covers).filePath("first.png")));
    QVERIFY(QFileInfo::exists(QDir(covers).filePath("second.png")));
    QVERIFY(!QFileInfo::exists(QDir(covers).filePath("ROLL-001.jpg")));
    QVERIFY(!QFileInfo::exists(QDir(covers).filePath("ROLL-002.jpg")));
    QCOMPARE(works.findById(firstId)->imageUrl, QStringLiteral("first.png"));
    QCOMPARE(works.findById(secondId)->imageUrl, QStringLiteral("second.png"));
}

QTEST_MAIN(WorkMaintenanceServiceTest)
#include "WorkMaintenanceServiceTest.moc"
