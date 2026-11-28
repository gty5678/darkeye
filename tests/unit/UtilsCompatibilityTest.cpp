#include "utils/GeneralUtils.h"
#include "utils/MediaUtils.h"
#include "utils/TextUtils.h"
#include "utils/UpdateUtils.h"
#include "database/CsvExport.h"
#include "database/SqliteConnection.h"

#include <QFile>
#include <QSqlQuery>
#include <QStandardItemModel>
#include <QTemporaryDir>
#include <QtTest>

class UtilsCompatibilityTest final : public QObject
{
    Q_OBJECT

private slots:
    void personDisplayFormatting();
    void mediaScanMatchesPythonContract();
    void genreExclusionsIgnoreNulls();
    void titleTagMappingMatchesPythonCrawlerContract();
    void csvExportsQuoteFieldsAndPreserveUtf8();
    void updateManifestRequiresNumericVersions();
};

void UtilsCompatibilityTest::personDisplayFormatting()
{
    QCOMPARE(darkeye::utils::rankPosition(20, {10, 20, 30}), 0.5);
    QCOMPARE(darkeye::utils::rankPosition(20, {10, 20, 30}, true), 0.5);
    QCOMPARE(darkeye::utils::rankPosition(1, {}), 0.0);
    QCOMPARE(darkeye::utils::convertDate(QStringLiteral("2026-09-16")),
             QStringLiteral("2026年09月16日"));
    QCOMPARE(darkeye::utils::convertDate(QStringLiteral("bad-date")),
             QStringLiteral("bad-date"));
}

void UtilsCompatibilityTest::mediaScanMatchesPythonContract()
{
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    QDir root(temporary.path());
    QVERIFY(root.mkpath(QStringLiteral("nested")));

    const QString serialVideo = root.filePath(QStringLiteral("nested/SONE-979 sample.MP4"));
    const QString fanzaVideo = root.filePath(QStringLiteral("1start00451-extra.mkv"));
    const QString unknownVideo = root.filePath(QStringLiteral("nested/holiday.avi"));
    const QString ignored = root.filePath(QStringLiteral("SONE-100.txt"));
    for (const QString &path : {serialVideo, fanzaVideo, unknownVideo, ignored}) {
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
    }

    const QList<darkeye::utils::VideoFile> collected =
        darkeye::utils::collectVideos({temporary.path()});
    QCOMPARE(collected.size(), 3);

    const darkeye::utils::VideoScanResult scan =
        darkeye::utils::videoNamesFromPaths({temporary.path()});
    QVERIFY(scan.serials.contains(QStringLiteral("SONE-979")));
    QVERIFY(scan.serials.contains(QStringLiteral("START-451")));
    QCOMPARE(scan.filesWithoutSerial.size(), 1);
    QVERIFY(std::any_of(scan.filesWithoutSerial.cbegin(), scan.filesWithoutSerial.cend(),
                        [](const auto &entry) { return entry.first == QStringLiteral("holiday"); }));
}

void UtilsCompatibilityTest::genreExclusionsIgnoreNulls()
{
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QString configPath = temporary.filePath(QStringLiteral("exclude.json"));
    QFile config(configPath);
    QVERIFY(config.open(QIODevice::WriteOnly));
    config.write(R"({"exclude_genre":["2000","AV女优",null]})");
    config.close();
    const QSet<QString> excluded = darkeye::utils::loadExcludedGenres(configPath);
    QCOMPARE(excluded.size(), 2);
    QVERIFY(excluded.contains(QStringLiteral("AV女优")));
}

void UtilsCompatibilityTest::titleTagMappingMatchesPythonCrawlerContract()
{
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QString mapPath = temporary.filePath(QStringLiteral("tag_map.json"));
    QFile map(mapPath);
    QVERIFY(map.open(QIODevice::WriteOnly));
    map.write(R"({"新人":"出道作","黒パンスト":["黑丝","连裤袜"],"出張先|相部屋":"出张先相部屋"})");
    map.close();

    const QStringList tags = darkeye::utils::tagNamesFromText(
        QStringLiteral("新人 黒パンスト 出張先の相部屋"), mapPath);
    QCOMPARE(QSet<QString>(tags.cbegin(), tags.cend()),
             QSet<QString>({QStringLiteral("出道作"), QStringLiteral("黑丝"),
                            QStringLiteral("连裤袜"), QStringLiteral("出张先相部屋")}));
    QCOMPARE(darkeye::utils::tagNamesFromText(QStringLiteral("出張先だけ"), mapPath),
             QStringList());
}

