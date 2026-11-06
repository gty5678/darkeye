#include "ui/pages/NfoImport.h"
#include "database/SchemaManager.h"
#include "database/SqliteConnection.h"
#include "database/repositories/PersonRepository.h"
#include "database/repositories/WorkRepository.h"
#include <QDir>
#include <QFile>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QtTest>

using namespace darkeye;
namespace
{
bool writeFile(const QString &path, const QByteArray &bytes)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}
bool image(const QString &path)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QImage value(8, 8, QImage::Format_RGB32);
    value.fill(Qt::red);
    return value.save(path);
}
}
class NfoImportTest final : public QObject
{
    Q_OBJECT
private slots:
    void serialAndRoot_data();
    void serialAndRoot();
    void dates_data();
    void dates();
    void directChildrenAndSceneSources();
    void relativeImagesUseWorkingDirectory();
    void blockedImages_data();
    void blockedImages();
    void mdczCastCreationAndFallback();
    void mdczFanartMigration();
};

void NfoImportTest::serialAndRoot_data()
{
    QTest::addColumn<QByteArray>("xml");
    QTest::addColumn<bool>("mdcz");
    QTest::addColumn<QString>("serial");
    QTest::addColumn<QString>("error");
    QTest::newRow("33 ordinary rejects uniqueid") << QByteArray("<movie><uniqueid>A-1</uniqueid></movie>") << false << QString{} << QStringLiteral("缺少番号");
    QTest::newRow("33 id precedes num") << QByteArray("<movie><num>B-1</num><id>A-1</id></movie>") << false << QStringLiteral("A-1") << QString{};
    QTest::newRow("33 empty id falls back num") << QByteArray("<movie><id> </id><num>a-1</num></movie>") << false << QStringLiteral("A-1") << QString{};
    QTest::newRow("34 default priority") << QByteArray("<movie><uniqueid>B-1</uniqueid><uniqueid default=' TRUE '>a-1</uniqueid></movie>") << true << QStringLiteral("A-1") << QString{};
    QTest::newRow("34 empty default skipped") << QByteArray("<movie><uniqueid default='true'> </uniqueid><uniqueid>B-1</uniqueid><uniqueid>A-1</uniqueid></movie>") << true << QStringLiteral("B-1") << QString{};
    QTest::newRow("34 first valid default") << QByteArray("<movie><uniqueid default='true'>B-1</uniqueid><uniqueid default='true'>A-1</uniqueid></movie>") << true << QStringLiteral("B-1") << QString{};
    QTest::newRow("34 id overrides default") << QByteArray("<movie><uniqueid default='true'>B-1</uniqueid><num>C-1</num><id>A-1</id></movie>") << true << QStringLiteral("A-1") << QString{};
    QTest::newRow("35 ordinary wrong root") << QByteArray("<root><movie><id>A-1</id></movie></root>") << false << QString{} << QStringLiteral("根元素");
    QTest::newRow("35 mdcz wrong root") << QByteArray("<root><id>A-1</id></root>") << true << QString{} << QStringLiteral("根元素");
    QTest::newRow("35 namespaced root") << QByteArray("<movie xmlns='urn:other'><id>A-1</id></movie>") << true << QString{} << QStringLiteral("根元素");
    QTest::newRow("nested id excluded") << QByteArray("<movie><other><id>A-1</id></other></movie>") << true << QString{} << QStringLiteral("缺少番号");
    QTest::newRow("malformed trailing XML") << QByteArray("<movie><id>A-1</id></movie><other/>") << false << QString{} << QStringLiteral("XML 解析失败");
}
void NfoImportTest::serialAndRoot()
{
    QFETCH(QByteArray, xml); QFETCH(bool, mdcz); QFETCH(QString, serial); QFETCH(QString, error);
    QTemporaryDir temp;
    const QString path = temp.filePath(QStringLiteral("movie.nfo"));
    QVERIFY(writeFile(path, xml));
    QString message;
    const auto parsed = nfo::parseNfo(path, mdcz, &message);
    if (error.isEmpty()) { QVERIFY2(parsed.has_value(), qPrintable(message)); QCOMPARE(parsed->serial, serial); }
    else { QVERIFY(!parsed); QVERIFY2(message.contains(error), qPrintable(message)); }
}
void NfoImportTest::dates_data()
{
    QTest::addColumn<QByteArray>("fields"); QTest::addColumn<bool>("mdcz"); QTest::addColumn<QString>("expected");
    const QByteArray all("<release>2001-01-01</release><releasedate>2002-02-02</releasedate><premiered>2003-03-03</premiered>");
    QTest::newRow("36 ordinary priority") << all << false << QStringLiteral("2003-03-03");
    QTest::newRow("36 mdcz priority") << all << true << QStringLiteral("2003-03-03");
    const QByteArray fallback("<release>2001-01-01</release><releasedate>2002-02-02</releasedate><premiered> </premiered>");
    QTest::newRow("36 ordinary fallback") << fallback << false << QStringLiteral("2001-01-01");
    QTest::newRow("36 mdcz fallback") << fallback << true << QStringLiteral("2002-02-02");
    QTest::newRow("36 ordinary ignores releasedate") << QByteArray("<releasedate>2002-02-02</releasedate>") << false << QString{};
    QTest::newRow("36 first premiered only") << QByteArray("<premiered/><premiered>2003-03-03</premiered><release>2001-01-01</release>") << false << QStringLiteral("2001-01-01");
}
void NfoImportTest::dates()
{
    QFETCH(QByteArray, fields); QFETCH(bool, mdcz); QFETCH(QString, expected);
    QTemporaryDir temp;
    const QString path = temp.filePath(QStringLiteral("movie.nfo"));
    QVERIFY(writeFile(path, "<movie><id>A-1</id>" + fields + "</movie>"));
    QString error;
    const auto parsed = nfo::parseNfo(path, mdcz, &error);
    QVERIFY2(parsed.has_value(), qPrintable(error));
    QCOMPARE(parsed->releaseDate, expected);
}
void NfoImportTest::directChildrenAndSceneSources()
{
    QTemporaryDir temp;
    const QString path = temp.filePath(QStringLiteral("movie.nfo"));
    QVERIFY(writeFile(path, R"(<movie><id>A-1</id><image>stray</image>
        <fanart><thumb>fanart</thumb><thumb preview="preview"/></fanart>
        <other><image>nested</image><mdcz><scene_images><image>nested-mdcz</image></scene_images></mdcz></other>
        <mdcz><image>stray-mdcz</image><scene_images><image>scene-1</image><other><image>nested-scene</image></other><image>scene-2</image></scene_images></mdcz>
        <mdcz><scene_images><image>second-mdcz</image></scene_images></mdcz></movie>)"));
    QString error;
    const auto ordinary = nfo::parseNfo(path, false, &error);
    const auto mdcz = nfo::parseNfo(path, true, &error);
    QVERIFY(ordinary); QVERIFY(mdcz);
    QCOMPARE(ordinary->fanart.size(), 2); QCOMPARE(ordinary->fanart[0].url, QStringLiteral("fanart")); QCOMPARE(ordinary->fanart[1].url, QStringLiteral("preview"));
    QCOMPARE(mdcz->fanart.size(), 2); QCOMPARE(mdcz->fanart[0].url, QStringLiteral("scene-1")); QCOMPARE(mdcz->fanart[1].url, QStringLiteral("scene-2"));
}
void NfoImportTest::relativeImagesUseWorkingDirectory()
{
    QTemporaryDir temp;
    QVERIFY(image(temp.filePath(QStringLiteral("nfo/cover.jpg"))));
    const QString path = temp.filePath(QStringLiteral("nfo/movie.nfo"));
    const QByteArray content("<movie><id>A-1</id><thumb>cover.jpg</thumb></movie>");
    QVERIFY(writeFile(path, content));
    const QString oldDirectory = QDir::currentPath();
    struct Restore { QString path; ~Restore() { QDir::setCurrent(path); } } restore{oldDirectory};
    QVERIFY(QDir::setCurrent(temp.path()));
    QString error;
    const auto parsed = nfo::parseNfo(path, false, &error);
    QVERIFY(parsed);
    QVERIFY(nfo::pickCover(*parsed).isEmpty()); // Only NFO-adjacent image exists.
    QVERIFY(image(temp.filePath(QStringLiteral("cover.jpg"))));
    QCOMPARE(nfo::pickCover(*parsed), QDir::current().filePath(QStringLiteral("cover.jpg")));
    QCOMPARE(nfo::resolveLocalImage(temp.filePath(QStringLiteral("nfo/cover.jpg"))), temp.filePath(QStringLiteral("nfo/cover.jpg")));
}
void NfoImportTest::blockedImages_data()
{
    QTest::addColumn<QString>("url"); QTest::addColumn<bool>("blocked");
    QTest::newRow("38 https") << QStringLiteral("https://www.javsee.in/a.jpg") << true;
    QTest::newRow("38 http mixed case") << QStringLiteral(" HTTP://WWW.JAVSEE.IN/a.jpg ") << true;
    QTest::newRow("38 literal Python prefix") << QStringLiteral("https://www.javsee.in.example/a.jpg") << true;
    QTest::newRow("38 without www follows Python") << QStringLiteral("https://javsee.in/a.jpg") << false;
    QTest::newRow("38 another host") << QStringLiteral("https://example.test/a.jpg") << false;
}
void NfoImportTest::blockedImages()
{
    QFETCH(QString, url); QFETCH(bool, blocked);
    QCOMPARE(nfo::isBlockedImage(url), blocked);
}
void NfoImportTest::mdczCastCreationAndFallback()
{
    QTemporaryDir temp;
    settings::Paths paths(temp.path(), QDir(temp.path()).filePath(QStringLiteral("data")));
    SqliteConnection connection;
    QString error;
    QVERIFY(connection.open(temp.filePath(QStringLiteral("public.db")), false, &error));
    QVERIFY2(SchemaManager::initializeEmptyDatabase(connection, DatabaseKind::Public, &error), qPrintable(error));
    const QString maleName = QStringLiteral("NfoMaleExportOnly");
    QVERIFY(writeFile(QDir(paths.configDirectory()).filePath(QStringLiteral("actors_cn_jp_export.json")), QJsonDocument(QJsonArray{maleName}).toJson()));
    const QString path = temp.filePath(QStringLiteral("movie.nfo"));
    QVERIFY(writeFile(path, "<movie><id>MDCZ-1</id><actor><name>NfoMaleExportOnly</name></actor></movie>"));
    QVERIFY2(nfo::importNfo(connection.database(), path, true, paths, &error), qPrintable(error));
    PersonRepository people(connection.database());
    QVERIFY(people.findByName(PersonKind::Actress, maleName));
    QVERIFY(!people.findByName(PersonKind::Actor, maleName));
    // Ordinary imports still use the exported male-name set.
    QVERIFY(writeFile(path, "<movie><id>ORD-1</id><actor><name>NfoMaleExportOther</name></actor><thumb>https://www.javsee.in/cover.jpg</thumb></movie>"));
    QVERIFY(writeFile(QDir(paths.configDirectory()).filePath(QStringLiteral("actors_cn_jp_export.json")), "[\"NfoMaleExportOther\"]"));
    QVERIFY2(nfo::importNfo(connection.database(), path, false, paths, &error), qPrintable(error));
    QVERIFY(people.findByName(PersonKind::Actor, QStringLiteral("NfoMaleExportOther")));
    WorkRepository works(connection.database());
    const auto ordinaryId = works.findIdBySerial(QStringLiteral("ORD-1"));
    QVERIFY(ordinaryId); const auto ordinary = works.findById(*ordinaryId); QVERIFY(ordinary); QVERIFY(ordinary->imageUrl.isEmpty());
    // Existing male actors retain their table in MDCZ.
    QVERIFY(people.create(PersonKind::Actor, QStringLiteral("ExistingMale"), QStringLiteral("ExistingMale")));
    QVERIFY(writeFile(path, "<movie><id>MDCZ-2</id><actor><name>ExistingMale</name></actor></movie>"));
    QVERIFY2(nfo::importNfo(connection.database(), path, true, paths, &error), qPrintable(error));
    QVERIFY(!people.findByName(PersonKind::Actress, QStringLiteral("ExistingMale")));
    // A rejected actress insertion exercises Python's actor fallback.
    QSqlQuery query(connection.database());
    QVERIFY(query.exec(QStringLiteral("CREATE TRIGGER reject_new_actress BEFORE INSERT ON actress BEGIN SELECT RAISE(FAIL, 'test rejection'); END")));
    QVERIFY(writeFile(path, "<movie><id>MDCZ-3</id><actor><name>FallbackMale</name></actor></movie>"));
    QVERIFY2(nfo::importNfo(connection.database(), path, true, paths, &error), qPrintable(error));
    QVERIFY(people.findByName(PersonKind::Actor, QStringLiteral("FallbackMale")));
}
void NfoImportTest::mdczFanartMigration()
{
    QTemporaryDir temp;
    settings::Paths paths(temp.filePath(QStringLiteral("app")), QDir(temp.filePath(QStringLiteral("app"))).filePath(QStringLiteral("data")));
    SqliteConnection connection;
    QString error;
    QVERIFY(connection.open(temp.filePath(QStringLiteral("public.db")), false, &error));
    QVERIFY2(SchemaManager::initializeEmptyDatabase(connection, DatabaseKind::Public, &error), qPrintable(error));
    const QString path = temp.filePath(QStringLiteral("movie/movie.nfo"));
    QVERIFY(writeFile(path, R"(<movie><id>MDCZ-40</id><fanart><thumb>https://example.test/ignored.jpg</thumb></fanart>
        <image>https://example.test/stray.jpg</image><mdcz><scene_images>
        <image>https://example.test/unmatched.jpg</image><image>https://example.test/matched.jpg</image>
        <image>https://example.test/matched.jpg</image><image>https://example.test/remote.jpg</image>
        </scene_images></mdcz></movie>)"));
    const QString matched = temp.filePath(QStringLiteral("movie/extrafanart/matched.jpg"));
    const QString fallback = temp.filePath(QStringLiteral("movie/extrafanart/a.png"));
    QVERIFY(image(matched)); QVERIFY(image(fallback));
    QVERIFY2(nfo::importNfo(connection.database(), path, true, paths, &error), qPrintable(error));
    WorkRepository works(connection.database());
    const auto id = works.findIdBySerial(QStringLiteral("MDCZ-40")); QVERIFY(id);
    const auto work = works.findById(*id); QVERIFY(work);
    const auto fanart = QJsonDocument::fromJson(work->fanartJson.toUtf8()).array();
    QCOMPARE(fanart.size(), 3);
    QCOMPARE(fanart[0].toObject().value(QStringLiteral("url")).toString(), QStringLiteral("https://example.test/matched.jpg"));
    QCOMPARE(fanart[0].toObject().value(QStringLiteral("file")).toString(), QStringLiteral("matched.jpg"));
    QCOMPARE(fanart[1].toObject().value(QStringLiteral("file")).toString(), QStringLiteral("unmatched.jpg"));
    QCOMPARE(fanart[2].toObject().value(QStringLiteral("file")).toString(), QString{});
    QVERIFY(!QFileInfo::exists(matched)); QVERIFY(!QFileInfo::exists(fallback));
    QVERIFY(QFileInfo::exists(QDir(paths.fanartDirectory()).filePath(QStringLiteral("matched.jpg"))));
    QVERIFY(QFileInfo::exists(QDir(paths.fanartDirectory()).filePath(QStringLiteral("unmatched.jpg"))));
}
QTEST_GUILESS_MAIN(NfoImportTest)
#include "NfoImportTest.moc"
