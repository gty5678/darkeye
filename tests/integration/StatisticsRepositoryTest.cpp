#include "QtTestEntry.h"

#include "database/SchemaManager.h"
#include "database/repositories/PersonRepository.h"
#include "database/repositories/PrivateRepository.h"
#include "database/repositories/StatisticsRepository.h"
#include "database/repositories/WorkRepository.h"
#include "darkeye_ui/components/Charts.h"
#include "ui/pages/PersonalDataPanels.h"
#include "ui/pages/StatisticsChartPage.h"
#include "darkeye_ui/theme/ThemeService.h"

#include <QApplication>
#include <QComboBox>
#include <QDateTime>
#include <QDir>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QSqlQuery>
#include <QSqlError>
#include <QTemporaryDir>
#include <QtTest>

class StatisticsRepositoryTest final : public QObject
{
    Q_OBJECT

private slots:
    void reproducesOverviewAndPersonalStatistics();
    void rendersPersonalDataPanels();
    void shootingAgeHistogramKeepsBarsVisibleUnderCurve();
};

void StatisticsRepositoryTest::reproducesOverviewAndPersonalStatistics()
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
    darkeye::StatisticsRepository statistics(publicConnection.database(),
                                              privateConnection.database());
    QVERIFY(!statistics.favoriteUnwatchedSalesCycle(&errorMessage).has_value());
    const QString now = QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm"));
    QVERIFY(privateData.addMasturbationRecord(*workId, work.serialNumber, now,
                                              QStringLiteral("tool"), 5, QString(), &errorMessage));
    QVERIFY(privateData.addSexualArousalRecord(QStringLiteral("2020-01-02 06:00"), QString(),
                                               &errorMessage));

    const auto overview = statistics.overview(&errorMessage);
    QVERIFY2(overview.has_value(), qPrintable(errorMessage));
    QCOMPARE(overview->workCount, 2);
    QCOMPARE(overview->actressCount, 1);
    QCOMPARE(overview->actorCount, 1);
    QCOMPARE(overview->favoriteWorkCount, 2);
    QCOMPARE(overview->favoriteActressCount, 1);
    const auto top = statistics.topActress(30, &errorMessage);
    QVERIFY2(top.has_value(), qPrintable(errorMessage));
    QCOMPARE(top->actressId, *actressId);
    QCOMPARE(top->name, QStringLiteral("统计演员"));
    QCOMPARE(top->recordCount, 1);
    QCOMPARE(statistics.recordCountInDays(90, darkeye::PersonalRecordKind::Masturbation,
                                          &errorMessage),
             1);
    QCOMPARE(statistics.favoriteUnwatchedSalesCycle(&errorMessage), std::optional<int>(90));
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
        const auto current = statistics.overview(&errorMessage);
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
    const QString historicalTime =
        QDateTime::currentDateTime().addYears(-2).toString(QStringLiteral("yyyy-MM-dd HH:mm"));
    QVERIFY(privateData.addMasturbationRecord(*workId, work.serialNumber, historicalTime,
                                              QStringLiteral("tool"), 5, QString(), &errorMessage));
    const auto historicalTop = statistics.topActress(std::nullopt, &errorMessage);
    QVERIFY2(historicalTop.has_value(), qPrintable(errorMessage));
    QCOMPARE(historicalTop->actressId, *actressId);
    QCOMPARE(historicalTop->recordCount, 2);
    const auto lastYearTop = statistics.topActress(365, &errorMessage);
    QVERIFY2(lastYearTop.has_value(), qPrintable(errorMessage));
    QCOMPARE(lastYearTop->recordCount, 1);

}

