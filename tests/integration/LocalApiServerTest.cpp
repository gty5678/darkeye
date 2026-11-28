#include "http/LocalApiServer.h"

#include "database/SchemaManager.h"
#include "database/repositories/WorkRepository.h"

#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

class LocalApiServerTest final : public QObject
{
    Q_OBJECT

private slots:
    void checksSerialExistenceUsingWorkRepository();
    void acceptsDirectWorkCapture_data();
    void acceptsDirectWorkCapture();
    void cloudflareNotificationAcceptsMosaicPayload_data();
    void cloudflareNotificationAcceptsMosaicPayload();
};

void LocalApiServerTest::checksSerialExistenceUsingWorkRepository()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    darkeye::SqliteConnection connection;
    QString error;
    QVERIFY2(connection.open(directory.filePath(QStringLiteral("public.db")), false, &error),
             qPrintable(error));
    QVERIFY2(darkeye::SchemaManager::initializeEmptyDatabase(
                 connection, darkeye::DatabaseKind::Public, &error), qPrintable(error));

    darkeye::WorkRepository works(connection.database());
    QVERIFY2(works.insertSerial(QStringLiteral("ABC-123"), &error).has_value(), qPrintable(error));
    const auto deletedId = works.insertSerial(QStringLiteral("ABC-456"), &error);
    QVERIFY2(deletedId.has_value(), qPrintable(error));
    QVERIFY2(works.setDeleted(*deletedId, true, &error), qPrintable(error));

    darkeye::LocalApiServer server([&works](const QStringList &serials, QString *lookupError)
    {
        return works.existingSerials(serials, lookupError);
    });
    QVERIFY2(server.start(0, &error), qPrintable(error));

    QNetworkAccessManager network;
    QNetworkRequest request(QUrl(QStringLiteral("http://127.0.0.1:%1/api/v1/check_existence")
                                    .arg(server.port())));
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    auto *reply = network.post(request, QByteArray(R"({"items":[" abc-123 ","ABC-123","abc-456","missing"]})"));
    QTRY_VERIFY(reply->isFinished());
    QCOMPARE(reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt(), 200);
    const QJsonObject expected{{QStringLiteral(" abc-123 "), true},
                               {QStringLiteral("ABC-123"), true},
                               {QStringLiteral("abc-456"), true},
                               {QStringLiteral("missing"), false}};
    QCOMPARE(QJsonDocument::fromJson(reply->readAll()).object()
                 .value(QStringLiteral("results")).toObject(), expected);
}

void LocalApiServerTest::acceptsDirectWorkCapture_data()
{
    QTest::addColumn<QByteArray>("payload");
    QTest::addColumn<int>("statusCode");
    QTest::addColumn<QString>("serial");
    QTest::newRow("valid") << QByteArray(R"({"content":" ABC-123 "})")
                            << 200 << QStringLiteral("ABC-123");
    QTest::newRow("blank") << QByteArray(R"({"content":"  "})") << 422 << QString();
}

void LocalApiServerTest::acceptsDirectWorkCapture()
{
    QFETCH(QByteArray, payload);
    QFETCH(int, statusCode);
    QFETCH(QString, serial);
    darkeye::LocalApiServer server({});
    QString error;
    QVERIFY2(server.start(0, &error), qPrintable(error));
    QSignalSpy captures(&server, &darkeye::LocalApiServer::captureOneReceived);

    QNetworkAccessManager network;
    QNetworkRequest request(QUrl(QStringLiteral("http://127.0.0.1:%1/api/v1/capture/one")
                                    .arg(server.port())));
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    auto *reply = network.post(request, payload);
    QTRY_VERIFY(reply->isFinished());
    QCOMPARE(reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt(), statusCode);
    if (serial.isEmpty())
    {
        QVERIFY(captures.isEmpty());
        return;
    }
    QCOMPARE(captures.size(), 1);
    QCOMPARE(captures.first(), (QVariantList{serial}));
    QCOMPARE(QJsonDocument::fromJson(reply->readAll()).object().value(QStringLiteral("status")).toString(),
             QStringLiteral("success"));
}

void LocalApiServerTest::cloudflareNotificationAcceptsMosaicPayload_data()
{
    QTest::addColumn<QByteArray>("payload");
    QTest::newRow("current") << QByteArray(R"({"site": " JavDB ", "serial": " ABC-123 "})");
    QTest::newRow("legacy") << QByteArray(R"({
        "site": " JavDB ", "serial": " ABC-123 ", "phase": "start",
        "url": "https://example.test/", "merge_request_id": "test-request"
    })");
}

void LocalApiServerTest::cloudflareNotificationAcceptsMosaicPayload()
{
    QFETCH(QByteArray, payload);
    darkeye::LocalApiServer server({});
    QString error;
    QVERIFY2(server.start(0, &error), qPrintable(error));
    QSignalSpy notifications(&server, &darkeye::LocalApiServer::cloudflareChallengeReceived);

    QNetworkAccessManager network;
    QNetworkRequest request(QUrl(QStringLiteral("http://127.0.0.1:%1/api/v1/cloudflare-challenge-notify")
                                    .arg(server.port())));
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    auto *reply = network.post(request, payload);
    QTRY_VERIFY(reply->isFinished());
    QCOMPARE(reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt(), 200);
    QCOMPARE(QJsonDocument::fromJson(reply->readAll()).object().value(QStringLiteral("status")).toString(),
             QStringLiteral("success"));
    QCOMPARE(notifications.size(), 1);
    QCOMPARE(notifications.first(), (QVariantList{QStringLiteral("JavDB"), QStringLiteral("ABC-123")}));
}

QTEST_GUILESS_MAIN(LocalApiServerTest)
#include "LocalApiServerTest.moc"
