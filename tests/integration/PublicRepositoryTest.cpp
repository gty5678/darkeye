#include "database/SchemaManager.h"
#include "database/repositories/PersonRepository.h"
#include "database/repositories/ReferenceRepository.h"
#include "database/repositories/WorkRepository.h"
#include "services/ReferenceJsonService.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSqlError>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QtTest>

class PublicRepositoryTest final : public QObject
{
    Q_OBJECT

private slots:
    void createsAndFindsPeopleAndReferences();
    void searchesPeopleWithStableFiltersAndOrdering();
    void updatesPersonDetailsAndNameChains();
    void managesAndRedirectsReferenceRecordsAtomically();
    void managesTagTypesAliasesAndRedirectsAtomically();
    void importsAndExportsReferenceJsonAtomically();
    void batchesWorkDeletionRestorationAndPermanentRemoval();
};

void PublicRepositoryTest::createsAndFindsPeopleAndReferences()
{
    QTemporaryDir temporaryDirectory;
    QVERIFY(temporaryDirectory.isValid());
    darkeye::SqliteConnection connection;
    QString errorMessage;
    QVERIFY2(connection.open(QDir(temporaryDirectory.path()).filePath("public.db"), false,
                             &errorMessage),
             qPrintable(errorMessage));
    QVERIFY2(darkeye::SchemaManager::initializeEmptyDatabase(
                 connection, darkeye::DatabaseKind::Public, &errorMessage),
             qPrintable(errorMessage));

    darkeye::PersonRepository people(connection.database());
    const auto actressId = people.create(darkeye::PersonKind::Actress, QStringLiteral("中文名"),
                                         QStringLiteral("日本名"), &errorMessage);
    QVERIFY2(actressId.has_value(), qPrintable(errorMessage));
    QCOMPARE(people.findByName(darkeye::PersonKind::Actress, QStringLiteral("日本名")), actressId);

    darkeye::ReferenceRepository references(connection.database());
    const auto makerId =
        references.create(darkeye::ReferenceKind::Maker, QStringLiteral("Maker"), &errorMessage);
    QVERIFY2(makerId.has_value(), qPrintable(errorMessage));
    QCOMPARE(references.findByName(darkeye::ReferenceKind::Maker, QStringLiteral("Maker")),
             makerId);
    const auto typeId = references.createTagType(QStringLiteral("类型"), 1, &errorMessage);
    QVERIFY2(typeId.has_value(), qPrintable(errorMessage));
    QVERIFY2(references
                 .createTag(QStringLiteral("标签"), typeId, QStringLiteral("#112233"), QString(),
                            &errorMessage)
                 .has_value(),
             qPrintable(errorMessage));
    QVERIFY(!references
                 .createTag(QStringLiteral("标签"), typeId, QStringLiteral("#112233"), QString(),
                            &errorMessage)
                 .has_value());
}

