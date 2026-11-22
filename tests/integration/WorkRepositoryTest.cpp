#include "database/repositories/WorkRepository.h"
#include "database/repositories/ReferenceRepository.h"
#include "database/SchemaManager.h"
#include "database/Transaction.h"

#include <QDir>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QtTest>

class WorkRepositoryTest final : public QObject
{
    Q_OBJECT

private slots:
    void insertsFindsAndSoftDeletesWork();
    void transactionRollsBackUnlessCommitted();
    void completeInsertCommitsRelationsAtomically();
    void searchesAndUpdatesWorkDetails();
    void filtersRelationsAndPaginates();
    void filtersWorksWithLocalVideo();
    void tagOptionsKeepPythonColorOrder();
};

void WorkRepositoryTest::tagOptionsKeepPythonColorOrder()
{
    QTemporaryDir directory;
    darkeye::SqliteConnection connection;
    QString error;
    QVERIFY(connection.open(QDir(directory.path()).filePath("public.db"), false, &error));
    QVERIFY(darkeye::SchemaManager::initializeEmptyDatabase(connection, darkeye::DatabaseKind::Public, &error));
    QSqlQuery seed(connection.database());
    QVERIFY(seed.exec(QStringLiteral("DELETE FROM tag")));
    QVERIFY(seed.exec(QStringLiteral("INSERT INTO tag_type(tag_type_id,tag_type_name,tag_order) VALUES(999,'type',1)")));
    QVERIFY(seed.exec(QStringLiteral("INSERT INTO tag(tag_id,tag_name,tag_type_id,color) VALUES"
                                    "(1,'Z',999,'#000000'),(2,'A',999,'#000000'),(3,'B',999,'#ffffff')")));
    darkeye::WorkRepository works(connection.database());
    const auto options = works.tagOptions();
    QCOMPARE(options.size(), 3);
    QCOMPARE(options[0].id, 1);
    QCOMPARE(options[1].id, 2);
    QCOMPARE(options[2].id, 3);
    darkeye::ReferenceRepository references(connection.database());
    const auto records = references.listTags(&error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QCOMPARE(records.size(), 3);
    for (int i = 0; i < 3; ++i)
        QCOMPARE(records[i].id, options[i].id);
}

void WorkRepositoryTest::insertsFindsAndSoftDeletesWork()
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

    darkeye::WorkRepository repository(connection.database());
    const std::optional<qint64> id =
        repository.insertSerial(QStringLiteral("  ABC-123  "), &errorMessage);
    QVERIFY2(id.has_value(), qPrintable(errorMessage));
    QVERIFY(repository.existsSerial(QStringLiteral("abc-123"), &errorMessage));
    QCOMPARE(repository.findIdBySerial(QStringLiteral("ABC-123"), &errorMessage), id);
    QVERIFY(!repository.findIdBySerial(QStringLiteral("abc-123"), &errorMessage).has_value());

    const std::optional<darkeye::Work> work = repository.findById(*id, &errorMessage);
    QVERIFY(work.has_value());
    QCOMPARE(work->serialNumber, QStringLiteral("ABC-123"));
    QVERIFY(!work->deleted);

    QVERIFY(repository.setDeleted(*id, true, &errorMessage));
    QCOMPARE(repository.allIds(false, &errorMessage).size(), 0);
    QCOMPARE(repository.allIds(true, &errorMessage), QList<qint64>{*id});
}

void WorkRepositoryTest::transactionRollsBackUnlessCommitted()
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
    darkeye::WorkRepository repository(connection.database());

    {
        darkeye::Transaction transaction(connection.database());
        QVERIFY2(transaction.isActive(), qPrintable(transaction.errorString()));
        QVERIFY(repository.insertSerial(QStringLiteral("ROLLBACK-1"), &errorMessage).has_value());
    }

    QVERIFY(!repository.existsSerial(QStringLiteral("ROLLBACK-1"), &errorMessage));
}