void StatisticsRepositoryTest::shootingAgeHistogramKeepsBarsVisibleUnderCurve()
{
    darkeye::SqliteConnection publicConnection;
    QString error;
    QVERIFY2(publicConnection.open(QStringLiteral(":memory:"), false, &error),
             qPrintable(error));
    QVERIFY2(darkeye::SchemaManager::initializeEmptyDatabase(
                 publicConnection, darkeye::DatabaseKind::Public, &error),
             qPrintable(error));
    QSqlQuery fixture(publicConnection.database());
    QVERIFY(fixture.exec(QStringLiteral("INSERT INTO actress (actress_id, birthday) VALUES (1,'2000-01-01')")));
    QVERIFY(fixture.exec(QStringLiteral("INSERT INTO work (work_id, serial_number, release_date) "
                                        "VALUES (1,'STATS-001','2020-07-01')")));
    QVERIFY(fixture.exec(QStringLiteral("INSERT INTO work_actress_relation (work_id, actress_id) VALUES (1,1)")));

    darkeye::ThemeService themes(*qobject_cast<QApplication *>(QCoreApplication::instance()));
    QVERIFY(themes.setTheme(darkeye::ThemeId::Light));
    darkeye::StatisticsChartPage page(publicConnection.database(), QSqlDatabase(), themes);
    page.resize(900, 600);
    page.show();
    auto *chart = page.captureContent();
    QVERIFY(chart);
    QComboBox *selector = nullptr;
    for (auto *combo : page.findChildren<QComboBox *>()) {
        if (combo->accessibleName() == QStringLiteral("统计项")) {
            selector = combo;
            break;
        }
    }
    QVERIFY(selector);
    const int shootingAge = selector->findText(QStringLiteral("拍摄年龄分布"));
    QVERIFY(shootingAge >= 0);
    selector->setCurrentIndex(shootingAge);
    QApplication::processEvents();

    const QPixmap screenshot = chart->grab();
    QVERIFY(!screenshot.isNull());
    const QImage image = screenshot.toImage();
    const qreal dpr = screenshot.devicePixelRatio();
    const QRect plot = chart->rect().adjusted(88, 85, -48, -68);
    // The single age falls in [19 years 10 months, 20 years 2 months).
    // Sample away from the curve,
    // percentile marker and gridlines, including the adjacent empty bin.
    const auto pixelAtAge = [&](qreal age) {
        const int x = qRound((plot.left() + plot.width() * (age - 16.5) / 24) * dpr);
        const int y = qRound((plot.bottom() - plot.height() * .15) * dpr);
        return image.pixelColor(x, y);
    };
    const QColor background(themes.currentTokens().background);
    QVERIFY(pixelAtAge(20.08) != background);
    QCOMPARE(pixelAtAge(20.25), background);
}

void StatisticsRepositoryTest::rendersPersonalDataPanels()
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
    QVERIFY(themes.setTheme(darkeye::ThemeId::Light));
    darkeye::LibraryOverviewPage overview(publicConnection.database(), privateConnection.database(), themes);
    darkeye::TopActressesPage favorites(publicConnection.database(), privateConnection.database(),
                                       themes, temporaryDirectory.path());
    darkeye::RecordHeatmapPage heatmap(publicConnection.database(), privateConnection.database(), themes);
    for (auto *panel : {static_cast<QWidget *>(&overview), static_cast<QWidget *>(&favorites),
                        static_cast<QWidget *>(&heatmap)}) {
        panel->resize(1200, 320);
        panel->show();
    }
    QApplication::processEvents();

    const auto counts = overview.findChildren<QWidget *>(QStringLiteral("StatisticsOverviewCard"));
    QCOMPARE(counts.size(), 7);
    const auto background = QColor(themes.currentTokens().pageBackground);
    const auto surface = QColor(themes.currentTokens().background);
    const auto *firstCount = overview.findChild<QLabel *>(QStringLiteral("StatisticValue_0"));
    QVERIFY(firstCount);
    QCOMPARE(firstCount->text(), QStringLiteral("0"));
    QCOMPARE(firstCount->font().pixelSize(), 26);
    auto *countCard = firstCount->parentWidget();
    const auto countImage = countCard->grab();
    const qreal countDpr = countImage.devicePixelRatio();
    QCOMPARE(countImage.toImage().pixelColor(qRound(2 * countDpr), qRound(2 * countDpr)), background);
    QCOMPARE(countImage.toImage().pixelColor(qRound(5 * countDpr), qRound(20 * countDpr)), surface);

    QVERIFY(heatmap.findChild<QWidget *>(QStringLiteral("PersonalRecordHeatmap")));
    QVERIFY(heatmap.findChild<QPushButton *>(QStringLiteral("RecordKindPrevious")));
    QVERIFY(heatmap.findChild<QPushButton *>(QStringLiteral("RecordKindNext")));
    QVERIFY(heatmap.findChild<QWidget *>(QStringLiteral("RecordYearButtonList")));
    const auto cards = favorites.findChildren<QWidget *>(QStringLiteral("PersonalSummaryCard"));
    QCOMPARE(cards.size(), 4);
    bool hasHistoricalCard = false;
    for (auto *label : favorites.findChildren<QLabel *>())
    {
        if (label->text() == QStringLiteral("历史最喜欢的女优"))
            hasHistoricalCard = true;
    }
    QVERIFY(hasHistoricalCard);
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
    if (!previewPath.isEmpty()) QVERIFY(overview.grab().save(previewPath));

    overview.resize(620, 112);
    auto *overviewScroll = overview.findChild<QScrollArea *>();
    QVERIFY(overviewScroll);
    QTRY_VERIFY(overviewScroll->horizontalScrollBar()->maximum() > 0);
    QCOMPARE(overviewScroll->verticalScrollBar()->maximum(), 0);
}

DARKEYE_TEST_MAIN(StatisticsRepositoryTest, QApplication)
#include "StatisticsRepositoryTest.moc"