void PublicRepositoryTest::searchesPeopleWithStableFiltersAndOrdering()
{
    QTemporaryDir temporaryDirectory;
    QVERIFY(temporaryDirectory.isValid());
    darkeye::SqliteConnection connection;
    QString errorMessage;
    QVERIFY2(connection.open(QDir(temporaryDirectory.path()).filePath("public.db"), false,
                             &errorMessage),
             qPrintable(errorMessage));
    QVERIFY2(darkeye::SchemaManager::initializeEmptyDatabase(
                 connection, darkeye::DatabaseKind::Public, &errorMessage),
             qPrintable(errorMessage));

    darkeye::PersonRepository people(connection.database());
    const auto first = people.create(darkeye::PersonKind::Actress, QStringLiteral("百分%号"),
                                     QStringLiteral("第一"), &errorMessage);
    const auto second = people.create(darkeye::PersonKind::Actress, QStringLiteral("下划_线"),
                                      QStringLiteral("第二"), &errorMessage);
    QVERIFY2(first.has_value() && second.has_value(), qPrintable(errorMessage));
    QSqlQuery update(connection.database());
    update.prepare(QStringLiteral("UPDATE actress SET cup=?, height=?, image_urlA=?, create_time=? "
                                  "WHERE actress_id=?"));
    update.addBindValue(QStringLiteral("D"));
    update.addBindValue(165);
    update.addBindValue(QStringLiteral("first.jpg"));
    update.addBindValue(QStringLiteral("2026-01-01 00:00:00"));
    update.addBindValue(*first);
    QVERIFY(update.exec());
    update.prepare(
        QStringLiteral("UPDATE actress SET cup=?, height=?, create_time=? WHERE actress_id=?"));
    update.addBindValue(QStringLiteral("E"));
    update.addBindValue(170);
    update.addBindValue(QStringLiteral("2026-02-01 00:00:00"));
    update.addBindValue(*second);
    QVERIFY(update.exec());

    darkeye::PersonSearch search;
    search.kind = darkeye::PersonKind::Actress;
    QCOMPARE(people.count(search), std::optional<int>(2));
    QCOMPARE(people.search(search).first().id, *second);
    search.name = QStringLiteral("%");
    QCOMPARE(people.count(search), std::optional<int>(1));
    QCOMPARE(people.search(search).first().id, *first);
    search.name.clear();
    search.cup = QStringLiteral("D");
    QCOMPARE(people.search(search).first().id, *first);
    search.cup.clear();
    search.restrictToIncludedIds = true;
    search.includedIds = {*second};
    QCOMPARE(people.count(search), std::optional<int>(1));
    QCOMPARE(people.search(search).first().id, *second);
    search.includedIds.clear();
    QCOMPARE(people.count(search), std::optional<int>(0));
    QCOMPARE(people.cupOptions(), QStringList({QStringLiteral("D"), QStringLiteral("E")}));
    QVERIFY(people.nameSuggestions(darkeye::PersonKind::Actress).contains(QStringLiteral("第一")));
}

void PublicRepositoryTest::updatesPersonDetailsAndNameChains()
{
    QTemporaryDir temporaryDirectory;
    QVERIFY(temporaryDirectory.isValid());
    darkeye::SqliteConnection connection;
    QString errorMessage;
    QVERIFY2(connection.open(QDir(temporaryDirectory.path()).filePath("public.db"), false,
                             &errorMessage),
             qPrintable(errorMessage));
    QVERIFY2(darkeye::SchemaManager::initializeEmptyDatabase(
                 connection, darkeye::DatabaseKind::Public, &errorMessage),
             qPrintable(errorMessage));

    darkeye::PersonRepository people(connection.database());
    const auto actressId = people.create(darkeye::PersonKind::Actress, QStringLiteral("初始名"),
                                         QStringLiteral("初期名"), &errorMessage);
    QVERIFY2(actressId.has_value(), qPrintable(errorMessage));
    auto details = people.findDetails(darkeye::PersonKind::Actress, *actressId, &errorMessage);
    QVERIFY2(details.has_value(), qPrintable(errorMessage));
    details->birthday = QStringLiteral("2001-02-03");
    details->height = 166;
    details->bust = 88;
    details->waist = 58;
    details->hip = 87;
    details->cup = QStringLiteral("E");
    details->debutDate = QStringLiteral("2021-04-05");
    details->needUpdate = false;
    details->minnanoUrl = QStringLiteral("12345");
    details->notes = QStringLiteral("人物备注");
    details->names = {{0, QStringLiteral("现用名"), QStringLiteral("現在名"),
                       QStringLiteral("Current"), QStringLiteral("げんざい")},
                      {0, QStringLiteral("曾用名"), QStringLiteral("旧名"),
                       QStringLiteral("Former"), QStringLiteral("きゅう")}};
    QVERIFY2(people.updateDetails(*details, &errorMessage), qPrintable(errorMessage));

    const auto saved = people.findDetails(darkeye::PersonKind::Actress, *actressId, &errorMessage);
    QVERIFY2(saved.has_value(), qPrintable(errorMessage));
    QCOMPARE(saved->height, std::optional<int>(166));
    QCOMPARE(saved->cup, QStringLiteral("E"));
    QCOMPARE(saved->names.size(), 2);
    QCOMPARE(saved->names.first().chinese, QStringLiteral("现用名"));
    QCOMPARE(saved->names.last().japanese, QStringLiteral("旧名"));
    QCOMPARE(saved->notes, QStringLiteral("人物备注"));

    // Editing an existing name must preserve its primary key, just as the
    // Python ModifyActressPage sends actress_name_id back on submit.  This is
    // essential when another table holds a reference to the name row.
    const qint64 primaryNameId = saved->names.first().id;
    darkeye::PersonDetails renamed = *saved;
    renamed.names.first().chinese = QStringLiteral("现用名（已修改）");
    QVERIFY2(people.updateDetails(renamed, &errorMessage), qPrintable(errorMessage));
    const auto renamedSaved =
        people.findDetails(darkeye::PersonKind::Actress, *actressId, &errorMessage);
    QVERIFY2(renamedSaved.has_value(), qPrintable(errorMessage));
    QCOMPARE(renamedSaved->names.first().id, primaryNameId);
    QCOMPARE(renamedSaved->names.first().chinese, QStringLiteral("现用名（已修改）"));

    QSqlQuery chain(connection.database());
    chain.prepare(
        QStringLiteral("SELECT redirect_actress_name_id FROM actress_name WHERE actress_id=? "
                       "ORDER BY actress_name_id"));
    chain.addBindValue(*actressId);
    QVERIFY(chain.exec());
    QVERIFY(chain.next());
    const qint64 headId = renamedSaved->names.first().id;
    QVERIFY(chain.value(0).isNull());
    QVERIFY(chain.next());
    QCOMPARE(chain.value(0).toLongLong(), headId);

    darkeye::PersonDetails invalid = *renamedSaved;
    invalid.names = {{0, {}, {}, {}, {}}};
    QVERIFY(!people.updateDetails(invalid, &errorMessage));
    QCOMPARE(people.findDetails(darkeye::PersonKind::Actress, *actressId)->names.first().chinese,
             QStringLiteral("现用名（已修改）"));

    const auto actorId = people.create(darkeye::PersonKind::Actor, QStringLiteral("男演员"),
                                       QStringLiteral("男優"), &errorMessage);
    QVERIFY2(actorId.has_value(), qPrintable(errorMessage));
    auto actor = people.findDetails(darkeye::PersonKind::Actor, *actorId, &errorMessage);
    QVERIFY(actor.has_value());
    actor->handsome = 0;
    actor->fat = 2;
    actor->notes = QStringLiteral("演员备注");
    QVERIFY2(people.updateDetails(*actor, &errorMessage), qPrintable(errorMessage));
    const auto savedActor = people.findDetails(darkeye::PersonKind::Actor, *actorId);
    QCOMPARE(savedActor->handsome, std::optional<int>(0));
    QCOMPARE(savedActor->fat, std::optional<int>(2));
}

