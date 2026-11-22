#include "database/DatabaseMaintenanceService.h"
#include "database/SqliteConnection.h"
#include "database/WebDavBackupService.h"
#include "database/WebDavCredentialStore.h"

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSqlQuery>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QUuid>
#include <QtTest>
#include <QtCore/private/qzipreader_p.h>

class SnapshotUploadTest final : public QObject
{
    Q_OBJECT
private slots:
    void snapshotWorkflow_data();
    void snapshotWorkflow();
    void compressionFailurePreservesSnapshot();
    void snapshotFailureDoesNotUpload();
};

void SnapshotUploadTest::snapshotWorkflow_data()
{
    QTest::addColumn<bool>("autoUpload");
    QTest::addColumn<bool>("enabled");
    QTest::addColumn<int>("putStatus");
    QTest::newRow("local-only") << false << false << 201;
    QTest::newRow("enabled-without-auto-upload") << false << true << 201;
    QTest::newRow("auto-upload-disabled-cloud") << true << false << 201;
    QTest::newRow("auto-upload-missing-credentials") << true << true << 0;
    QTest::newRow("auto-upload-success") << true << true << 201;
    QTest::newRow("auto-upload-failure") << true << true << 507;
}

void SnapshotUploadTest::snapshotWorkflow()
{
    QFETCH(bool, autoUpload);
    QFETCH(bool, enabled);
    QFETCH(int, putStatus);
    QTemporaryDir root;
    QVERIFY(root.isValid());
    darkeye::SqliteConnection database;
    QString error;
    QVERIFY2(database.open(root.filePath(QStringLiteral("source.db")), false, &error), qPrintable(error));
    QSqlQuery query(database.database());
    QVERIFY(query.exec(QStringLiteral("CREATE TABLE sample(value TEXT)")));
    QVERIFY(query.exec(QStringLiteral("INSERT INTO sample VALUES('snapshot')")));
    const QString covers = root.filePath(QStringLiteral("covers"));
    QVERIFY(QDir().mkpath(covers + QStringLiteral("/nested")));
    const QString imageName = QStringLiteral("nested/封面.jpg");
    QFile image(QDir(covers).filePath(imageName));
    QVERIFY(image.open(QIODevice::WriteOnly));
    const QByteArray imageData(4096, 'x');
    QCOMPARE(image.write(imageData), qint64(imageData.size()));
    image.close();

    QTcpServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost));
    QByteArray uploadedBody;
    QByteArray uploadedTarget;
    int requests = 0;
    connect(&server, &QTcpServer::newConnection, this, [&] {
        QTcpSocket *socket = server.nextPendingConnection();
        connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
        connect(socket, &QTcpSocket::readyRead, socket, [&, socket] {
            const QByteArray request = socket->property("request").toByteArray() + socket->readAll();
            socket->setProperty("request", request);
            const qsizetype headerEnd = request.indexOf("\r\n\r\n");
            if (headerEnd < 0 || socket->property("handled").toBool()) return;
            qint64 length = 0;
            for (const QByteArray &header : request.left(headerEnd).split('\n'))
                if (header.toLower().startsWith("content-length:")) length = header.mid(15).trimmed().toLongLong();
            if (request.size() - headerEnd - 4 < length) return;
            socket->setProperty("handled", true);
            ++requests;
            int status = 207;
            if (request.startsWith("PUT "))
            {
                uploadedBody = request.mid(headerEnd + 4, length);
                uploadedTarget = request.split(' ').at(1);
                status = putStatus;
            }
            socket->write("HTTP/1.1 " + QByteArray::number(status) +
                          " Result\r\nContent-Length: 0\r\nConnection: close\r\n\r\n");
            socket->disconnectFromHost();
        });
    });
    darkeye::CrawlerSettings::WebDav settings;
    settings.autoUploadOnBackup = autoUpload;
    settings.enabled = enabled;
    settings.baseUrl = QUrl(QStringLiteral("http://127.0.0.1:%1").arg(server.serverPort()));
    settings.remoteRoot = QStringLiteral("/snapshots");
    settings.profileName = QStringLiteral("snapshot-test-%1").arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
    struct CredentialCleanup {
        QString profile;
        ~CredentialCleanup() { if (!profile.isEmpty()) darkeye::WebDavCredentialStore::clear(profile); }
    } cleanup;
    if (autoUpload && enabled && putStatus != 0)
    {
        QVERIFY2(darkeye::WebDavCredentialStore::save(
                     settings.profileName, {QStringLiteral("test"), QStringLiteral("test")}, &error), qPrintable(error));
        cleanup.profile = settings.profileName;
    }
    const auto result = darkeye::WebDavBackupService::uploadPublicSnapshot(
        database.database(), root.filePath(QStringLiteral("backups")), covers,
        root.filePath(QStringLiteral("fanart")), root.filePath(QStringLiteral("actresses")),
        root.filePath(QStringLiteral("actors")), settings);
    QCOMPARE(result.succeeded, !autoUpload || (enabled && putStatus == 201));
    QVERIFY2(result.localPath.endsWith(QStringLiteral(".zip")), qPrintable(result.message));
    QVERIFY(QFileInfo(result.localPath).isFile());
    const QString snapshotDirectory = result.localPath.chopped(4);
    QVERIFY(QFileInfo(snapshotDirectory).isDir());
    QZipReader archive(result.localPath);
    QCOMPARE(archive.status(), QZipReader::NoError);
    const QByteArray metadataJson = archive.fileData(QStringLiteral("meta.json"));
    QVERIFY(!metadataJson.isEmpty());
    const auto metadata = QJsonDocument::fromJson(metadataJson).object();
    const QString databaseName = metadata.value(QStringLiteral("db")).toObject().value(QStringLiteral("file")).toString();
    QVERIFY(!databaseName.isEmpty());
    QFile snapshotDb(QDir(snapshotDirectory).filePath(databaseName));
    QVERIFY(snapshotDb.open(QIODevice::ReadOnly));
    QCOMPARE(archive.fileData(databaseName), snapshotDb.readAll());
    // QZipReader looks up entries using local bytes; the ZIP itself uses UTF-8,
    // as required by Python's zipfile and indicated by bit 11 of the flags.
    const QByteArray entryName = (QStringLiteral("workcovers/") + imageName).toUtf8();
    QCOMPARE(archive.fileData(QString::fromLocal8Bit(entryName)), imageData);
    QCOMPARE(archive.count(), 3);
    QFile zip(result.localPath);
    QVERIFY(zip.open(QIODevice::ReadOnly));
    const QByteArray zipData = zip.readAll();
    QVERIFY(zipData.contains(entryName));
    QVERIFY(zipData.size() < imageData.size());
    if (autoUpload && enabled && putStatus != 0)
    {
        QCOMPARE(uploadedBody, zipData);
        QCOMPARE(uploadedTarget, (QStringLiteral("/snapshots/") + QFileInfo(result.localPath).fileName()).toUtf8());
        QCOMPARE(result.remotePath, QString::fromUtf8(uploadedTarget));
        if (putStatus != 201) QVERIFY(result.message.contains(QStringLiteral("ZIP 已保存")));
    }
    else QCOMPARE(requests, 0);
    // A repeated backup in the same second must not overwrite existing files.
    const auto repeated = darkeye::DatabaseMaintenanceService::compressPublicSnapshot(snapshotDirectory);
    QVERIFY(!repeated.succeeded);
    zip.seek(0);
    QCOMPARE(zip.readAll(), zipData);
}

