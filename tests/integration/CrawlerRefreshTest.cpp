#include "crawler/CrawlerScheduler.h"
#include "database/SchemaManager.h"
#include "database/SqliteConnection.h"
#include "database/repositories/WorkRepository.h"
#include "services/CrawlerPersistenceService.h"
#include "ui/pages/InboxPage.h"

#include <QAbstractItemModel>
#include <QListWidget>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTimer>
#include <QtTest>

class CrawlerRefreshTest final : public QObject
{
    Q_OBJECT
private slots:
    void retainsHistoryAndAvoidsUnchangedRebuilds();
    void snapshotRepairsMissedQueueSignal();
    void publishesCommittedChangesOnly();
    void persistsWholeTitleMappedTags();
};

void CrawlerRefreshTest::retainsHistoryAndAvoidsUnchangedRebuilds()
{
    darkeye::CrawlerScheduler scheduler(QUrl(QStringLiteral("http://127.0.0.1:1")));
    scheduler.pause();
    darkeye::CrawlerPersistenceService persistence({}, {}, {}, darkeye::TranslationSettings{});
    darkeye::InboxPage page(scheduler, persistence);
    page.initialize();
    page.resize(1000, 400);
    page.show();
    auto *completed = page.findChild<QListWidget *>(QStringLiteral("inboxCompletedList"));
    QVERIFY(completed);
    for (int index = 0; index < 60; ++index)
        persistence.finished(QStringLiteral("TEST-%1").arg(index), true, {});
    QCOMPARE(completed->count(), 60);
    completed->doItemsLayout();
    QVERIFY(completed->verticalScrollBar()->maximum() > 0);
    completed->verticalScrollBar()->setValue(completed->verticalScrollBar()->maximum() / 2);
    const int position = completed->verticalScrollBar()->value();
    QSignalSpy resets(completed->model(), &QAbstractItemModel::modelReset);
    auto *timer = page.findChild<QTimer *>(QStringLiteral("inboxSnapshotTimer"));
    QVERIFY(timer);
    QCOMPARE(timer->interval(), 1000);
    QVERIFY(QMetaObject::invokeMethod(timer, "timeout", Qt::DirectConnection));
    scheduler.queueChanged();
    QCOMPARE(resets.count(), 0);
    QCOMPARE(completed->verticalScrollBar()->value(), position);
    persistence.finished(QStringLiteral("TEST-60"), true, {});
    QCOMPARE(completed->count(), 61);
    QCOMPARE(completed->verticalScrollBar()->value(), position);
    resets.clear();
    persistence.completenessChanged(QStringLiteral("TEST-0"), {{QStringLiteral("cover"), true}});
    QVERIFY(resets.count() > 0);
    QCOMPARE(completed->verticalScrollBar()->value(), position);
    resets.clear();
    persistence.completenessChanged(QStringLiteral("TEST-0"), {{QStringLiteral("cover"), true}});
    QCOMPARE(resets.count(), 0);
}

void CrawlerRefreshTest::snapshotRepairsMissedQueueSignal()
{
    darkeye::CrawlerScheduler scheduler(QUrl(QStringLiteral("http://127.0.0.1:1")));
    scheduler.pause();
    darkeye::CrawlerPersistenceService persistence({}, {}, {}, darkeye::TranslationSettings{});
    darkeye::InboxPage page(scheduler, persistence);
    page.initialize();
    auto *pending = page.findChild<QListWidget *>(QStringLiteral("inboxPendingList"));
    QVERIFY(pending);
    {
        const QSignalBlocker blocker(&scheduler);
        scheduler.enqueue({QStringLiteral("TEST-001")});
        scheduler.pause();
    }
    QCOMPARE(pending->count(), 0);
    QTRY_COMPARE_WITH_TIMEOUT(pending->count(), 1, 2500);
    QVERIFY(pending->item(0)->text().contains(QStringLiteral("TEST-001")));
    QSignalSpy resets(pending->model(), &QAbstractItemModel::modelReset);
    scheduler.queueChanged();
    QCOMPARE(resets.count(), 0);
}