void WorkRepositoryTest::completeInsertCommitsRelationsAtomically()
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

    QSqlQuery seed(connection.database());
    QVERIFY(seed.exec(QStringLiteral("INSERT INTO actress DEFAULT VALUES")));
    const qint64 actressId = seed.lastInsertId().toLongLong();
    QVERIFY(seed.exec(QStringLiteral("INSERT INTO actress DEFAULT VALUES")));
    const qint64 secondActressId = seed.lastInsertId().toLongLong();
    QVERIFY(seed.exec(QStringLiteral("INSERT INTO actor DEFAULT VALUES")));
    const qint64 actorId = seed.lastInsertId().toLongLong();
    QVERIFY(seed.exec(QStringLiteral("INSERT INTO tag(tag_name) VALUES('tag')")));
    const qint64 tagId = seed.lastInsertId().toLongLong();

    darkeye::Work work;
    work.serialNumber = QStringLiteral("FULL-1");
    work.runtime = 120;
    work.notes = QStringLiteral("notes");
    darkeye::WorkRepository repository(connection.database());
    const auto workId =
        repository.insertComplete(work, {actressId, actressId}, {actorId}, {tagId}, &errorMessage);
    QVERIFY2(workId.has_value(), qPrintable(errorMessage));

    QSqlQuery countQuery(connection.database());
    QVERIFY(countQuery.exec(QStringLiteral("SELECT (SELECT COUNT(*) FROM work_actress_relation), "
                                           "(SELECT COUNT(*) FROM work_actor_relation), "
                                           "(SELECT COUNT(*) FROM work_tag_relation)")));
    QVERIFY(countQuery.next());
    QCOMPARE(countQuery.value(0).toInt(), 1);
    QCOMPARE(countQuery.value(1).toInt(), 1);
    QCOMPARE(countQuery.value(2).toInt(), 1);

    darkeye::Work invalidWork;
    invalidWork.serialNumber = QStringLiteral("ROLLBACK-FK");
    QVERIFY(!repository.insertComplete(invalidWork, {999999}, {}, {}, &errorMessage).has_value());
    QVERIFY(!repository.existsSerial(QStringLiteral("ROLLBACK-FK"), &errorMessage));

    darkeye::Work updated = *repository.findById(*workId, &errorMessage);
    updated.notes = QStringLiteral("事务内更新");
    QVERIFY(!repository.updateComplete(updated, {999999}, {}, {}, &errorMessage));
    QCOMPARE(repository.findById(*workId)->notes, QStringLiteral("notes"));
    QCOMPARE(repository.findDetailsById(*workId)->actresses.first().id, actressId);
    QVERIFY2(repository.updateComplete(updated, {secondActressId}, {}, {tagId}, &errorMessage),
             qPrintable(errorMessage));
    const auto updatedDetails = repository.findDetailsById(*workId, &errorMessage);
    QVERIFY(updatedDetails.has_value());
    QCOMPARE(updatedDetails->work.notes, QStringLiteral("事务内更新"));
    QCOMPARE(updatedDetails->actresses.size(), 1);
    QCOMPARE(updatedDetails->actresses.first().id, secondActressId);
    QVERIFY(updatedDetails->actors.isEmpty());
    QCOMPARE(updatedDetails->tags.size(), 1);
}

void WorkRepositoryTest::searchesAndUpdatesWorkDetails()
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

    darkeye::WorkRepository repository(connection.database());
    darkeye::Work first;
    first.serialNumber = QStringLiteral("ABC-010");
    first.chineseTitle = QStringLiteral("海边故事");
    first.director = QStringLiteral("测试导演");
    first.releaseDate = QStringLiteral("2025-01-02");
    const auto firstId = repository.insertComplete(first, {}, {}, {}, &errorMessage);
    QVERIFY2(firstId.has_value(), qPrintable(errorMessage));
    const auto deletedId = repository.insertSerial(QStringLiteral("ABC-999"), &errorMessage);
    QVERIFY(deletedId.has_value());
    QVERIFY(repository.setDeleted(*deletedId, true, &errorMessage));

    darkeye::WorkSearch search;
    search.keyword = QStringLiteral("海边");
    search.order = darkeye::WorkSortOrder::SerialAscending;
    const QList<darkeye::WorkSummary> results = repository.search(search, &errorMessage);
    QCOMPARE(results.size(), 1);
    QCOMPARE(results.first().id, *firstId);
    QCOMPARE(results.first().serialNumber, QStringLiteral("ABC-010"));

    darkeye::Work updated = *repository.findById(*firstId, &errorMessage);
    updated.chineseTitle = QStringLiteral("更新后的标题");
    updated.runtime = 95;
    updated.notes = QStringLiteral("保留换行\n第二行");
    QVERIFY2(repository.updateDetails(updated, &errorMessage), qPrintable(errorMessage));
    const auto loaded = repository.findById(*firstId, &errorMessage);
    QVERIFY(loaded.has_value());
    QCOMPARE(loaded->serialNumber, QStringLiteral("ABC-010"));
    QCOMPARE(loaded->chineseTitle, QStringLiteral("更新后的标题"));
    QCOMPARE(loaded->runtime, std::optional<int>(95));
    QCOMPARE(loaded->notes, QStringLiteral("保留换行\n第二行"));
}

