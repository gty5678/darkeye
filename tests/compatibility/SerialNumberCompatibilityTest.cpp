#include "domain/SerialNumber.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QtTest>

class SerialNumberCompatibilityTest final : public QObject
{
    Q_OBJECT

private slots:
    void matchesFrozenGoldenSamples();
    void removesConfiguredEscapeStrings();
};

void SerialNumberCompatibilityTest::matchesFrozenGoldenSamples()
{
    const QString path =
        QDir(QStringLiteral(DARKEYE_SOURCE_DIR))
            .filePath(QStringLiteral("tests/fixtures/golden/serial-number.json"));
    QFile file(path);
    QVERIFY2(file.open(QIODevice::ReadOnly), qPrintable(file.errorString()));
    QJsonParseError parseError;
    const QJsonObject golden =
        QJsonDocument::fromJson(file.readAll(), &parseError).object();
    QCOMPARE(parseError.error, QJsonParseError::NoError);

    for (const QJsonValue &entry : golden.value(QStringLiteral("normalizeNumber")).toArray()) {
        const QJsonArray row = entry.toArray();
        QCOMPARE(darkeye::serial::normalizeNumber(row.at(0).toString()),
                 row.at(1).toString());
    }
    for (const QJsonValue &entry : golden.value(QStringLiteral("normalizeRawName")).toArray()) {
        const QJsonArray row = entry.toArray();
        QCOMPARE(darkeye::serial::normalizeRawName(row.at(0).toString()),
                 row.at(1).toString());
    }
    for (const QJsonValue &entry : golden.value(QStringLiteral("valid")).toArray()) {
        const QJsonArray row = entry.toArray();
        QCOMPARE(darkeye::serial::isValid(row.at(0).toString()), row.at(1).toBool());
    }
    for (const QJsonValue &entry : golden.value(QStringLiteral("fanza")).toArray()) {
        const QJsonArray row = entry.toArray();
        QCOMPARE(darkeye::serial::convertFanza(row.at(0).toString()),
                 row.at(1).toString());
    }
    for (const QJsonValue &entry : golden.value(QStringLiteral("equal")).toArray()) {
        const QJsonArray row = entry.toArray();
        QCOMPARE(darkeye::serial::equal(row.at(0).toString(), row.at(1).toString()),
                 row.at(2).toBool());
    }
    for (const QJsonValue &entry : golden.value(QStringLiteral("special")).toArray()) {
        const QJsonArray row = entry.toArray();
        QCOMPARE(darkeye::serial::convertSpecial(row.at(0).toString()),
                 row.at(1).toString());
    }
    for (const QJsonValue &entry : golden.value(QStringLiteral("extract")).toArray()) {
        const QJsonArray row = entry.toArray();
        const std::optional<QString> input =
            row.at(0).isNull() ? std::nullopt
                               : std::optional<QString>(row.at(0).toString());
        const std::optional<QString> actual = darkeye::serial::extract(input);
        if (row.at(1).isNull()) {
            QVERIFY2(!actual.has_value(), qPrintable(row.at(0).toString()));
        } else {
            QVERIFY2(actual.has_value(), qPrintable(row.at(0).toString()));
            QCOMPARE(*actual, row.at(1).toString());
        }
    }
}

void SerialNumberCompatibilityTest::removesConfiguredEscapeStrings()
{
    QCOMPARE(darkeye::serial::normalizeRawName(
                 QStringLiteral("FOO IPX-247 BAR"),
                 {QStringLiteral("FOO"), QStringLiteral("BAR")}),
             QStringLiteral("IPX-247"));
    QVERIFY(darkeye::serial::extract(
                QStringLiteral("PREFIX IPX-247 SUFFIX"),
                {QStringLiteral("PREFIX"), QStringLiteral("SUFFIX")})
            == std::optional<QString>(QStringLiteral("IPX-247")));
}

QTEST_MAIN(SerialNumberCompatibilityTest)
#include "SerialNumberCompatibilityTest.moc"
