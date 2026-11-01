#include "database/SchemaManager.h"
#include "database/repositories/PersonRepository.h"
#include "database/repositories/PrivateRepository.h"
#include "database/repositories/StatisticsRepository.h"
#include "database/repositories/WorkRepository.h"
#include "darkeye_ui/components/Charts.h"
#include "ui/components/StatsOverviewCards.h"
#include "ui/pages/DashboardPage.h"
#include "ui/pages/PersonalDataPage.h"
#include "darkeye_ui/theme/ThemeService.h"

#include <QApplication>
#include <QDateTime>
#include <QDir>
#include <QListWidget>
#include <QPushButton>
#include <QSqlQuery>
#include <QSqlError>
#include <QTemporaryDir>
#include <QtTest>

class StatisticsRepositoryTest final : public QObject
{
    Q_OBJECT

private slots:
    void reproducesDashboardAndPersonalStatistics();
    void rendersDashboardPage();
    void rendersPersonalDataPage();
};

void StatisticsRepositoryTest::reproducesDashboardAndPersonalStatistics()
{
    QTemporaryDir temporaryDirectory;
    QVERIFY(temporaryDirectory.isValid());
    darkeye::SqliteConnection publicConnection;
    darkeye::SqliteConnection privateConnection;
    QString errorMessage;
    QVERIFY2(publicConnection.open(QDir(temporaryDirectory.path()).filePath("public.db"), false,
                                   &errorMessage),
             qPrintable(errorMessage));
    QVERIFY2(privateConnection.open(QDir(temporaryDirectory.path()).filePath("private.db"), false,
                                    &errorMessage),
             qPrintable(errorMessage));
    QVERIFY2(darkeye::SchemaManager::initializeEmptyDatabase(
                 publicConnection, darkeye::DatabaseKind::Public, &errorMessage),
             qPrintable(errorMessage));
    QVERIFY2(darkeye::SchemaManager::initializeEmptyDatabase(
                 privateConnection, darkeye::DatabaseKind::Private, &errorMessage),
             qPrintable(errorMessage));

    darkeye::PersonRepository people(publicConnection.database());
    const auto actressId = people.create(darkeye::PersonKind::Actress,
                                         QStringLiteral("统计演员"),
                                         QStringLiteral("統計女優"), &errorMessage);
    QVERIFY2(actressId.has_value(), qPrintable(errorMessage));
    const auto actorId = people.create(darkeye::PersonKind::Actor, QStringLiteral("统计男演员"),
                                       QStringLiteral("統計男優"), &errorMessage);
    QVERIFY2(actorId.has_value(), qPrintable(errorMessage));
    darkeye::WorkRepository works(publicConnection.database());
    darkeye::Work work;
    work.serialNumber = QStringLiteral("STATS-001");
    work.releaseDate = QStringLiteral("2020-01-01");
    work.director = QStringLiteral("统计导演");
    const auto workId = works.insertComplete(work, {*actressId}, {*actorId}, {}, &errorMessage);
    QVERIFY2(workId.has_value(), qPrintable(errorMessage));
    const auto unwatchedId = works.insertSerial(QStringLiteral("STATS-002"), &errorMessage);
    QVERIFY2(unwatchedId.has_value(), qPrintable(errorMessage));

    darkeye::PrivateRepository privateData(privateConnection.database());
    QVERIFY(privateData.addFavoriteWork(*workId, work.serialNumber, &errorMessage));
    QVERIFY(privateData.addFavoriteWork(*unwatchedId, QStringLiteral("STATS-002"), &errorMessage));
    QVERIFY(privateData.addFavoriteActress(*actressId, QStringLiteral("統計女優"), &errorMessage));
    const QString now = QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm"));
    QVERIFY(privateData.addMasturbationRecord(*workId, work.serialNumber, now,
                                              QStringLiteral("tool"), 5, QString(), &errorMessage));
    QVERIFY(privateData.addSexualArousalRecord(QStringLiteral("2020-01-02 06:00"), QString(),
                                               &errorMessage));

    darkeye::StatisticsRepository statistics(publicConnection.database(),
                                              privateConnection.database());
    const auto dashboard = statistics.dashboard(&errorMessage);
    QVERIFY2(dashboard.has_value(), qPrintable(errorMessage));
    QCOMPARE(dashboard->workCount, 2);
    QCOMPARE(dashboard->actressCount, 1);
    QCOMPARE(dashboard->actorCount, 1);
    QCOMPARE(dashboard->favoriteWorkCount, 2);
    QCOMPARE(dashboard->favoriteActressCount, 1);
    const auto top = statistics.topActress(30, &errorMessage);
    QVERIFY2(top.has_value(), qPrintable(errorMessage));
    QCOMPARE(top->actressId, *actressId);
    QCOMPARE(top->name, QStringLiteral("统计演员"));
    QCOMPARE(top->recordCount, 1);
    QCOMPARE(statistics.recordCountInDays(90, darkeye::PersonalRecordKind::Masturbation,
                                          &errorMessage),
             1);
    QCOMPARE(statistics.favoriteUnwatchedSalesCycle(&errorMessage), 90);
    QCOMPARE(statistics.earliestRecordYear(&errorMessage), 2020);

    QSqlQuery fixture(publicConnection.database());
    QVERIFY2(fixture.exec(QStringLiteral("UPDATE actress SET birthday='2000-01-01'")),
             qPrintable(fixture.lastError().text()));
    QVERIFY(fixture.exec(QStringLiteral("INSERT INTO maker (cn_name) VALUES ('统计制作商')")));
    QVERIFY(fixture.exec(QStringLiteral("UPDATE work SET maker_id=(SELECT MAX(maker_id) FROM maker)")));

    // Soft deletion must exclude works in every work chart scope, even when
    // favorites and historical records still reference the deleted work.
    for (const bool deleted : {false, true, false})
    {
        QVERIFY(works.setDeleted(*workId, deleted, &errorMessage));
        const auto current = statistics.dashboard(&errorMessage);
        QVERIFY2(current.has_value(), qPrintable(errorMessage));
        QCOMPARE(current->workCount, deleted ? 1 : 2);
        QCOMPARE(current->recentThirtyDays, deleted ? 1 : 2);
        QCOMPARE(statistics.workAddedDates(&errorMessage).size(), deleted ? 1 : 2);
        for (int scope = -1; scope <= 2; ++scope)
        {
            errorMessage.clear();
            const auto years = statistics.workReleaseYears(scope, &errorMessage);
            QCOMPARE(years.size(), deleted ? 0 : 1);
            if (!years.isEmpty()) QCOMPARE(years.first().value, 1.0);
            QCOMPARE(statistics.topDirectors(scope, &errorMessage).size(), deleted ? 0 : 1);
            const auto makers = statistics.topMakers(scope, &errorMessage);
            QCOMPARE(makers.size(), deleted && scope >= 1 ? 0 : 1);
            if (!makers.isEmpty()) QCOMPARE(makers.first().value, scope <= 0 ? (deleted ? 1.0 : 2.0)
                                                                     : (deleted ? 0.0 : 1.0));
            QCOMPARE(statistics.workActressAges(scope, &errorMessage).size(), deleted ? 0 : 1);
            QVERIFY2(errorMessage.isEmpty(), qPrintable(errorMessage));
        }
    }

}

