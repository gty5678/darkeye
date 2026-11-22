#include "services/LlmTranslationService.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QtTest>

class LlmTranslationServiceTest final : public QObject
{
    Q_OBJECT

private slots:
    void switchesPromptPerRequest();
};

void LlmTranslationServiceTest::switchesPromptPerRequest()
{
    QTcpServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost, 0));
    QList<QJsonObject> requests;
    connect(&server, &QTcpServer::newConnection, this, [&] {
        auto *socket = server.nextPendingConnection();
        connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
        connect(socket, &QTcpSocket::readyRead, this, [&, socket] {
            QByteArray bytes = socket->property("request").toByteArray() + socket->readAll();
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
            requests.append(QJsonDocument::fromJson(bytes.mid(separator + 4, length)).object());
            socket->setProperty("responded", true);
            const QByteArray body = QJsonDocument(QJsonObject{
                {QStringLiteral("choices"), QJsonArray{QJsonObject{
                    {QStringLiteral("message"), QJsonObject{
                        {QStringLiteral("content"), QStringLiteral("测试译名")}}}}}}})
                .toJson(QJsonDocument::Compact);
            socket->write(QByteArray("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\n"
                                    "Connection: close\r\nContent-Length: ") +
                          QByteArray::number(body.size()) + "\r\n\r\n" + body);
            socket->disconnectFromHost();
        });
    });

    darkeye::TranslationSettings settings;
    settings.model = QStringLiteral("test-model");
    settings.baseUrl = QStringLiteral("http://127.0.0.1:%1/v1").arg(server.serverPort());
    settings.retries = 0;
    darkeye::LlmTranslationService translator(settings);
    QSignalSpy finished(&translator, &darkeye::LlmTranslationService::translationFinished);
    const QString source = QStringLiteral("テスト女優");
    const QStringList variants{QStringLiteral("default"), QStringLiteral("actress_name"),
                               QStringLiteral("default"), QStringLiteral("unknown")};
    for (const QString &variant : variants)
    {
        const int count = finished.size();
        const quint64 id = count == 0 ? translator.translate(source)
            : translator.translate(source, QStringLiteral("zh-CN"), variant);
        QTRY_COMPARE(finished.size(), count + 1);
        QCOMPARE(finished.last().at(0).toULongLong(), id);
        QCOMPARE(finished.last().at(1).toString(), QStringLiteral("测试译名"));
        QVERIFY(finished.last().at(2).toString().isEmpty());
    }
    QCOMPARE(requests.size(), variants.size());
    const QString genericPrompt = QStringLiteral(
        "你是专业的日文到中文翻译引擎。只输出译文，不添加任何解释、前后缀、引号或注释。"
        "保留番号、系列名、人名、专有名词。若输入为空，返回空字符串。将淫荡的用语翻译到位");
    const QString namePrompt = QStringLiteral(
        "你是专业的日文姓名翻译引擎。"
        "输入是女优或艺人的日文名字，输出仅允许为一个中文名字。"
        "优先使用常见汉字译名；若无通行译名，使用自然、简洁的中文音译。"
        "不要输出解释、括号、前后缀、引号、注释或额外句子。"
        "若输入为空，返回空字符串。");
    for (int index = 0; index < requests.size(); ++index)
    {
        const QJsonArray messages = requests.at(index).value(QStringLiteral("messages")).toArray();
        const bool name = variants.at(index) == QStringLiteral("actress_name");
        QCOMPARE(messages.at(0).toObject().value(QStringLiteral("content")).toString(),
                 name ? namePrompt : genericPrompt);
        QCOMPARE(messages.at(1).toObject().value(QStringLiteral("content")).toString(),
                 (name ? QStringLiteral("以下是日文艺人名，请翻译成zh-CN，只输出中文名字：\n\n%1")
                       : QStringLiteral("将以下文本翻译为 zh-CN，只输出译文：\n\n%1")).arg(source));
    }
}

QTEST_GUILESS_MAIN(LlmTranslationServiceTest)
#include "LlmTranslationServiceTest.moc"