void PublicRepositoryTest::managesAndRedirectsReferenceRecordsAtomically()
{
    QTemporaryDir temporaryDirectory;
    QVERIFY(temporaryDirectory.isValid());
    darkeye::SqliteConnection connection;
    QString errorMessage;
    QVERIFY2(connection.open(QDir(temporaryDirectory.path()).filePath("public.db"), false,
                             &errorMessage),
             qPrintable(errorMessage));
    QVERIFY2(darkeye::SchemaManager::initializeEmptyDatabase(
                 connection, darkeye::DatabaseKind::Public, &errorMessage),
             qPrintable(errorMessage));

    // These CRUD cases own their reference rows; initialization also imports built-in data.
    QSqlQuery clearDefaults(connection.database());
    QVERIFY(clearDefaults.exec(QStringLiteral("DELETE FROM prefix_maker_relation")));
    QVERIFY(clearDefaults.exec(QStringLiteral("DELETE FROM maker")));

    darkeye::ReferenceRepository references(connection.database());
    darkeye::ReferenceRecord source{darkeye::ReferenceKind::Maker, 0,
                                    QStringLiteral("旧片商"),      QStringLiteral("旧メーカー"),
                                    QStringLiteral("旧别名"),      QStringLiteral("旧说明"),
                                    QStringLiteral("old.png")};
    darkeye::ReferenceRecord target{darkeye::ReferenceKind::Maker, 0,
                                    QStringLiteral("保留片商"),    QStringLiteral("保留メーカー"),
                                    QStringLiteral("已有别名"),    QStringLiteral("保留说明"),
                                    QStringLiteral("new.png")};
    const auto sourceId = references.create(source, &errorMessage);
    const auto targetId = references.create(target, &errorMessage);
    QVERIFY2(sourceId.has_value() && targetId.has_value(), qPrintable(errorMessage));

    target.id = *targetId;
    target.detail = QStringLiteral("更新后的说明");
    QVERIFY2(references.update(target, &errorMessage), qPrintable(errorMessage));
    QCOMPARE(references.list(darkeye::ReferenceKind::Maker).first().detail,
             QStringLiteral("更新后的说明"));

    const auto prefixId =
        references.createMakerPrefix(QStringLiteral(" ABP "), *sourceId, &errorMessage);
    const auto duplicatePrefixId =
        references.createMakerPrefix(QStringLiteral("ABP"), *targetId, &errorMessage);
    QVERIFY2(prefixId.has_value() && duplicatePrefixId.has_value(), qPrintable(errorMessage));
    QCOMPARE(references.listMakerPrefixes().size(), 2);
    QVERIFY2(references.updateMakerPrefix({*prefixId, QStringLiteral("IPX"), *sourceId, {}},
                                          &errorMessage),
             qPrintable(errorMessage));
    QVERIFY2(references.removeMakerPrefix(*duplicatePrefixId, &errorMessage),
             qPrintable(errorMessage));
    QCOMPARE(references.listMakerPrefixes().first().prefix, QStringLiteral("IPX"));

    QSqlQuery setup(connection.database());
    setup.prepare(QStringLiteral("INSERT INTO work(serial_number, maker_id) VALUES(?, ?)"));
    setup.addBindValue(QStringLiteral("REF-001"));
    setup.addBindValue(*sourceId);
    QVERIFY2(setup.exec(), qPrintable(setup.lastError().text()));
    setup.prepare(
        QStringLiteral("INSERT INTO prefix_maker_relation(prefix, maker_id) VALUES(?, ?)"));
    setup.addBindValue(QStringLiteral("REF"));
    setup.addBindValue(*sourceId);
    QVERIFY2(setup.exec(), qPrintable(setup.lastError().text()));

    QVERIFY(!references.remove(darkeye::ReferenceKind::Maker, *sourceId, &errorMessage));
    QVERIFY(
        references.findByName(darkeye::ReferenceKind::Maker, QStringLiteral("旧片商")).has_value());
    QVERIFY(!references.redirect(darkeye::ReferenceKind::Maker, *sourceId, 999999, &errorMessage));
    QVERIFY(
        references.findByName(darkeye::ReferenceKind::Maker, QStringLiteral("旧片商")).has_value());
    QVERIFY2(
        references.redirect(darkeye::ReferenceKind::Maker, *sourceId, *targetId, &errorMessage),
        qPrintable(errorMessage));

    const QList<darkeye::ReferenceRecord> saved =
        references.list(darkeye::ReferenceKind::Maker, &errorMessage);
    QCOMPARE(saved.size(), 1);
    QCOMPARE(saved.first().id, *targetId);
    QVERIFY(saved.first().aliases.contains(QStringLiteral("已有别名")));
    QVERIFY(saved.first().aliases.contains(QStringLiteral("旧片商")));
    QVERIFY(saved.first().aliases.contains(QStringLiteral("旧メーカー")));
    QVERIFY(saved.first().aliases.contains(QStringLiteral("旧别名")));
    QSqlQuery verify(connection.database());
    QVERIFY(verify.exec(QStringLiteral("SELECT maker_id FROM work WHERE serial_number='REF-001'")));
    QVERIFY(verify.next());
    QCOMPARE(verify.value(0).toLongLong(), *targetId);
    QVERIFY(verify.exec(
        QStringLiteral("SELECT maker_id FROM prefix_maker_relation WHERE prefix='REF'")));
    QVERIFY(verify.next());
    QCOMPARE(verify.value(0).toLongLong(), *targetId);
    for (const darkeye::MakerPrefixRecord &prefix : references.listMakerPrefixes())
        QCOMPARE(prefix.makerId, *targetId);
}

