#include "database/SqliteConnection.h"
#include "services/VideoLibraryService.h"

#include <QFile>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QtTest>

class VideoLibraryServiceTest final : public QObject
{
    Q_OBJECT

private slots:
    void scansMissingSerialsAndSynchronizesPaths();
};

void VideoLibraryServiceTest::scansMissingSerialsAndSynchronizesPaths()
{
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    darkeye::SqliteConnection connection;
    QString error;
    QVERIFY2(connection.open(temporary.filePath(QStringLiteral("video.db")), false, &error),
             qPrintable(error));
    QSqlQuery query(connection.database());
    QVERIFY(query.exec(QStringLiteral(
        "CREATE TABLE work(work_id INTEGER PRIMARY KEY, serial_number TEXT, "
        "video_url TEXT, is_deleted INTEGER DEFAULT 0)")));
    QVERIFY(query.exec(QStringLiteral(
        "INSERT INTO work VALUES(1,'SONE-979','old-a.mp4',0),"
        "(2,'START-451','old-b.mp4',0),(3,'DELETED-1','old-d.mp4',1)")));

    const QString first = temporary.filePath(QStringLiteral("SONE-979-a.mp4"));
    const QString second = temporary.filePath(QStringLiteral("SONE-979-b.mkv"));
    const QString missing = temporary.filePath(QStringLiteral("NOPE-123.mp4"));
    const QString unknown = temporary.filePath(QStringLiteral("holiday.avi"));
    for (const QString &path : {first, second, missing, unknown}) {
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
    }

    darkeye::VideoLibraryService service(connection.database());
    const darkeye::VideoLibraryScanResult scan = service.scanMissingSerials({temporary.path()});
    QVERIFY2(scan.succeeded, qPrintable(scan.errorMessage));
    QCOMPARE(scan.scannedFiles, 4);
    QCOMPARE(scan.filesWithoutSerial.size(), 1);
    QCOMPARE(scan.missingSerials, QStringList({QStringLiteral("NOPE-123")}));

    const darkeye::VideoLibraryScanResult sync =
        service.synchronizeVideoUrls({temporary.path()});
    QVERIFY2(sync.succeeded, qPrintable(sync.errorMessage));
    QCOMPARE(sync.scannedFiles, 4);
    QCOMPARE(sync.unmatchedSerials, 1);
    QCOMPARE(sync.updatedWorks, 3);

    QVERIFY(query.exec(QStringLiteral("SELECT video_url FROM work WHERE work_id=1")));
    QVERIFY(query.next());
    const QStringList stored = query.value(0).toString().split(QLatin1Char(','));
    QCOMPARE(stored.size(), 2);
    QVERIFY(stored.contains(QFileInfo(first).canonicalFilePath()));
    QVERIFY(stored.contains(QFileInfo(second).canonicalFilePath()));
    QVERIFY(query.exec(QStringLiteral("SELECT COUNT(*) FROM work WHERE video_url IS NOT NULL")));
    QVERIFY(query.next());
    QCOMPARE(query.value(0).toInt(), 1);
}

QTEST_MAIN(VideoLibraryServiceTest)
#include "VideoLibraryServiceTest.moc"