void StatisticsRepositoryTest::rendersDashboardPage()
{
    QTemporaryDir temporaryDirectory;
    QVERIFY(temporaryDirectory.isValid());
    darkeye::SqliteConnection publicConnection;
    darkeye::SqliteConnection privateConnection;
    QString errorMessage;
    QVERIFY2(publicConnection.open(QDir(temporaryDirectory.path()).filePath("public.db"), false,
                                   &errorMessage),
             qPrintable(errorMessage));
    QVERIFY2(privateConnection.open(QDir(temporaryDirectory.path()).filePath("private.db"), false,
                                    &errorMessage),
             qPrintable(errorMessage));
    QVERIFY(darkeye::SchemaManager::initializeEmptyDatabase(
        publicConnection, darkeye::DatabaseKind::Public, &errorMessage));
    QVERIFY(darkeye::SchemaManager::initializeEmptyDatabase(
        privateConnection, darkeye::DatabaseKind::Private, &errorMessage));

    darkeye::DashboardPage page(publicConnection.database(), privateConnection.database());
    page.resize(1000, 700);
    page.show();
    QApplication::processEvents();

    QVERIFY(page.findChild<QWidget *>(QStringLiteral("StatsOverviewCards")));
    const auto *recentView =
        page.findChild<QListWidget *>(QStringLiteral("DashboardRecentViewList"));
    const auto *recentAdded =
        page.findChild<QListWidget *>(QStringLiteral("DashboardRecentAddedList"));
    const auto *pending = page.findChild<QListWidget *>(QStringLiteral("DashboardPendingList"));
    QVERIFY(recentView);
    QVERIFY(recentAdded);
    QVERIFY(pending);
    QCOMPARE(recentView->count(), 2);
    QCOMPARE(recentAdded->count(), 2);
    QCOMPARE(pending->count(), 2);
    QCOMPARE(recentView->maximumHeight(), 200);
    QCOMPARE(pending->maximumHeight(), 140);
    QCOMPARE(recentView->item(0)->text(), QStringLiteral("（占位）最近看过的一部作品"));
    QCOMPARE(recentAdded->item(1)->text(), QStringLiteral("（占位）最近新增女优"));
    QCOMPARE(pending->item(1)->text(), QStringLiteral("（占位）8 部作品未绑定女优"));
}

