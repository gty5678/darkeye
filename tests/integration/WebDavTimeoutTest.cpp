#include "database/WebDavBackupService.h"
#include "database/WebDavCredentialStore.h"

#include <QElapsedTimer>
#include <QFile>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTimer>
#include <QUuid>
#include <QtTest>

class WebDavTimeoutTest final : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();
    void stalledRequest_data();
    void stalledRequest();
    void progressingResponse();

private:
    QString m_profile;
};

void WebDavTimeoutTest::initTestCase()
{
#ifndef Q_OS_WIN
    if (!qEnvironmentVariableIsSet("DARKEYE_TEST_SYSTEM_KEYRING"))
        QSKIP("Set DARKEYE_TEST_SYSTEM_KEYRING=1 in a desktop session to test WebDAV timeouts.");
#endif
    m_profile = QStringLiteral("timeout-test-%1").arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
    QString error;
    QVERIFY2(darkeye::WebDavCredentialStore::save(
                 m_profile, {QStringLiteral("test"), QStringLiteral("test")}, &error), qPrintable(error));
}

void WebDavTimeoutTest::cleanupTestCase()
{
    if (!m_profile.isEmpty()) QVERIFY(darkeye::WebDavCredentialStore::clear(m_profile));
}

void WebDavTimeoutTest::stalledRequest_data()
{
    QTest::addColumn<QByteArray>("verb");
    QTest::addColumn<int>("timeoutSeconds");
    QTest::addColumn<bool>("sendHeaders");
    QTest::newRow("propfind-3-seconds") << QByteArray("PROPFIND") << 3 << false;
    QTest::newRow("propfind-4-seconds") << QByteArray("PROPFIND") << 4 << false;
    QTest::newRow("mkcol") << QByteArray("MKCOL") << 3 << false;
    QTest::newRow("put") << QByteArray("PUT") << 3 << false;
    QTest::newRow("get") << QByteArray("GET") << 3 << false;
    QTest::newRow("get-stalls-after-200") << QByteArray("GET") << 3 << true;
}

void WebDavTimeoutTest::stalledRequest()
{
    QFETCH(QByteArray, verb);
    QFETCH(int, timeoutSeconds);
    QFETCH(bool, sendHeaders);
    QTcpServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost));
    QList<QByteArray> verbs;
    connect(&server, &QTcpServer::newConnection, this, [&] {
        QTcpSocket *socket = server.nextPendingConnection();
        connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
        connect(socket, &QTcpSocket::readyRead, socket, [&, socket] {
            const QByteArray request = socket->property("request").toByteArray() + socket->readAll();
            socket->setProperty("request", request);
            if (!request.contains("\r\n\r\n") || socket->property("handled").toBool()) return;
            socket->setProperty("handled", true);
            const QByteArray method = request.left(request.indexOf(' '));
            verbs.append(method);
            if (method == verb)
            {
                if (sendHeaders)
                    socket->write("HTTP/1.1 200 OK\r\nContent-Length: 100\r\n\r\nx");
                return;
            }
            // MKCOL is reached only when the directory does not exist.
            const QByteArray status = verb == "MKCOL" ? "404 Not Found" : "207 Multi-Status";
            socket->write("HTTP/1.1 " + status + "\r\nContent-Length: 0\r\nConnection: close\r\n\r\n");
            socket->disconnectFromHost();
        });
    });
    darkeye::CrawlerSettings::WebDav settings;
    settings.enabled = true;
    settings.profileName = m_profile;
    settings.baseUrl = QUrl(QStringLiteral("http://127.0.0.1:%1").arg(server.serverPort()));
    settings.timeoutSeconds = timeoutSeconds;
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QString localPath = temporary.filePath(QStringLiteral("backup.db"));
    QFile file(localPath);
    QVERIFY(file.open(QIODevice::WriteOnly));
    QCOMPARE(file.write("backup"), qint64(6));
    file.close();
    QElapsedTimer elapsed;
    elapsed.start();
    darkeye::WebDavActionResult result;
    if (verb == "PUT" || verb == "MKCOL")
        result = darkeye::WebDavBackupService::uploadFile(localPath, settings);
    else if (verb == "GET")
        result = darkeye::WebDavBackupService::restoreDatabaseBackup(
            {}, QStringLiteral("/darkeye/remote.db"), temporary.path(), settings);
    else
        result = darkeye::WebDavBackupService::testConnection(settings);
    QVERIFY(!result.succeeded);
    QVERIFY2(result.message.contains(QStringLiteral("超时")), qPrintable(result.message));
    QVERIFY(elapsed.elapsed() >= timeoutSeconds * 1000 - 200);
    QVERIFY(elapsed.elapsed() < timeoutSeconds * 1000 + 2000);
    QVERIFY(verbs.contains(verb));
    if (verb == "GET") QVERIFY(!QFile::exists(temporary.filePath(QStringLiteral("remote.db"))));
    if (verb == "PROPFIND") QCOMPARE(verbs.size(), 1);
}

void WebDavTimeoutTest::progressingResponse()
{
    QTcpServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost));
    connect(&server, &QTcpServer::newConnection, this, [&] {
        QTcpSocket *socket = server.nextPendingConnection();
        connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
        connect(socket, &QTcpSocket::readyRead, socket, [socket] {
            const QByteArray request = socket->property("request").toByteArray() + socket->readAll();
            socket->setProperty("request", request);
            if (!request.contains("\r\n\r\n") || socket->property("handled").toBool()) return;
            socket->setProperty("handled", true);
            const QByteArray body = "<multistatus><response><href>/darkeye/backup.db</href></response></multistatus>";
            socket->write("HTTP/1.1 207 Multi-Status\r\nConnection: close\r\nContent-Length: " +
                          QByteArray::number(body.size()) + "\r\n\r\n" + body.left(10));
            QTimer::singleShot(2000, socket, [socket, body] { socket->write(body.mid(10, 10)); });
            QTimer::singleShot(4000, socket, [socket, body] {
                socket->write(body.mid(20));
                socket->disconnectFromHost();
            });
        });
    });
    darkeye::CrawlerSettings::WebDav settings;
    settings.enabled = true;
    settings.profileName = m_profile;
    settings.baseUrl = QUrl(QStringLiteral("http://127.0.0.1:%1").arg(server.serverPort()));
    settings.timeoutSeconds = 3;
    QStringList files;
    const auto result = darkeye::WebDavBackupService::listBackups(settings, &files);
    QVERIFY2(result.succeeded, qPrintable(result.message));
    QCOMPARE(files, QStringList{QStringLiteral("/darkeye/backup.db")});
}

QTEST_GUILESS_MAIN(WebDavTimeoutTest)
#include "WebDavTimeoutTest.moc"