void PublicRepositoryTest::managesTagTypesAliasesAndRedirectsAtomically()
{
    QTemporaryDir temporaryDirectory;
    QVERIFY(temporaryDirectory.isValid());
    darkeye::SqliteConnection connection;
    QString errorMessage;
    QVERIFY2(connection.open(QDir(temporaryDirectory.path()).filePath("public.db"), false,
                             &errorMessage),
             qPrintable(errorMessage));
    QVERIFY2(darkeye::SchemaManager::initializeEmptyDatabase(
                 connection, darkeye::DatabaseKind::Public, &errorMessage),
             qPrintable(errorMessage));

    // These CRUD cases own their reference rows; initialization also imports built-in data.
    QSqlQuery clearDefaults(connection.database());
    QVERIFY(clearDefaults.exec(QStringLiteral("DELETE FROM tag")));
    QVERIFY(clearDefaults.exec(QStringLiteral("DELETE FROM tag_type")));

    darkeye::ReferenceRepository references(connection.database());
    const auto storyType = references.createTagType(QStringLiteral("剧情"), 2, &errorMessage);
    const auto poseType = references.createTagType(QStringLiteral("体位"), 1, &errorMessage);
    QVERIFY2(storyType.has_value() && poseType.has_value(), qPrintable(errorMessage));
    QCOMPARE(references.listTagTypes().first().id, *poseType);
    QVERIFY2(references.moveTagType(*storyType, -1, &errorMessage), qPrintable(errorMessage));
    QCOMPARE(references.listTagTypes().first().id, *storyType);
    const auto performerType = references.createTagType(QStringLiteral("演员"), 3, &errorMessage);
    QVERIFY2(performerType.has_value(), qPrintable(errorMessage));
    QVERIFY2(references.moveTagType(*storyType, 2, &errorMessage), qPrintable(errorMessage));
    const QList<darkeye::TagTypeRecord> reorderedTypes = references.listTagTypes();
    QCOMPARE(reorderedTypes.at(0).id, *poseType);
    QCOMPARE(reorderedTypes.at(1).id, *performerType);
    QCOMPARE(reorderedTypes.at(2).id, *storyType);

    darkeye::TagRecord source;
    source.name = QStringLiteral("旧标签");
    source.typeId = storyType;
    source.color = QStringLiteral("#112233");
    source.detail = QStringLiteral("来源标签");
    source.aliases = {QStringLiteral("旧别名一"), QStringLiteral("旧别名二")};
    darkeye::TagRecord target;
    target.name = QStringLiteral("保留标签");
    target.typeId = poseType;
    target.color = QStringLiteral("#445566");
    target.aliases = {QStringLiteral("保留别名")};
    const auto sourceId = references.createTag(source, &errorMessage);
    const auto targetId = references.createTag(target, &errorMessage);
    QVERIFY2(sourceId.has_value() && targetId.has_value(), qPrintable(errorMessage));
    QCOMPARE(references.listTags().size(), 2);

    QSqlQuery setup(connection.database());
    QVERIFY(setup.exec(
        QStringLiteral("INSERT INTO work(serial_number) VALUES('TAG-001'), ('TAG-002')")));
    QVERIFY(setup.exec(
        QStringLiteral("INSERT INTO work_tag_relation(work_id, tag_id) "
                       "SELECT work_id, %1 FROM work WHERE serial_number IN ('TAG-001', 'TAG-002')")
            .arg(*sourceId)));
    QVERIFY(setup.exec(QStringLiteral("INSERT INTO work_tag_relation(work_id, tag_id) "
                                      "SELECT work_id, %1 FROM work WHERE serial_number='TAG-001'")
                           .arg(*targetId)));

    QVERIFY(!references.redirectTag(*sourceId, 999999, &errorMessage));
    QCOMPARE(references.listTags().size(), 2);
    QVERIFY2(references.redirectTag(*sourceId, *targetId, &errorMessage), qPrintable(errorMessage));
    QList<darkeye::TagRecord> tags = references.listTags(&errorMessage);
    QCOMPARE(tags.size(), 1);
    QCOMPARE(tags.first().id, *targetId);
    QVERIFY(tags.first().aliases.contains(QStringLiteral("旧标签")));
    QVERIFY(tags.first().aliases.contains(QStringLiteral("旧别名一")));
    QVERIFY(tags.first().aliases.contains(QStringLiteral("保留别名")));

    QSqlQuery verify(connection.database());
    QVERIFY(verify.exec(QStringLiteral(
        "SELECT work_id, COUNT(*) FROM work_tag_relation GROUP BY work_id ORDER BY work_id")));
    QVERIFY(verify.next());
    QCOMPARE(verify.value(1).toInt(), 1);
    QVERIFY(verify.next());
    QCOMPARE(verify.value(1).toInt(), 1);

    darkeye::TagRecord updated = tags.first();
    updated.color = QStringLiteral("#abcdef");
    updated.detail = QStringLiteral("更新说明");
    updated.aliases.append(QStringLiteral("新增别名"));
    QVERIFY2(references.updateTag(updated, &errorMessage), qPrintable(errorMessage));
    tags = references.listTags(&errorMessage);
    QCOMPARE(tags.first().color, QStringLiteral("#abcdef"));
    QCOMPARE(tags.first().detail, QStringLiteral("更新说明"));
    QVERIFY(tags.first().aliases.contains(QStringLiteral("新增别名")));
    QVERIFY(!references.removeTag(*targetId, &errorMessage));
    QVERIFY(!references.removeTagType(*poseType, &errorMessage));

    darkeye::TagRecord unused;
    unused.name = QStringLiteral("未使用标签");
    unused.color = QStringLiteral("#cccccc");
    const auto unusedId = references.createTag(unused, &errorMessage);
    QVERIFY2(unusedId.has_value(), qPrintable(errorMessage));
    QVERIFY2(references.removeTag(*unusedId, &errorMessage), qPrintable(errorMessage));
    QCOMPARE(references.listTags().size(), 1);
}

