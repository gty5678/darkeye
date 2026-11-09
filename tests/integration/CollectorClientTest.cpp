#include "crawler/CollectorClient.h"
#include "database/SchemaManager.h"
#include "database/SqliteConnection.h"
#include "database/repositories/PersonRepository.h"
#include "services/TopActressSyncService.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QUrlQuery>
#include <QtTest>

namespace
{

class FakeCollector final : public QObject
{
public:
    explicit FakeCollector(QByteArray responseBody, int status = 200)
        : m_responseBody(std::move(responseBody)), m_status(status)
    {
        connect(&m_server, &QTcpServer::newConnection, this,
                [this]
                {
                    QTcpSocket *socket = m_server.nextPendingConnection();
                    connect(socket, &QTcpSocket::readyRead, this,
                            [this, socket]
                            {
                                m_request += socket->readAll();
                                if (!m_request.contains("\r\n\r\n"))
                                    return;
                                const QByteArray reason = m_status == 200
                                                              ? QByteArrayLiteral("OK")
                                                              : QByteArrayLiteral("Error");
                                const QByteArray response =
                                    QByteArrayLiteral("HTTP/1.1 ") + QByteArray::number(m_status) +
                                    QByteArrayLiteral(" ") + reason +
                                    QByteArrayLiteral("\r\nContent-Type: application/json\r\n"
                                                      "Connection: close\r\nContent-Length: ") +
                                    QByteArray::number(m_responseBody.size()) +
                                    QByteArrayLiteral("\r\n\r\n") + m_responseBody;
                                socket->write(response);
                                socket->disconnectFromHost();
                            });
                });
        if (!m_server.listen(QHostAddress::LocalHost, 0))
            qFatal("Cannot start fake collector: %s", qPrintable(m_server.errorString()));
    }

    [[nodiscard]] QUrl url(const QString &path) const
    {
        return QUrl(QStringLiteral("http://127.0.0.1:%1%2").arg(m_server.serverPort()).arg(path));
    }

    [[nodiscard]] QByteArray requestLine() const
    {
        return m_request.left(m_request.indexOf("\r\n"));
    }

private:
    QTcpServer m_server;
    QByteArray m_responseBody;
    QByteArray m_request;
    int m_status = 200;
};

QByteArray json(const QJsonObject &object)
{
    return QJsonDocument(object).toJson(QJsonDocument::Compact);
}

} // namespace

class CollectorClientTest final : public QObject
{
    Q_OBJECT

private slots:
    void encodesActressPathAndQuery();
    void exposesHttpDetailError();
    void syncsOnlyFirstFiftyPopularActresses();
};

void CollectorClientTest::encodesActressPathAndQuery()
{
    FakeCollector server(
        json({{QStringLiteral("ok"), true}, {QStringLiteral("data"), QJsonObject{}}}));
    darkeye::CollectorClient client({}, server.url(QStringLiteral("/api/v1/actress")), {});
    QSignalSpy finished(&client, &darkeye::CollectorClient::requestFinished);

    const quint64 requestId =
        client.fetchActress(QStringLiteral("葵 つかさ"), QStringLiteral("/detail?id=12"));
    QVERIFY(finished.wait(2000));
    QCOMPARE(finished.first().at(0).toULongLong(), requestId);
    QCOMPARE(finished.first().at(1).value<darkeye::CollectorRequestKind>(),
             darkeye::CollectorRequestKind::Actress);
    QVERIFY(finished.first().at(2).toBool());
    const QByteArray requestLine = server.requestLine();
    QVERIFY(requestLine.startsWith("GET /api/v1/actress/%E8%91%B5%20%E3%81%A4"));
    const QUrl requestUrl = QUrl::fromEncoded(requestLine.split(' ').value(1));
    QCOMPARE(QUrlQuery(requestUrl).queryItemValue(QStringLiteral("minnano_url"), QUrl::FullyDecoded),
             QStringLiteral("/detail?id=12"));
}

void CollectorClientTest::exposesHttpDetailError()
{
    FakeCollector server(json({{QStringLiteral("detail"), QJsonArray{QStringLiteral("字段无效")}}}),
                         422);
    darkeye::CollectorClient client({}, {}, server.url(QStringLiteral("/api/v1/top-actresses")));
    QSignalSpy finished(&client, &darkeye::CollectorClient::requestFinished);

    QVERIFY(client.fetchTopActresses() > 0);
    QVERIFY(finished.wait(2000));
    QVERIFY(!finished.first().at(2).toBool());
    QCOMPARE(finished.first().at(4).toString(), QStringLiteral("[\"字段无效\"]"));
    QCOMPARE(finished.first().at(5).toInt(), 422);
}

void CollectorClientTest::syncsOnlyFirstFiftyPopularActresses()
{
    QJsonArray names{QStringLiteral("女优卜"), QStringLiteral("女优ト"), QStringLiteral("已存在")};
    for (int index = 0; index < 49; ++index)
        names.append(QStringLiteral("女优%1").arg(index));
    FakeCollector server(json({{QStringLiteral("ok"), true}, {QStringLiteral("names"), names}}));
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    darkeye::SqliteConnection connection;
    QString errorMessage;
    QVERIFY2(connection.open(directory.filePath(QStringLiteral("public.db")), false, &errorMessage),
             qPrintable(errorMessage));
    QVERIFY2(darkeye::SchemaManager::initializeEmptyDatabase(
                 connection, darkeye::DatabaseKind::Public, &errorMessage), qPrintable(errorMessage));
    darkeye::PersonRepository people(connection.database());
    QVERIFY(people
                .create(darkeye::PersonKind::Actress, QStringLiteral("已存在"),
                        QStringLiteral("已存在"), &errorMessage)
                .has_value());
    darkeye::TopActressSyncService service(connection.database(),
                                           server.url(QStringLiteral("/api/v1/top-actresses")));
    QSignalSpy finished(&service, &darkeye::TopActressSyncService::finished);

    QVERIFY(service.start());
    QVERIFY(finished.wait(3000));
    const darkeye::TopActressSyncResult result =
        finished.first().at(0).value<darkeye::TopActressSyncResult>();
    QVERIFY2(result.succeeded, qPrintable(result.errorMessage));
    QCOMPARE(result.received, 50);
    QCOMPARE(result.inserted, 48);
    QCOMPARE(result.existing, 2);
    QCOMPARE(result.failed, 0);
    QVERIFY(people.findByName(darkeye::PersonKind::Actress, QStringLiteral("女优ト")).has_value());
    QVERIFY(people.findByName(darkeye::PersonKind::Actress, QStringLiteral("女优46")).has_value());
    QVERIFY(!people.findByName(darkeye::PersonKind::Actress, QStringLiteral("女优47")).has_value());
}

QTEST_MAIN(CollectorClientTest)
#include "CollectorClientTest.moc"