void SnapshotUploadTest::compressionFailurePreservesSnapshot()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    const QString snapshot = root.filePath(QStringLiteral("snapshot"));
    QVERIFY(QDir().mkpath(snapshot));
    QFile meta(QDir(snapshot).filePath(QStringLiteral("meta.json")));
    QVERIFY(meta.open(QIODevice::WriteOnly));
    QCOMPARE(meta.write("{}"), qint64(2));
    meta.close();
    QVERIFY(QDir().mkpath(snapshot + QStringLiteral(".zip")));
    const auto failed = darkeye::DatabaseMaintenanceService::compressPublicSnapshot(snapshot);
    QVERIFY(!failed.succeeded);
    QVERIFY(meta.exists());
    QVERIFY(QDir().rmdir(snapshot + QStringLiteral(".zip")));
    QFile oversized(QDir(snapshot).filePath(QStringLiteral("oversized.db")));
    QVERIFY(oversized.open(QIODevice::WriteOnly));
    // Sparse resize exercises the ZIP32 guard without allocating 4 GiB in memory.
    QVERIFY(oversized.resize(0xffffffffLL));
    oversized.close();
    const auto tooLarge = darkeye::DatabaseMaintenanceService::compressPublicSnapshot(snapshot);
    QVERIFY(!tooLarge.succeeded);
    QVERIFY(tooLarge.message.contains(QStringLiteral("容量")));
    QVERIFY(!QFileInfo::exists(snapshot + QStringLiteral(".zip")));
    QVERIFY(meta.exists());
    QVERIFY(!darkeye::DatabaseMaintenanceService::compressPublicSnapshot(root.filePath(QStringLiteral("missing"))).succeeded);
}

void SnapshotUploadTest::snapshotFailureDoesNotUpload()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    darkeye::CrawlerSettings::WebDav settings;
    settings.enabled = true;
    settings.autoUploadOnBackup = true;
    const auto failed = darkeye::WebDavBackupService::uploadPublicSnapshot(
        {}, root.path(), {}, {}, {}, {}, settings);
    QVERIFY(!failed.succeeded);
    QVERIFY(failed.message.contains(QStringLiteral("数据库尚未打开")));
    QVERIFY(QDir(root.path()).entryList({QStringLiteral("*.zip")}, QDir::Files).isEmpty());
}

QTEST_GUILESS_MAIN(SnapshotUploadTest)
#include "SnapshotUploadTest.moc"
