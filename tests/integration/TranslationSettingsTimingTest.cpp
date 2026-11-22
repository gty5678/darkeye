#include "services/BatchTranslationService.h"
#include "services/LlmTranslationService.h"
#include "settings/Settings.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QSqlQuery>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QtTest>
#include <functional>

namespace
{
class TranslationServer final : public QTcpServer
{
public:
    QList<QJsonObject> requests;
    QList<QByteArray> headers;
    std::function<void()> onRequest;
    bool failNext = false;

    TranslationServer()
    {
        connect(this, &QTcpServer::newConnection, this, [this] {
            auto *socket = nextPendingConnection();
            connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
            connect(socket, &QTcpSocket::readyRead, this, [this, socket] {
                const QByteArray bytes = socket->property("request").toByteArray() + socket->readAll();
                socket->setProperty("request", bytes);
                const qsizetype separator = bytes.indexOf("\r\n\r\n");
                if (separator < 0 || socket->property("responded").toBool()) return;
                qsizetype length = 0;
                for (QByteArray header : bytes.left(separator).split('\n'))
                {
                    header = header.trimmed();
                    if (header.toLower().startsWith("content-length:"))
                        length = header.mid(15).trimmed().toLongLong();
                }
                if (bytes.size() - separator - 4 < length) return;
                socket->setProperty("responded", true);
                headers.append(bytes.left(separator));
                requests.append(QJsonDocument::fromJson(bytes.mid(separator + 4, length)).object());
                const bool fail = failNext;
                failNext = false;
                if (onRequest) onRequest();
                const QByteArray body = fail ? QByteArray("{\"error\":{\"message\":\"retry\"}}")
                    : QByteArray("{\"choices\":[{\"message\":{\"content\":\"translated\"}}]}");
                socket->write(QByteArray(fail ? "HTTP/1.1 500 Error\r\n" : "HTTP/1.1 200 OK\r\n") +
                              "Content-Type: application/json\r\nConnection: close\r\nContent-Length: " +
                              QByteArray::number(body.size()) + "\r\n\r\n" + body);
                socket->disconnectFromHost();
            });
        });
    }
};
}

class TranslationSettingsTimingTest final : public QObject
{
    Q_OBJECT
private slots:
    void reloadsPerCallAndKeepsRetrySettings();
    void batchReloadsPerFieldAndPerRun();
};

void TranslationSettingsTimingTest::reloadsPerCallAndKeepsRetrySettings()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString file = directory.filePath(QStringLiteral("settings.ini"));
    TranslationServer oldServer, newServer;
    QVERIFY(oldServer.listen(QHostAddress::LocalHost, 0));
    QVERIFY(newServer.listen(QHostAddress::LocalHost, 0));
    darkeye::LlmTranslationService translator([file] { return darkeye::settings::translation(file); });
    QSignalSpy finished(&translator, &darkeye::LlmTranslationService::translationFinished);
    darkeye::TranslationSettings values;
    values.baseUrl = QStringLiteral("http://127.0.0.1:%1/old").arg(oldServer.serverPort());
    values.model = QStringLiteral("old-model");
    values.apiKey = QStringLiteral("old-key");
    values.retries = 1;
    darkeye::settings::saveTranslation(values, file);
    oldServer.failNext = true;
    oldServer.onRequest = [&] {
        values.baseUrl = QStringLiteral("http://127.0.0.1:%1/new").arg(newServer.serverPort());
        values.model = QStringLiteral("new-model");
        values.apiKey = QStringLiteral("new-key");
        values.retries = 0;
        values.timeoutSeconds = 1;
        darkeye::settings::saveTranslation(values, file);
    };
    (void)translator.translate(QStringLiteral("name"), QStringLiteral("zh-CN"), QStringLiteral("actress_name"));
    QTRY_COMPARE(finished.size(), 1);
    QVERIFY(finished.last().at(2).toString().isEmpty());
    QCOMPARE(oldServer.requests.size(), 2);
    QCOMPARE(newServer.requests.size(), 0);
    for (int index = 0; index < 2; ++index)
    {
        QCOMPARE(oldServer.requests.at(index).value(QStringLiteral("model")).toString(), QStringLiteral("old-model"));
        QVERIFY(oldServer.headers.at(index).contains("Authorization: Bearer old-key"));
    }
    newServer.failNext = true;
    (void)translator.translate(QStringLiteral("story"));
    QTRY_COMPARE(finished.size(), 2);
    QVERIFY(!finished.last().at(2).toString().isEmpty());
    QCOMPARE(newServer.requests.size(), 1);
    QCOMPARE(newServer.requests.first().value(QStringLiteral("model")).toString(), QStringLiteral("new-model"));
    QVERIFY(newServer.headers.first().startsWith("POST /new/chat/completions "));
    QVERIFY(newServer.headers.first().contains("Authorization: Bearer new-key"));
    QCOMPARE(oldServer.requests.size(), 2);
}