void PublicRepositoryTest::importsAndExportsReferenceJsonAtomically()
{
    QTemporaryDir temporaryDirectory;
    QVERIFY(temporaryDirectory.isValid());
    darkeye::SqliteConnection connection;
    QString errorMessage;
    QVERIFY2(connection.open(QDir(temporaryDirectory.path()).filePath("public.db"), false,
                             &errorMessage),
             qPrintable(errorMessage));
    QVERIFY2(darkeye::SchemaManager::initializeEmptyDatabase(
                 connection, darkeye::DatabaseKind::Public, &errorMessage),
             qPrintable(errorMessage));
    darkeye::ReferenceRepository references(connection.database());
    const auto makerId = references.create(
        {darkeye::ReferenceKind::Maker, 0, QStringLiteral("旧片商"), QStringLiteral("旧メーカー"),
         QString(), QStringLiteral("旧说明"), QStringLiteral("old.png")},
        &errorMessage);
    const auto unusedMakerId = references.create(darkeye::ReferenceKind::Maker,
                                                 QStringLiteral("未引用片商"), &errorMessage);
    QVERIFY2(makerId.has_value() && unusedMakerId.has_value(), qPrintable(errorMessage));
    QVERIFY2(
        references.createMakerPrefix(QStringLiteral("OLD"), *makerId, &errorMessage).has_value(),
        qPrintable(errorMessage));
    QSqlQuery setup(connection.database());
    setup.prepare(QStringLiteral("INSERT INTO work(serial_number, maker_id) VALUES(?, ?)"));
    setup.addBindValue(QStringLiteral("JSON-001"));
    setup.addBindValue(*makerId);
    QVERIFY2(setup.exec(), qPrintable(setup.lastError().text()));

    const QString makerPath = QDir(temporaryDirectory.path()).filePath("maker_prefix.json");
    QJsonObject importedMaker{
        {QStringLiteral("cn_name"), QStringLiteral("新片商")},
        {QStringLiteral("jp_name"), QJsonValue::Null},
        {QStringLiteral("aliases"), QStringLiteral("旧片商,另一别名")},
        {QStringLiteral("detail"), QStringLiteral("新说明")},
        {QStringLiteral("logo_url"), QStringLiteral("new.png")},
        {QStringLiteral("prefixes"), QJsonArray{QStringLiteral("NEW"), QStringLiteral("NEW")}}};
    QFile makerFile(makerPath);
    QVERIFY(makerFile.open(QIODevice::WriteOnly));
    QCOMPARE(makerFile.write(QJsonDocument(QJsonArray{importedMaker}).toJson()),
             QJsonDocument(QJsonArray{importedMaker}).toJson().size());
    makerFile.close();

    darkeye::ReferenceJsonService service(connection.database());
    QVERIFY2(service.importFromFile(darkeye::ReferenceKind::Maker, makerPath, &errorMessage),
             qPrintable(errorMessage));
    const QList<darkeye::ReferenceRecord> makers =
        references.list(darkeye::ReferenceKind::Maker, &errorMessage);
    QCOMPARE(makers.size(), 1);
    QCOMPARE(makers.first().id, *makerId);
    QCOMPARE(makers.first().chineseName, QStringLiteral("新片商"));
    QCOMPARE(references.listMakerPrefixes().size(), 1);
    QCOMPARE(references.listMakerPrefixes().first().prefix, QStringLiteral("NEW"));
    QVERIFY(setup.exec(QStringLiteral("SELECT maker_id FROM work WHERE serial_number='JSON-001'")));
    QVERIFY(setup.next());
    QCOMPARE(setup.value(0).toLongLong(), *makerId);

    const QString exportPath = QDir(temporaryDirectory.path()).filePath("exported.json");
    QVERIFY2(service.exportToFile(darkeye::ReferenceKind::Maker, exportPath, &errorMessage),
             qPrintable(errorMessage));
    QFile exportedFile(exportPath);
    QVERIFY(exportedFile.open(QIODevice::ReadOnly));
    const QJsonArray exported = QJsonDocument::fromJson(exportedFile.readAll()).array();
    QCOMPARE(exported.size(), 1);
    QVERIFY(!exported.first().toObject().contains(QStringLiteral("maker_id")));
    QVERIFY(exported.first().toObject().value(QStringLiteral("jp_name")).isNull());
    QCOMPARE(exported.first().toObject().value(QStringLiteral("prefixes")).toArray().size(), 1);

    const QString invalidPath = QDir(temporaryDirectory.path()).filePath("invalid.json");
    QFile invalidFile(invalidPath);
    QVERIFY(invalidFile.open(QIODevice::WriteOnly));
    QVERIFY(invalidFile.write("{\"not\":\"a list\"}") > 0);
    invalidFile.close();
    QVERIFY(!service.importFromFile(darkeye::ReferenceKind::Maker, invalidPath, &errorMessage));
    QCOMPARE(references.list(darkeye::ReferenceKind::Maker).first().chineseName,
             QStringLiteral("新片商"));

    for (const auto &[kind, extraKey] : QList<QPair<darkeye::ReferenceKind, QString>>{
             {darkeye::ReferenceKind::Label, QString()},
             {darkeye::ReferenceKind::Series, QStringLiteral("related_series")}})
    {
        QJsonObject object{{QStringLiteral("cn_name"), QStringLiteral("导入资料")},
                           {QStringLiteral("jp_name"), QJsonValue::Null},
                           {QStringLiteral("aliases"), QJsonValue::Null},
                           {QStringLiteral("detail"), QStringLiteral("说明")}};
        if (!extraKey.isEmpty())
            object.insert(extraKey, QStringLiteral("12,13"));
        const QString path =
            QDir(temporaryDirectory.path())
                .filePath(QStringLiteral("kind-%1.json").arg(static_cast<int>(kind)));
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        QVERIFY(file.write(QJsonDocument(QJsonArray{object}).toJson()) > 0);
        file.close();
        QVERIFY2(service.importFromFile(kind, path, &errorMessage), qPrintable(errorMessage));
        const QList<darkeye::ReferenceRecord> records = references.list(kind, &errorMessage);
        QCOMPARE(records.size(), 1);
        QCOMPARE(records.first().chineseName, QStringLiteral("导入资料"));
        if (!extraKey.isEmpty())
            QCOMPARE(records.first().extra, QStringLiteral("12,13"));
    }
}