void StatisticsRepositoryTest::rendersPersonalDataPage()
{
    QTemporaryDir temporaryDirectory;
    QVERIFY(temporaryDirectory.isValid());
    darkeye::SqliteConnection publicConnection;
    darkeye::SqliteConnection privateConnection;
    QString errorMessage;
    QVERIFY2(publicConnection.open(QDir(temporaryDirectory.path()).filePath("public.db"), false,
                                   &errorMessage),
             qPrintable(errorMessage));
    QVERIFY2(privateConnection.open(QDir(temporaryDirectory.path()).filePath("private.db"), false,
                                    &errorMessage),
             qPrintable(errorMessage));
    QVERIFY(darkeye::SchemaManager::initializeEmptyDatabase(
        publicConnection, darkeye::DatabaseKind::Public, &errorMessage));
    QVERIFY(darkeye::SchemaManager::initializeEmptyDatabase(
        privateConnection, darkeye::DatabaseKind::Private, &errorMessage));

    auto *application = qobject_cast<QApplication *>(QCoreApplication::instance());
    QVERIFY(application);
    darkeye::ThemeService themes(*application);
    darkeye::PersonalDataPage page(publicConnection.database(), privateConnection.database(),
                                    themes, temporaryDirectory.path());
    page.resize(1200, 700);
    page.show();
    QApplication::processEvents();
    QVERIFY(page.findChild<QWidget *>(QStringLiteral("StatsOverviewCards")));
    QVERIFY(page.findChild<QWidget *>(QStringLiteral("PersonalRecordHeatmap")));
    QVERIFY(page.findChild<QPushButton *>(QStringLiteral("RecordKindPrevious")));
    QVERIFY(page.findChild<QPushButton *>(QStringLiteral("RecordKindNext")));
    QVERIFY(page.findChild<QWidget *>(QStringLiteral("RecordYearButtonList")));
    const QPixmap snapshot = page.grab();
    QVERIFY(!snapshot.isNull());
    QCOMPARE(snapshot.deviceIndependentSize().toSize(), page.size());
    const auto cards = page.findChildren<QWidget *>(QStringLiteral("PersonalSummaryCard"));
    QCOMPARE(cards.size(), 4);
    for (auto *card : cards) {
        QVERIFY(card->graphicsEffect() == nullptr);
        QCOMPARE(card->size(), QSize(186, 266));
        const qreal dpr = card->devicePixelRatioF();
        // The shadow must extend into the reserved margin without painting
        // opaque corner pixels or capturing the child labels and avatars.
        QTRY_VERIFY_WITH_TIMEOUT(card->grab().toImage().pixelColor(
            qRound(card->width() * dpr / 2), qRound(7 * dpr)).alpha() > 0, 2000);
        const QImage image = card->grab().toImage();
        QCOMPARE(image.pixelColor(0, 0).alpha(), 0);
        QVERIFY(image.pixelColor(qRound(card->width() * dpr / 2), qRound(7 * dpr)).alpha() < 80);
    }
    const QString previewPath = qEnvironmentVariable("DARKEYE_TEST_SNAPSHOT_PATH");
    if (!previewPath.isEmpty()) QVERIFY(page.grab().save(previewPath));
}

QTEST_MAIN(StatisticsRepositoryTest)
#include "StatisticsRepositoryTest.moc"