void WorkRepositoryTest::filtersRelationsAndPaginates()
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

    QSqlQuery seed(connection.database());
    QVERIFY(seed.exec(QStringLiteral("INSERT INTO actress DEFAULT VALUES")));
    const qint64 actressId = seed.lastInsertId().toLongLong();
    seed.prepare(QStringLiteral(
        "INSERT INTO actress_name(actress_id, name_type, cn, jp) VALUES(?, 1, ?, ?)"));
    seed.addBindValue(actressId);
    seed.addBindValue(QStringLiteral("关联女优"));
    seed.addBindValue(QStringLiteral("関連女優"));
    QVERIFY(seed.exec());
    QVERIFY(seed.exec(QStringLiteral("INSERT INTO actor DEFAULT VALUES")));
    const qint64 actorId = seed.lastInsertId().toLongLong();
    seed.prepare(QStringLiteral("INSERT INTO actor_name(actor_id, name_type, cn) VALUES(?, 1, ?)"));
    seed.addBindValue(actorId);
    seed.addBindValue(QStringLiteral("关联男优"));
    QVERIFY(seed.exec());
    QVERIFY(seed.exec(QStringLiteral("INSERT INTO tag(tag_name) VALUES('精确标签')")));
    const qint64 tagId = seed.lastInsertId().toLongLong();
    QVERIFY(seed.exec(QStringLiteral("INSERT INTO tag(tag_name) VALUES('第二标签')")));
    const qint64 secondTagId = seed.lastInsertId().toLongLong();
    QVERIFY(seed.exec(
        QStringLiteral("INSERT INTO maker(cn_name, jp_name) VALUES('目标片商', 'メーカー')")));
    const qint64 makerId = seed.lastInsertId().toLongLong();
    QVERIFY(seed.exec(
        QStringLiteral("INSERT INTO label(cn_name, jp_name) VALUES('目标厂牌', 'レーベル')")));
    const qint64 labelId = seed.lastInsertId().toLongLong();
    QVERIFY(seed.exec(
        QStringLiteral("INSERT INTO series(cn_name, jp_name) VALUES('目标系列', 'シリーズ')")));
    const qint64 seriesId = seed.lastInsertId().toLongLong();

    darkeye::WorkRepository repository(connection.database());
    darkeye::Work matching;
    matching.serialNumber = QStringLiteral("FILTER-001");
    matching.chineseTitle = QStringLiteral("筛选目标");
    matching.chineseStory = QStringLiteral("海边的故事内容");
    matching.notes = QStringLiteral("只在简单笔记中");
    matching.makerId = makerId;
    matching.labelId = labelId;
    matching.seriesId = seriesId;
    const auto matchingId = repository.insertComplete(matching, {actressId}, {actorId},
                                                      {tagId, secondTagId}, &errorMessage);
    QVERIFY2(matchingId.has_value(), qPrintable(errorMessage));
    darkeye::Work other;
    other.serialNumber = QStringLiteral("FILTER-002");
    other.chineseTitle = QStringLiteral("无关联作品");
    QVERIFY(repository.insertComplete(other, {}, {}, {}, &errorMessage).has_value());

    darkeye::WorkSearch search;
    search.actressName = QStringLiteral("関連");
    search.actorName = QStringLiteral("男优");
    search.tagName = QStringLiteral("精确");
    search.chineseStory = QStringLiteral("故事内容");
    search.notes = QStringLiteral("简单笔记");
    search.makerName = QStringLiteral("片商");
    search.labelName = QStringLiteral("レーベル");
    search.seriesName = QStringLiteral("系列");
    search.tagIds = {tagId, secondTagId};
    const auto total = repository.count(search, &errorMessage);
    QVERIFY2(total.has_value(), qPrintable(errorMessage));
    QCOMPARE(*total, 1);
    const auto filtered = repository.search(search, &errorMessage);
    QCOMPARE(filtered.size(), 1);
    QCOMPARE(filtered.first().serialNumber, QStringLiteral("FILTER-001"));
    QVERIFY(repository.makerSuggestions().contains(QStringLiteral("目标片商")));
    QVERIFY(repository.labelSuggestions().contains(QStringLiteral("目标厂牌")));
    QVERIFY(repository.seriesSuggestions().contains(QStringLiteral("目标系列")));

    const auto details = repository.findDetailsById(*matchingId, &errorMessage);
    QVERIFY2(details.has_value(), qPrintable(errorMessage));
    QCOMPARE(details->makerName, QStringLiteral("目标片商"));
    QCOMPARE(details->labelName, QStringLiteral("目标厂牌"));
    QCOMPARE(details->seriesName, QStringLiteral("目标系列"));
    QCOMPARE(details->actresses.size(), 1);
    QCOMPARE(details->actresses.first().name, QStringLiteral("关联女优"));
    QCOMPARE(details->actors.size(), 1);
    QCOMPARE(details->actors.first().name, QStringLiteral("关联男优"));
    QCOMPARE(details->tags.size(), 2);

    darkeye::WorkSearch paged;
    // Own all three pagination rows instead of depending on initialization data.
    QVERIFY(repository.insertSerial(QStringLiteral("FILTER-000"), &errorMessage).has_value());
    paged.order = darkeye::WorkSortOrder::SerialAscending;
    paged.limit = 1;
    paged.offset = 1;
    const auto secondPage = repository.search(paged, &errorMessage);
    QCOMPARE(secondPage.size(), 1);
    QCOMPARE(secondPage.first().serialNumber, QStringLiteral("FILTER-001"));
    QCOMPARE(repository.count(paged, &errorMessage), std::optional<int>(3));
    paged.offset = 2;
    const auto thirdPage = repository.search(paged, &errorMessage);
    QCOMPARE(thirdPage.size(), 1);
    QCOMPARE(thirdPage.first().serialNumber, QStringLiteral("FILTER-002"));
}

