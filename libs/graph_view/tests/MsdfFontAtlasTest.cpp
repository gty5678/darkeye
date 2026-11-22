#include "graph_view/MsdfFontAtlas.h"

#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QtTest>

class MsdfFontAtlasTest final : public QObject
{
    Q_OBJECT

private slots:
    void parallelMatchesSerial();
    void reportsBuildErrors();
};

namespace {
QString fontPath()
{
    const QDir fonts(QDir(qEnvironmentVariable("WINDIR")).filePath(QStringLiteral("Fonts")));
    for (const auto& name : {QStringLiteral("msyh.ttc"), QStringLiteral("arial.ttf")}) {
        const QString path = fonts.filePath(name);
        if (QFileInfo::exists(path)) {
            return path;
        }
    }
    return {};
}
}

void MsdfFontAtlasTest::parallelMatchesSerial()
{
    MsdfFontAtlas::Config config;
    config.fontPath = fontPath();
    if (config.fontPath.isEmpty()) {
        QSKIP("No test font available");
    }
    config.atlasWidth = config.atlasHeight = 2048;
    QString characters;
    for (ushort cp = 0x4e00; cp < 0x4f00; ++cp) {
        characters.append(QChar(cp));
    }
    for (ushort cp = 33; cp < 127; ++cp) {
        characters.append(QChar(cp));
    }
    // Exercise duplicates, whitespace and a codepoint absent from these fonts.
    characters.append(QStringLiteral("   AV AV"));
    const char32_t missing[] = {0x10ffff};
    characters.append(QString::fromUcs4(missing, 1));
    const QStringList labels{characters, QStringLiteral("重复标签"), characters};
    MsdfFontAtlas::AtlasData serial;
    MsdfFontAtlas::AtlasData parallel;
    QString error;
    config.maxGenerationThreads = 1;
    QElapsedTimer timer;
    timer.start();
    QVERIFY2(MsdfFontAtlas::BuildAtlasStandalone(config, labels, serial, &error), qPrintable(error));
    const qint64 serialMs = timer.elapsed();
    config.maxGenerationThreads = 0;
    timer.restart();
    QVERIFY2(MsdfFontAtlas::BuildAtlasStandalone(config, labels, parallel, &error), qPrintable(error));
    qInfo() << "MSDF serial:" << serialMs << "ms; parallel:" << timer.elapsed() << "ms";

    QCOMPARE(parallel.width, serial.width);
    QCOMPARE(parallel.height, serial.height);
    QVERIFY(parallel.pixels == serial.pixels);
    QCOMPARE(parallel.ascender, serial.ascender);
    QCOMPARE(parallel.descender, serial.descender);
    QCOMPARE(parallel.lineHeight, serial.lineHeight);
    QCOMPARE(parallel.glyphs.size(), serial.glyphs.size());
    for (const auto& [cp, expected] : serial.glyphs) {
        const auto it = parallel.glyphs.find(cp);
        QVERIFY(it != parallel.glyphs.end());
        const auto& actual = it->second;
        QCOMPARE(actual.codepoint, expected.codepoint);
        QCOMPARE(actual.advance, expected.advance);
        QCOMPARE(actual.drawable, expected.drawable);
        QCOMPARE(actual.planeLeft, expected.planeLeft);
        QCOMPARE(actual.planeBottom, expected.planeBottom);
        QCOMPARE(actual.planeRight, expected.planeRight);
        QCOMPARE(actual.planeTop, expected.planeTop);
        QCOMPARE(actual.u0, expected.u0);
        QCOMPARE(actual.v0, expected.v0);
        QCOMPARE(actual.u1, expected.u1);
        QCOMPARE(actual.v1, expected.v1);
    }
    QVERIFY(parallel.glyphs.count(' ') != 0);
    QVERIFY(!parallel.glyphs.at(' ').drawable);
    QVERIFY(parallel.glyphs.count(0x10ffff) == 0);

    // Small sets and an empty label list must keep their serial output too.
    for (const auto& small : {QStringList{}, QStringList{QStringLiteral("A A?")}}) {
        config.maxGenerationThreads = 1;
        QVERIFY(MsdfFontAtlas::BuildAtlasStandalone(config, small, serial));
        config.maxGenerationThreads = 0;
        QVERIFY(MsdfFontAtlas::BuildAtlasStandalone(config, small, parallel));
        QVERIFY(parallel.pixels == serial.pixels);
    }
}

void MsdfFontAtlasTest::reportsBuildErrors()
{
    MsdfFontAtlas::Config config;
    MsdfFontAtlas::AtlasData data;
    QString error;
    QVERIFY(!MsdfFontAtlas::BuildAtlasStandalone(config, {}, data, &error));
    QVERIFY(!error.isEmpty());
    config.fontPath = fontPath();
    if (config.fontPath.isEmpty()) {
        QSKIP("No test font available for overflow check");
    }
    config.atlasWidth = config.atlasHeight = 8;
    error.clear();
    QVERIFY(!MsdfFontAtlas::BuildAtlasStandalone(config, {QStringLiteral("ABCD")}, data, &error));
    QVERIFY(error.contains(QStringLiteral("overflow")));
}

QTEST_GUILESS_MAIN(MsdfFontAtlasTest)
#include "MsdfFontAtlasTest.moc"