void TranslationSettingsTimingTest::batchReloadsPerFieldAndPerRun()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString file = directory.filePath(QStringLiteral("settings.ini"));
    TranslationServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost, 0));
    const QString connection = QStringLiteral("translation-settings-timing");
    {
        auto database = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connection);
        database.setDatabaseName(QStringLiteral(":memory:"));
        QVERIFY(database.open());
        QSqlQuery query(database);
        QVERIFY(query.exec(QStringLiteral("CREATE TABLE work (work_id INTEGER PRIMARY KEY, jp_title TEXT, cn_title TEXT, jp_story TEXT, cn_story TEXT, is_deleted INTEGER DEFAULT 0)")));
        QVERIFY(query.exec(QStringLiteral("INSERT INTO work(work_id,jp_title,jp_story) VALUES(1,'title','story')")));
        darkeye::BatchTranslationService batch(database, [file] { return darkeye::settings::translation(file); });
        QSignalSpy finished(&batch, &darkeye::BatchTranslationService::finished);
        darkeye::TranslationSettings values;
        values.baseUrl = QStringLiteral("http://127.0.0.1:%1/v1").arg(server.serverPort());
        values.model = QStringLiteral("first-model");
        values.retries = 0;
        darkeye::settings::saveTranslation(values, file);
        server.onRequest = [&] {
            values.model = QStringLiteral("next-model");
            darkeye::settings::saveTranslation(values, file);
        };
        QVERIFY(batch.start(darkeye::BatchTranslationMode::FillMissing));
        QTRY_COMPARE(finished.size(), 1);
        const auto result = qvariant_cast<darkeye::BatchTranslationResult>(finished.first().first());
        QVERIFY(result.succeeded);
        QCOMPARE(result.failedFields, 0);
        QCOMPARE(result.translatedTitles, 1);
        QCOMPARE(result.translatedStories, 1);
        QCOMPARE(server.requests.size(), 2);
        QCOMPARE(server.requests.at(0).value(QStringLiteral("model")).toString(), QStringLiteral("first-model"));
        QCOMPARE(server.requests.at(1).value(QStringLiteral("model")).toString(), QStringLiteral("next-model"));
        server.onRequest = {};
        values.model = QStringLiteral("another-run");
        darkeye::settings::saveTranslation(values, file);
        QVERIFY(batch.start(darkeye::BatchTranslationMode::ForceOverwrite));
        QTRY_COMPARE(finished.size(), 2);
        QCOMPARE(server.requests.size(), 4);
        for (int index = 2; index < 4; ++index)
            QCOMPARE(server.requests.at(index).value(QStringLiteral("model")).toString(), QStringLiteral("another-run"));
    }
    QSqlDatabase::removeDatabase(connection);
}

QTEST_GUILESS_MAIN(TranslationSettingsTimingTest)
#include "TranslationSettingsTimingTest.moc"