void WorkRepositoryTest::filtersWorksWithLocalVideo()
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

    darkeye::WorkRepository repository(connection.database());
    darkeye::Work withVideo;
    withVideo.serialNumber = QStringLiteral("VIDEO-001");
    withVideo.videoUrl = QStringLiteral("C:/videos/VIDEO-001.mp4");
    QVERIFY2(repository.insertComplete(withVideo, {}, {}, {}, &errorMessage).has_value(),
             qPrintable(errorMessage));
    darkeye::Work withoutVideo;
    withoutVideo.serialNumber = QStringLiteral("VIDEO-002");
    withoutVideo.videoUrl = QStringLiteral("   ");
    QVERIFY2(repository.insertComplete(withoutVideo, {}, {}, {}, &errorMessage).has_value(),
             qPrintable(errorMessage));

    darkeye::WorkSearch search;
    search.requireLocalVideo = true;
    search.order = darkeye::WorkSortOrder::SerialAscending;
    QCOMPARE(repository.count(search, &errorMessage), std::optional<int>(1));
    const QList<darkeye::WorkSummary> results = repository.search(search, &errorMessage);
    QCOMPARE(results.size(), 1);
    QCOMPARE(results.first().serialNumber, QStringLiteral("VIDEO-001"));
}

QTEST_MAIN(WorkRepositoryTest)
#include "WorkRepositoryTest.moc"