void UtilsCompatibilityTest::csvExportsQuoteFieldsAndPreserveUtf8()
{
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    QStandardItemModel model(1, 2);
    model.setHorizontalHeaderLabels({QStringLiteral("名称"), QStringLiteral("备注")});
    model.setData(model.index(0, 0), QStringLiteral("作品,一"));
    model.setData(model.index(0, 1), QStringLiteral("含\"引号\""));
    const QString modelCsv = temporary.filePath(QStringLiteral("model.csv"));
    QString error;
    QVERIFY2(darkeye::exportModelToCsv(&model, modelCsv, &error), qPrintable(error));
    QFile modelOutput(modelCsv);
    QVERIFY(modelOutput.open(QIODevice::ReadOnly | QIODevice::Text));
    QCOMPARE(QString::fromUtf8(modelOutput.readAll()),
             QStringLiteral("名称,备注\n\"作品,一\",\"含\"\"引号\"\"\"\n"));

    const QString databasePath = temporary.filePath(QStringLiteral("data.db"));
    {
        darkeye::SqliteConnection connection;
        QVERIFY2(connection.open(databasePath, false, &error), qPrintable(error));
        QSqlQuery query(connection.database());
        QVERIFY(query.exec(QStringLiteral("CREATE TABLE item(id INTEGER, name TEXT)")));
        QVERIFY(query.exec(QStringLiteral("INSERT INTO item VALUES(1, '中文')")));
    }
    const QString sqlCsv = temporary.filePath(QStringLiteral("sql.csv"));
    QVERIFY2(darkeye::exportSqlToCsv(QStringLiteral("SELECT id,name FROM item"), sqlCsv,
                                    databasePath, &error), qPrintable(error));
    QFile sqlOutput(sqlCsv);
    QVERIFY(sqlOutput.open(QIODevice::ReadOnly | QIODevice::Text));
    QCOMPARE(QString::fromUtf8(sqlOutput.readAll()), QStringLiteral("id,name\n1,中文\n"));
}

void UtilsCompatibilityTest::updateManifestRequiresNumericVersions()
{
    QCOMPARE(darkeye::utils::parseVersion(QStringLiteral("v1.2.3")).value(),
             QList<int>({1, 2, 3}));
    QVERIFY(!darkeye::utils::parseVersion(QStringLiteral("1.beta")).has_value());
    QVERIFY(darkeye::utils::isNewerVersion(QStringLiteral("1.2.0"), QStringLiteral("1.1.9")));
    QVERIFY(!darkeye::utils::isNewerVersion(QStringLiteral("1.0"), QStringLiteral("1.0.0")));
    QVERIFY(!darkeye::utils::isNewerVersion(QStringLiteral("1.0.0"), QStringLiteral("1.0")));
    QVERIFY(!darkeye::utils::isNewerVersion(QStringLiteral("nightly"), QStringLiteral("stable")));
    QVERIFY(!darkeye::utils::parseVersion(QStringLiteral("1..2")));
    QVERIFY(!darkeye::utils::parseVersion(QStringLiteral("1.-2")));

    const QByteArray manifest = QString::fromUtf8(
        R"({"latestVersion":"1.1.0","releaseNotes":"修复问题","packages":{"%1":{"url":"https://example.test/app.zip","sha256":"%2"}}})")
        .arg(darkeye::utils::updateTarget(), QString(64, u'a')).toUtf8();
    const auto available = darkeye::utils::evaluateUpdateManifest(QStringLiteral("1.0.0"), manifest);
    QVERIFY(available.success);
    QVERIFY(available.updateAvailable);
    QCOMPARE(available.latestVersion, QStringLiteral("1.1.0"));
    QCOMPARE(available.packageUrl, QStringLiteral("https://example.test/app.zip"));
    QVERIFY(available.message.contains(QStringLiteral("修复问题")));
    const auto current = darkeye::utils::evaluateUpdateManifest(QStringLiteral("1.1.0"), manifest);
    QVERIFY(current.success);
    QVERIFY(!current.updateAvailable);
    QByteArray invalidVersion = manifest;
    invalidVersion.replace("1.1.0", "nightly");
    QVERIFY(!darkeye::utils::evaluateUpdateManifest(QStringLiteral("1.0.0"), invalidVersion).success);
    for (const QByteArray &invalid : {
            QByteArray(R"({"latestVersion":"1.1.0","package":{"url":"https://example.test/old.zip"}})"),
            QByteArray(R"({"latestVersion":"1.1.0","packages":{"other-target":{}}})"),
            QByteArray(R"({"releaseNotes":"none"})")}) {
        QVERIFY(!darkeye::utils::evaluateUpdateManifest(QStringLiteral("1.0.0"), invalid).success);
    }
    QByteArray badHash = manifest;
    badHash.replace(QByteArray(64, 'a'), QByteArray(64, '0'));
    QVERIFY(!darkeye::utils::evaluateUpdateManifest(QStringLiteral("1.0.0"), badHash).success);
    QByteArray insecure = manifest;
    insecure.replace("https://", "http://");
    QVERIFY(!darkeye::utils::evaluateUpdateManifest(QStringLiteral("1.0.0"), insecure).success);
}

QTEST_MAIN(UtilsCompatibilityTest)
#include "UtilsCompatibilityTest.moc"