void CrawlerRefreshTest::publishesCommittedChangesOnly()
{
    QTemporaryDir directory;
    darkeye::SqliteConnection connection;
    QString error;
    const QString databasePath = directory.filePath(QStringLiteral("public.db"));
    QVERIFY(connection.open(databasePath, false, &error));
    QVERIFY(darkeye::SchemaManager::initializeEmptyDatabase(
        connection, darkeye::DatabaseKind::Public, &error));
    darkeye::CrawlerPersistenceService persistence(databasePath, {}, {}, darkeye::TranslationSettings{});
    QSignalSpy works(&persistence, &darkeye::CrawlerPersistenceService::worksChanged);
    QSignalSpy actresses(&persistence, &darkeye::CrawlerPersistenceService::actressesChanged);
    QSignalSpy actors(&persistence, &darkeye::CrawlerPersistenceService::actorsChanged);
    QSignalSpy finished(&persistence, &darkeye::CrawlerPersistenceService::finished);
    bool committedAtSignal = false;
    connect(&persistence, &darkeye::CrawlerPersistenceService::worksChanged, this, [&] {
        committedAtSignal = darkeye::WorkRepository(connection.database())
            .findIdBySerial(QStringLiteral("TEST-001")).has_value();
    });
    persistence.persist(QStringLiteral("TEST-001"),
                        {{QStringLiteral("cn_title"), QStringLiteral("Title")}},
                        {QStringLiteral("cn_title")});
    QTRY_COMPARE(finished.count(), 1);
    QVERIFY(finished.first().at(1).toBool());
    QVERIFY(committedAtSignal);
    QCOMPARE(works.count(), 1);
    QCOMPARE(actresses.count(), 1);
    QCOMPARE(actors.count(), 1);

    darkeye::CrawlerPersistenceService invalid(directory.filePath(QStringLiteral("missing.db")),
                                               {}, {}, darkeye::TranslationSettings{});
    QSignalSpy invalidChanges(&invalid, &darkeye::CrawlerPersistenceService::worksChanged);
    QSignalSpy failed(&invalid, &darkeye::CrawlerPersistenceService::finished);
    invalid.persist(QStringLiteral("TEST-002"), {}, {QStringLiteral("cn_title")});
    QTRY_COMPARE(failed.count(), 1);
    QVERIFY(!failed.first().at(1).toBool());
    QCOMPARE(invalidChanges.count(), 0);
}

void CrawlerRefreshTest::persistsWholeTitleMappedTags()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    darkeye::SqliteConnection connection;
    QString error;
    const QString path = directory.filePath(QStringLiteral("public.db"));
    QVERIFY(connection.open(path, false, &error));
    QVERIFY(darkeye::SchemaManager::initializeEmptyDatabase(
        connection, darkeye::DatabaseKind::Public, &error));
    darkeye::CrawlerPersistenceService persistence(path, {}, {}, darkeye::TranslationSettings{});
    QSignalSpy finished(&persistence, &darkeye::CrawlerPersistenceService::finished);
    persistence.persist(QStringLiteral("MAPPED-001"),
        {{QStringLiteral("jp_title"), QStringLiteral("内射")}},
        {QStringLiteral("jp_title"), QStringLiteral("tag")});
    QTRY_COMPARE(finished.count(), 1);
    QVERIFY2(finished.first().at(1).toBool(), qPrintable(finished.first().at(2).toString()));
    darkeye::WorkRepository works(connection.database());
    const auto id = works.findIdBySerial(QStringLiteral("MAPPED-001"), &error);
    QVERIFY(id.has_value());
    const auto details = works.findDetailsById(*id, &error);
    QVERIFY(details.has_value());
    QStringList names;
    for (const auto &tag : details->tags) names.append(tag.name);
    QVERIFY(names.contains(QStringLiteral("中出")));
    QVERIFY(!names.contains(QStringLiteral("中")));
    QVERIFY(!names.contains(QStringLiteral("出")));
}

QTEST_MAIN(CrawlerRefreshTest)
#include "CrawlerRefreshTest.moc"
