#include "http/LocalApiServer.h"

#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QSignalSpy>
#include <QTest>

class LocalApiServerTest final : public QObject
{
    Q_OBJECT

private slots:
    void cloudflareNotificationAcceptsMosaicPayload_data();
    void cloudflareNotificationAcceptsMosaicPayload();
};

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