void PublicRepositoryTest::batchesWorkDeletionRestorationAndPermanentRemoval()
{
    QTemporaryDir temporaryDirectory;
    QVERIFY(temporaryDirectory.isValid());
    darkeye::SqliteConnection connection;
    QString errorMessage;
    QVERIFY2(connection.open(QDir(temporaryDirectory.path()).filePath("public.db"), false,
                             &errorMessage),
             qPrintable(errorMessage));
    QVERIFY2(darkeye::SchemaManager::initializeEmptyDatabase(
                 connection, darkeye::DatabaseKind::Public, &errorMessage),
             qPrintable(errorMessage));
    darkeye::WorkRepository works(connection.database());
    darkeye::Work first;
    first.serialNumber = QStringLiteral("STATE-001");
    first.chineseTitle = QStringLiteral("保留作品");
    first.imageUrl = QStringLiteral("STATE-001.jpg");
    darkeye::Work second;
    second.serialNumber = QStringLiteral("STATE-002");
    second.japaneseTitle = QStringLiteral("削除作品");
    second.imageUrl = QStringLiteral("STATE-002.jpg");
    second.fanartJson = QStringLiteral(
        "[{\"url\":\"https://example.invalid/one.jpg\",\"file\":\"STATE-002-fa.jpg\"},"
        "{\"url\":\"\",\"file\":\"../outside.jpg\"}]");
    const auto firstId = works.insertComplete(first, {}, {}, {}, &errorMessage);
    const auto secondId = works.insertComplete(second, {}, {}, {}, &errorMessage);
    QVERIFY2(firstId.has_value() && secondId.has_value(), qPrintable(errorMessage));

    darkeye::ReferenceRepository references(connection.database());
    const auto typeId = references.createTagType(QStringLiteral("状态测试"), 1, &errorMessage);
    darkeye::TagRecord tag;
    tag.name = QStringLiteral("关系保留测试");
    tag.typeId = typeId;
    const auto tagId = references.createTag(tag, &errorMessage);
    QVERIFY2(tagId.has_value(), qPrintable(errorMessage));
    QSqlQuery destroyRelation(connection.database());
    destroyRelation.prepare(
        QStringLiteral("INSERT INTO work_tag_relation(work_id, tag_id) VALUES(?, ?)"));
    destroyRelation.addBindValue(*secondId);
    destroyRelation.addBindValue(*tagId);
    QVERIFY2(destroyRelation.exec(), qPrintable(destroyRelation.lastError().text()));

    QVERIFY2(works.setDeletedMany({*firstId, *secondId}, true, &errorMessage),
             qPrintable(errorMessage));
    QCOMPARE(works.listByDeletedState(true).size(), 2);
    QCOMPARE(works.listByDeletedState(true, QStringLiteral("削除")).size(), 1);
    QCOMPARE(works.listByDeletedState(true, QStringLiteral("STATE-001")).first().id, *firstId);
    QVERIFY(works.listByDeletedState(true, QStringLiteral("%"), &errorMessage).isEmpty());
    QVERIFY(works.listByDeletedState(true, QStringLiteral("_"), &errorMessage).isEmpty());
    QVERIFY(works.listByDeletedState(true, QStringLiteral("' OR 1=1 --"), &errorMessage).isEmpty());
    QVERIFY(!works.setDeletedMany({*firstId, 999999}, false, &errorMessage));
    QCOMPARE(works.listByDeletedState(true).size(), 2);

    QVERIFY2(works.setDeletedMany({*firstId}, false, &errorMessage), qPrintable(errorMessage));
    QCOMPARE(works.listByDeletedState(false).size(), 1);
    QCOMPARE(works.listByDeletedState(true).size(), 1);
    QVERIFY(!works.permanentlyRemoveDeletedMany({*firstId}, nullptr, &errorMessage));
    QVERIFY(works.findById(*firstId).has_value());

    QStringList removedImages;
    QStringList removedFanart;
    QVERIFY2(works.permanentlyRemoveDeletedMany({*secondId}, &removedImages, &errorMessage,
                                                &removedFanart),
             qPrintable(errorMessage));
    QCOMPARE(removedImages, QStringList{QStringLiteral("STATE-002.jpg")});
    QCOMPARE(removedFanart, QStringList{QStringLiteral("STATE-002-fa.jpg")});
    QVERIFY(!works.findById(*secondId).has_value());
    QSqlQuery relation(connection.database());
    relation.prepare(QStringLiteral("SELECT COUNT(*) FROM work_tag_relation WHERE work_id=?"));
    relation.addBindValue(*secondId);
    QVERIFY(relation.exec());
    QVERIFY(relation.next());
    QCOMPARE(relation.value(0).toInt(), 0);
}

QTEST_MAIN(PublicRepositoryTest)
#include "PublicRepositoryTest.moc"
