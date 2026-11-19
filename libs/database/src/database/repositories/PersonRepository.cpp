#include "database/repositories/PersonRepository.h"

#include "database/Transaction.h"

#include <QSqlError>
#include <QSqlQuery>
#include <QSet>
#include <QVariantList>

namespace
{

QString likePattern(QString value)
{
    value = value.trimmed();
    value.replace(QStringLiteral("\\"), QStringLiteral("\\\\"));
    value.replace(QStringLiteral("%"), QStringLiteral("\\%"));
    value.replace(QStringLiteral("_"), QStringLiteral("\\_"));
    return QStringLiteral("%") + value + QStringLiteral("%");
}

struct PersonSearchClause final
{
    QString sql = QStringLiteral(" WHERE 1=1");
    QVariantList values;
};

PersonSearchClause buildSearchClause(const darkeye::PersonSearch &search,
                                     const QString &entityTable, const QString &nameTable,
                                     const QString &idColumn)
{
    PersonSearchClause clause;
    if (!search.name.trimmed().isEmpty())
    {
        clause.sql += QStringLiteral(" AND EXISTS (SELECT 1 FROM %1 filtered_name "
                                     "WHERE filtered_name.%2=%3.%2 AND ("
                                     "filtered_name.cn LIKE ? ESCAPE '\\' OR "
                                     "filtered_name.jp LIKE ? ESCAPE '\\' OR "
                                     "filtered_name.en LIKE ? ESCAPE '\\' OR "
                                     "filtered_name.kana LIKE ? ESCAPE '\\'))")
                          .arg(nameTable, idColumn, entityTable);
        const QString pattern = likePattern(search.name);
        for (int index = 0; index < 4; ++index)
            clause.values.append(pattern);
    }
    if (search.kind == darkeye::PersonKind::Actress && !search.cup.trimmed().isEmpty())
    {
        clause.sql += QStringLiteral(" AND actress.cup=?");
        clause.values.append(search.cup.trimmed());
    }
    if (search.sortOrder == darkeye::PersonSortOrder::BirthdayAscending ||
        search.sortOrder == darkeye::PersonSortOrder::BirthdayDescending)
    {
        clause.sql +=
            QStringLiteral(" AND %1.birthday IS NOT NULL AND %1.birthday!=''").arg(entityTable);
    }
    if (search.kind == darkeye::PersonKind::Actress &&
        (search.sortOrder == darkeye::PersonSortOrder::DebutAscending ||
         search.sortOrder == darkeye::PersonSortOrder::DebutDescending))
    {
        clause.sql +=
            QStringLiteral(" AND actress.debut_date IS NOT NULL AND actress.debut_date!=''");
    }
    if (search.kind == darkeye::PersonKind::Actress &&
        (search.sortOrder == darkeye::PersonSortOrder::WaistHipRatioAscending ||
         search.sortOrder == darkeye::PersonSortOrder::WaistHipRatioDescending))
    {
        clause.sql += QStringLiteral(" AND actress.waist IS NOT NULL AND actress.hip IS NOT NULL "
                                     "AND actress.hip!=0");
    }
    if (search.restrictToIncludedIds)
    {
        if (search.includedIds.isEmpty())
        {
            clause.sql += QStringLiteral(" AND 0");
        }
        else
        {
            QStringList placeholders;
            placeholders.fill(QStringLiteral("?"), search.includedIds.size());
            clause.sql += QStringLiteral(" AND %1.%2 IN (%3)")
                              .arg(entityTable, idColumn, placeholders.join(QLatin1Char(',')));
            for (const qint64 id : search.includedIds)
                clause.values.append(id);
        }
    }
    return clause;
}

bool bindAndExec(QSqlQuery &query, const QString &sql, const QVariantList &values,
                 QString *errorMessage)
{
    query.prepare(sql);
    for (const QVariant &value : values)
        query.addBindValue(value);
    if (query.exec())
        return true;
    if (errorMessage != nullptr)
        *errorMessage = query.lastError().text();
    return false;
}

QString sortClause(const darkeye::PersonSearch &search, const QString &table,
                   const QString &idColumn, QVariantList &values)
{
    using darkeye::PersonSortOrder;
    switch (search.sortOrder)
    {
    case PersonSortOrder::Random:
        values.append(search.randomSeed);
        values.append(search.randomSeed2);
        return QStringLiteral(" ORDER BY (((%1.%2 * ?) % 1000003) + "
                              "((%1.%2 * %1.%2 * ?) % 1000033)) % 1000037, %1.%2")
            .arg(table, idColumn);
    case PersonSortOrder::CreatedAscending:
        return QStringLiteral(" ORDER BY %1.create_time, %1.%2").arg(table, idColumn);
    case PersonSortOrder::ImageFirst:
        return QStringLiteral(" ORDER BY CASE WHEN %1.%2 IS NOT NULL AND %1.%2!='' "
                              "THEN 0 ELSE 1 END, %1.%3")
            .arg(table,
                 search.kind == darkeye::PersonKind::Actress ? QStringLiteral("image_urlA")
                                                             : QStringLiteral("image_url"),
                 idColumn);
    case PersonSortOrder::BirthdayAscending:
        return QStringLiteral(" ORDER BY %1.birthday DESC, %1.%2").arg(table, idColumn);
    case PersonSortOrder::BirthdayDescending:
        return QStringLiteral(" ORDER BY %1.birthday, %1.%2").arg(table, idColumn);
    case PersonSortOrder::DebutAscending:
        return QStringLiteral(" ORDER BY %1.debut_date, %1.%2").arg(table, idColumn);
    case PersonSortOrder::DebutDescending:
        return QStringLiteral(" ORDER BY %1.debut_date DESC, %1.%2").arg(table, idColumn);
    case PersonSortOrder::HeightAscending:
        return QStringLiteral(" ORDER BY %1.height, %1.%2").arg(table, idColumn);
    case PersonSortOrder::HeightDescending:
        return QStringLiteral(" ORDER BY %1.height DESC, %1.%2").arg(table, idColumn);
    case PersonSortOrder::CupAscending:
        return QStringLiteral(" ORDER BY %1.cup, %1.%2").arg(table, idColumn);
    case PersonSortOrder::CupDescending:
        return QStringLiteral(" ORDER BY %1.cup DESC, %1.%2").arg(table, idColumn);
    case PersonSortOrder::WaistHipRatioAscending:
        return QStringLiteral(" ORDER BY ROUND(%1.waist * 1.0 / NULLIF(%1.hip, 0), 2), %1.%2")
            .arg(table, idColumn);
    case PersonSortOrder::WaistHipRatioDescending:
        return QStringLiteral(" ORDER BY ROUND(%1.waist * 1.0 / NULLIF(%1.hip, 0), "
                              "2) DESC, %1.%2")
            .arg(table, idColumn);
    case PersonSortOrder::CreatedDescending:
    default:
        return QStringLiteral(" ORDER BY %1.create_time DESC, %1.%2").arg(table, idColumn);
    }
}

} // namespace

namespace darkeye
{

PersonRepository::PersonRepository(QSqlDatabase database) : m_database(std::move(database)) {}

std::optional<qint64> PersonRepository::create(PersonKind kind, const QString &chineseName,
                                               const QString &japaneseName, QString *errorMessage)
{
    if (chineseName.trimmed().isEmpty() && japaneseName.trimmed().isEmpty())
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = QStringLiteral("人物至少需要一个名称");
        }
        return std::nullopt;
    }
    const QString entityTable =
        kind == PersonKind::Actress ? QStringLiteral("actress") : QStringLiteral("actor");
    const QString nameTable =
        kind == PersonKind::Actress ? QStringLiteral("actress_name") : QStringLiteral("actor_name");
    const QString idColumn =
        kind == PersonKind::Actress ? QStringLiteral("actress_id") : QStringLiteral("actor_id");

    Transaction transaction(m_database);
    if (!transaction.isActive())
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = transaction.errorString();
        }
        return std::nullopt;
    }
    QSqlQuery entityQuery(m_database);
    if (!entityQuery.exec(QStringLiteral("INSERT INTO %1 DEFAULT VALUES").arg(entityTable)))
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = entityQuery.lastError().text();
        }
        return std::nullopt;
    }
    const qint64 personId = entityQuery.lastInsertId().toLongLong();

    QSqlQuery nameQuery(m_database);
    nameQuery.prepare(QStringLiteral("INSERT INTO %1(%2, name_type, cn, jp) VALUES(?, 1, ?, ?)")
                          .arg(nameTable, idColumn));
    nameQuery.addBindValue(personId);
    nameQuery.addBindValue(chineseName.trimmed());
    nameQuery.addBindValue(japaneseName.trimmed());
    if (!nameQuery.exec() || !transaction.commit())
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = nameQuery.lastError().text().isEmpty() ? transaction.errorString()
                                                                   : nameQuery.lastError().text();
        }
        return std::nullopt;
    }
    return personId;
}

std::optional<qint64> PersonRepository::findByName(PersonKind kind, const QString &name,
                                                   QString *errorMessage) const
{
    const QString table =
        kind == PersonKind::Actress ? QStringLiteral("actress_name") : QStringLiteral("actor_name");
    const QString idColumn =
        kind == PersonKind::Actress ? QStringLiteral("actress_id") : QStringLiteral("actor_id");
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral("SELECT %1 FROM %2 WHERE cn=? OR jp=? OR en=? LIMIT 1")
                      .arg(idColumn, table));
    const QString trimmedName = name.trimmed();
    query.addBindValue(trimmedName);
    query.addBindValue(trimmedName);
    query.addBindValue(trimmedName);
    if (!query.exec())
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = query.lastError().text();
        }
        return std::nullopt;
    }
    return query.next() ? std::optional<qint64>(query.value(0).toLongLong()) : std::nullopt;
}

QList<PersonSummary> PersonRepository::search(const PersonSearch &search,
                                              QString *errorMessage) const
{
    const bool actress = search.kind == PersonKind::Actress;
    const QString entityTable = actress ? QStringLiteral("actress") : QStringLiteral("actor");
    const QString nameTable =
        actress ? QStringLiteral("actress_name") : QStringLiteral("actor_name");
    const QString idColumn = actress ? QStringLiteral("actress_id") : QStringLiteral("actor_id");
    const QString imageColumn =
        actress ? QStringLiteral("image_urlA") : QStringLiteral("image_url");
    PersonSearchClause clause = buildSearchClause(search, entityTable, nameTable, idColumn);
    const QString nameIdColumn =
        actress ? QStringLiteral("actress_name_id") : QStringLiteral("actor_name_id");
    QString sql =
        QStringLiteral("SELECT %1.%2, COALESCE((SELECT COALESCE(NULLIF(primary_name.cn,''), "
                       "NULLIF(primary_name.jp,''), NULLIF(primary_name.en,''), "
                       "NULLIF(primary_name.kana,'')) FROM %4 primary_name "
                       "WHERE primary_name.%2=%1.%2 ORDER BY primary_name.name_type DESC, "
                       "primary_name.%5 LIMIT 1), '未命名'), %1.%3 FROM %1")
            .arg(entityTable, idColumn, imageColumn, nameTable, nameIdColumn);
    sql += clause.sql;
    sql += sortClause(search, entityTable, idColumn, clause.values);
    sql += QStringLiteral(" LIMIT ? OFFSET ?");
    clause.values.append(qMax(1, search.limit));
    clause.values.append(qMax(0, search.offset));

    QSqlQuery query(m_database);
    if (!bindAndExec(query, sql, clause.values, errorMessage))
        return {};
    QList<PersonSummary> people;
    while (query.next())
    {
        people.append(
            {query.value(0).toLongLong(), query.value(1).toString(), query.value(2).toString()});
    }
    return people;
}

std::optional<int> PersonRepository::count(const PersonSearch &search, QString *errorMessage) const
{
    const bool actress = search.kind == PersonKind::Actress;
    const QString entityTable = actress ? QStringLiteral("actress") : QStringLiteral("actor");
    const QString nameTable =
        actress ? QStringLiteral("actress_name") : QStringLiteral("actor_name");
    const QString idColumn = actress ? QStringLiteral("actress_id") : QStringLiteral("actor_id");
    const PersonSearchClause clause = buildSearchClause(search, entityTable, nameTable, idColumn);
    QSqlQuery query(m_database);
    if (!bindAndExec(query, QStringLiteral("SELECT COUNT(*) FROM %1").arg(entityTable) + clause.sql,
                     clause.values, errorMessage) ||
        !query.next())
    {
        return std::nullopt;
    }
    return query.value(0).toInt();
}

QSet<qint64> PersonRepository::matchingPersonIds(const PersonSearch &search) const
{
    const bool actress = search.kind == PersonKind::Actress;
    const QString entityTable = actress ? QStringLiteral("actress") : QStringLiteral("actor");
    const QString nameTable =
        actress ? QStringLiteral("actress_name") : QStringLiteral("actor_name");
    const QString idColumn = actress ? QStringLiteral("actress_id") : QStringLiteral("actor_id");
    const PersonSearchClause clause = buildSearchClause(search, entityTable, nameTable, idColumn);
    QSqlQuery query(m_database);
    if (!bindAndExec(query, QStringLiteral("SELECT %1 FROM %2").arg(idColumn, entityTable)
                                + clause.sql, clause.values, nullptr))
        return {};
    QSet<qint64> ids;
    while (query.next())
        ids.insert(query.value(0).toLongLong());
    return ids;
}

QStringList PersonRepository::nameSuggestions(PersonKind kind) const
{
    const QString table =
        kind == PersonKind::Actress ? QStringLiteral("actress_name") : QStringLiteral("actor_name");
    QSqlQuery query(m_database);
    if (!query.exec(QStringLiteral("SELECT name FROM (SELECT cn AS name FROM %1 UNION "
                                   "SELECT jp FROM %1 "
                                   "UNION SELECT en FROM %1 UNION SELECT kana FROM %1) "
                                   "WHERE name IS NOT NULL AND name!='' ORDER BY name")
                        .arg(table)))
    {
        return {};
    }
    QStringList names;
    while (query.next())
        names.append(query.value(0).toString());
    return names;
}

QStringList PersonRepository::cupOptions() const
{
    QSqlQuery query(m_database);
    if (!query.exec(
            QStringLiteral("SELECT DISTINCT cup FROM actress WHERE cup IS NOT NULL AND cup!='' "
                           "ORDER BY cup")))
    {
        return {};
    }
    QStringList cups;
    while (query.next())
        cups.append(query.value(0).toString());
    return cups;
}

std::optional<PersonDetails> PersonRepository::findDetails(PersonKind kind, qint64 personId,
                                                           QString *errorMessage) const
{
    const bool actress = kind == PersonKind::Actress;
    const QString entityTable = actress ? QStringLiteral("actress") : QStringLiteral("actor");
    const QString idColumn = actress ? QStringLiteral("actress_id") : QStringLiteral("actor_id");
    QSqlQuery entity(m_database);
    if (actress)
    {
        entity.prepare(
            QStringLiteral("SELECT image_urlA,birthday,height,bust,waist,hip,cup,debut_date,"
                           "need_update,minnano_url,notes FROM actress WHERE actress_id=?"));
    }
    else
    {
        entity.prepare(
            QStringLiteral("SELECT image_url,birthday,height,handsome,fat,need_update,notes "
                           "FROM actor WHERE actor_id=?"));
    }
    entity.addBindValue(personId);
    if (!entity.exec())
    {
        if (errorMessage != nullptr)
            *errorMessage = entity.lastError().text();
        return std::nullopt;
    }
    if (!entity.next())
        return std::nullopt;

    PersonDetails details;
    details.kind = kind;
    details.id = personId;
    details.imagePath = entity.value(0).toString();
    details.birthday = entity.value(1).toString();
    if (!entity.value(2).isNull())
        details.height = entity.value(2).toInt();
    if (actress)
    {
        if (!entity.value(3).isNull())
            details.bust = entity.value(3).toInt();
        if (!entity.value(4).isNull())
            details.waist = entity.value(4).toInt();
        if (!entity.value(5).isNull())
            details.hip = entity.value(5).toInt();
        details.cup = entity.value(6).toString();
        details.debutDate = entity.value(7).toString();
        details.needUpdate = entity.value(8).toBool();
        details.minnanoUrl = entity.value(9).toString();
        details.notes = entity.value(10).toString();
    }
    else
    {
        if (!entity.value(3).isNull())
            details.handsome = entity.value(3).toInt();
        if (!entity.value(4).isNull())
            details.fat = entity.value(4).toInt();
        details.needUpdate = entity.value(5).toBool();
        details.notes = entity.value(6).toString();
    }

    if (actress)
    {
        QSqlQuery reference(m_database);
        if (!reference.exec(QStringLiteral(
                "SELECT height,bust,waist,hip,cup FROM actress "
                "WHERE height IS NOT NULL AND height!=0 "
                "AND waist IS NOT NULL AND waist!=0 "
                "AND hip IS NOT NULL AND hip!=0 "
                "AND bust IS NOT NULL AND bust!=0 AND cup IS NOT NULL")))
        {
            if (errorMessage != nullptr)
                *errorMessage = reference.lastError().text();
            return std::nullopt;
        }
        while (reference.next())
        {
            PersonBodyMetrics metrics;
            if (!reference.value(0).isNull()) metrics.height = reference.value(0).toInt();
            if (!reference.value(1).isNull()) metrics.bust = reference.value(1).toInt();
            if (!reference.value(2).isNull()) metrics.waist = reference.value(2).toInt();
            if (!reference.value(3).isNull()) metrics.hip = reference.value(3).toInt();
            metrics.cup = reference.value(4).toString();
            details.bodyReference.append(std::move(metrics));
        }
    }

    const QString nameTable =
        actress ? QStringLiteral("actress_name") : QStringLiteral("actor_name");
    const QString nameIdColumn =
        actress ? QStringLiteral("actress_name_id") : QStringLiteral("actor_name_id");
    QSqlQuery names(m_database);
    if (actress)
    {
        names.prepare(
            QStringLiteral("WITH RECURSIVE name_chain AS ("
                           "SELECT actress_name_id,cn,jp,en,kana,redirect_actress_name_id,1 level "
                           "FROM actress_name WHERE actress_id=? "
                           "AND redirect_actress_name_id IS NULL UNION ALL "
                           "SELECT child.actress_name_id,child.cn,child.jp,child.en,child.kana,"
                           "child.redirect_actress_name_id,parent.level+1 FROM actress_name child "
                           "JOIN name_chain parent ON child.redirect_actress_name_id="
                           "parent.actress_name_id WHERE child.actress_id=?) "
                           "SELECT actress_name_id,cn,jp,en,kana FROM name_chain ORDER BY level"));
        names.addBindValue(personId);
        names.addBindValue(personId);
    }
    else
    {
        names.prepare(QStringLiteral("SELECT %1,cn,jp,en,kana FROM %2 WHERE %3=? "
                                     "ORDER BY name_type DESC,actor_name_id")
                          .arg(nameIdColumn, nameTable, idColumn));
        names.addBindValue(personId);
    }
    if (!names.exec())
    {
        if (errorMessage != nullptr)
            *errorMessage = names.lastError().text();
        return std::nullopt;
    }
    while (names.next())
    {
        details.names.append({names.value(0).toLongLong(), names.value(1).toString(),
                              names.value(2).toString(), names.value(3).toString(),
                              names.value(4).toString()});
    }

    const QString relationTable =
        actress ? QStringLiteral("work_actress_relation") : QStringLiteral("work_actor_relation");
    QSqlQuery works(m_database);
    works.prepare(QStringLiteral("SELECT work.work_id,work.serial_number,work.cn_title,"
                                 "work.release_date,work.image_url,wtr.tag_id,"
                                 "CASE WHEN (SELECT cn_name FROM maker WHERE maker_id=p.maker_id) "
                                 " IS NULL THEN 0 ELSE 1 END "
                                 "FROM work JOIN %1 relation "
                                 "ON relation.work_id=work.work_id "
                                 "LEFT JOIN work_tag_relation wtr ON wtr.work_id=work.work_id "
                                 "AND wtr.tag_id IN (1,2,3) "
                                 "LEFT JOIN prefix_maker_relation p ON p.prefix="
                                 "SUBSTR(work.serial_number,1,INSTR(work.serial_number,'-')-1) "
                                 "WHERE relation.%2=? "
                                 "ORDER BY work.release_date DESC")
                      .arg(relationTable, idColumn));
    works.addBindValue(personId);
    if (!works.exec())
    {
        if (errorMessage != nullptr)
            *errorMessage = works.lastError().text();
        return std::nullopt;
    }
    while (works.next())
    {
        details.works.append({works.value(0).toLongLong(), works.value(1).toString(),
                              works.value(2).toString(), works.value(3).toString(),
                              works.value(4).toString(), works.value(5).toInt(),
                              works.value(6).toBool()});
    }
    return details;
}

bool PersonRepository::updateDetails(const PersonDetails &details, QString *errorMessage)
{
    if (details.id <= 0 || details.names.isEmpty())
    {
        if (errorMessage != nullptr)
            *errorMessage = QStringLiteral("人物必须包含有效 ID 和至少一个姓名");
        return false;
    }
    const auto nameIsEmpty = [](const PersonName &name)
    {
        return name.chinese.trimmed().isEmpty() && name.japanese.trimmed().isEmpty() &&
               name.english.trimmed().isEmpty() && name.kana.trimmed().isEmpty();
    };
    for (const PersonName &name : details.names)
    {
        if (nameIsEmpty(name))
        {
            if (errorMessage != nullptr)
                *errorMessage = QStringLiteral("姓名行不能全部为空");
            return false;
        }
    }

    Transaction transaction(m_database);
    if (!transaction.isActive())
    {
        if (errorMessage != nullptr)
            *errorMessage = transaction.errorString();
        return false;
    }
    const bool actress = details.kind == PersonKind::Actress;
    QSqlQuery entity(m_database);
    if (actress)
    {
        entity.prepare(QStringLiteral(
            "UPDATE actress SET image_urlA=?,birthday=?,height=?,bust=?,waist=?,hip=?,"
            "cup=?,debut_date=?,need_update=?,minnano_url=?,notes=?,"
            "update_time=datetime('now','localtime') WHERE actress_id=?"));
        entity.addBindValue(details.imagePath.trimmed());
        entity.addBindValue(details.birthday.trimmed());
        entity.addBindValue(details.height.has_value() ? QVariant(*details.height) : QVariant());
        entity.addBindValue(details.bust.has_value() ? QVariant(*details.bust) : QVariant());
        entity.addBindValue(details.waist.has_value() ? QVariant(*details.waist) : QVariant());
        entity.addBindValue(details.hip.has_value() ? QVariant(*details.hip) : QVariant());
        entity.addBindValue(details.cup.trimmed());
        entity.addBindValue(details.debutDate.trimmed());
        entity.addBindValue(details.needUpdate ? 1 : 0);
        entity.addBindValue(details.minnanoUrl.trimmed());
        entity.addBindValue(details.notes.trimmed());
        entity.addBindValue(details.id);
    }
    else
    {
        entity.prepare(
            QStringLiteral("UPDATE actor SET image_url=?,birthday=?,height=?,handsome=?,fat=?,"
                           "need_update=?,notes=? WHERE actor_id=?"));
        entity.addBindValue(details.imagePath.trimmed());
        entity.addBindValue(details.birthday.trimmed());
        entity.addBindValue(details.height.has_value() ? QVariant(*details.height) : QVariant());
        entity.addBindValue(details.handsome.has_value() ? QVariant(*details.handsome)
                                                         : QVariant());
        entity.addBindValue(details.fat.has_value() ? QVariant(*details.fat) : QVariant());
        entity.addBindValue(details.needUpdate ? 1 : 0);
        entity.addBindValue(details.notes.trimmed());
        entity.addBindValue(details.id);
    }
    if (!entity.exec() || entity.numRowsAffected() != 1)
    {
        if (errorMessage != nullptr)
            *errorMessage = entity.lastError().text().isEmpty() ? QStringLiteral("人物不存在")
                                                                : entity.lastError().text();
        return false;
    }

    const QString nameTable =
        actress ? QStringLiteral("actress_name") : QStringLiteral("actor_name");
    const QString idColumn = actress ? QStringLiteral("actress_id") : QStringLiteral("actor_id");
    const QString nameIdColumn =
        actress ? QStringLiteral("actress_name_id") : QStringLiteral("actor_name_id");

    // Match update_actress_name() in the Python application: retain existing
    // name rows and their identities, remove only rows the editor removed, and
    // then rebuild the redirect chain.  Deleting every row first breaks any
    // external reference to actress_name and can fail under foreign-key checks.
    QSet<qint64> existingNameIds;
    QSqlQuery existingNames(m_database);
    existingNames.prepare(QStringLiteral("SELECT %1 FROM %2 WHERE %3=?")
                              .arg(nameIdColumn, nameTable, idColumn));
    existingNames.addBindValue(details.id);
    if (!existingNames.exec())
    {
        if (errorMessage != nullptr)
            *errorMessage = existingNames.lastError().text();
        return false;
    }
    while (existingNames.next())
        existingNameIds.insert(existingNames.value(0).toLongLong());

    QSet<qint64> submittedNameIds;
    for (const PersonName &name : details.names)
    {
        if (name.id > 0)
            submittedNameIds.insert(name.id);
    }
    for (const qint64 nameId : submittedNameIds)
    {
        if (!existingNameIds.contains(nameId))
        {
            if (errorMessage != nullptr)
                *errorMessage = QStringLiteral("姓名不属于当前人物");
            return false;
        }
    }
    if (actress)
    {
        QSqlQuery unlink(m_database);
        unlink.prepare(QStringLiteral(
            "UPDATE actress_name SET redirect_actress_name_id=NULL WHERE actress_id=?"));
        unlink.addBindValue(details.id);
        if (!unlink.exec())
        {
            if (errorMessage != nullptr)
                *errorMessage = unlink.lastError().text();
            return false;
        }
    }
    const QSet<qint64> deletedNameIds = existingNameIds - submittedNameIds;
    for (const qint64 nameId : deletedNameIds)
    {
        QSqlQuery removeName(m_database);
        removeName.prepare(QStringLiteral("DELETE FROM %1 WHERE %2=? AND %3=?")
                               .arg(nameTable, nameIdColumn, idColumn));
        removeName.addBindValue(nameId);
        removeName.addBindValue(details.id);
        if (!removeName.exec() || removeName.numRowsAffected() != 1)
        {
            if (errorMessage != nullptr)
                *errorMessage = removeName.lastError().text().isEmpty()
                                    ? QStringLiteral("删除姓名失败")
                                    : removeName.lastError().text();
            return false;
        }
    }

    qint64 previousNameId = 0;
    for (qsizetype index = 0; index < details.names.size(); ++index)
    {
        const PersonName &name = details.names.at(index);
        if (name.id > 0)
        {
            QSqlQuery updateName(m_database);
            if (actress)
            {
                updateName.prepare(QStringLiteral(
                    "UPDATE actress_name SET name_type=?,cn=?,jp=?,en=?,kana=?,"
                    "redirect_actress_name_id=? WHERE actress_name_id=? AND actress_id=?"));
            }
            else
            {
                updateName.prepare(QStringLiteral(
                    "UPDATE actor_name SET name_type=?,cn=?,jp=?,en=?,kana=? "
                    "WHERE actor_name_id=? AND actor_id=?"));
            }
            updateName.addBindValue(index == 0 ? 1 : 0);
            updateName.addBindValue(name.chinese.trimmed());
            updateName.addBindValue(name.japanese.trimmed());
            updateName.addBindValue(name.english.trimmed());
            updateName.addBindValue(name.kana.trimmed());
            if (actress)
                updateName.addBindValue(previousNameId > 0 ? QVariant(previousNameId) : QVariant());
            updateName.addBindValue(name.id);
            updateName.addBindValue(details.id);
            if (!updateName.exec() || updateName.numRowsAffected() != 1)
            {
                if (errorMessage != nullptr)
                    *errorMessage = updateName.lastError().text().isEmpty()
                                        ? QStringLiteral("更新姓名失败")
                                        : updateName.lastError().text();
                return false;
            }
            previousNameId = name.id;
        }
        else
        {
            QSqlQuery insertName(m_database);
            if (actress)
            {
                insertName.prepare(QStringLiteral(
                    "INSERT INTO actress_name(actress_id,name_type,cn,jp,en,kana,"
                    "redirect_actress_name_id) VALUES(?,?,?,?,?,?,?)"));
            }
            else
            {
                insertName.prepare(QStringLiteral(
                    "INSERT INTO actor_name(actor_id,name_type,cn,jp,en,kana) "
                    "VALUES(?,?,?,?,?,?)"));
            }
            insertName.addBindValue(details.id);
            insertName.addBindValue(index == 0 ? 1 : 0);
            insertName.addBindValue(name.chinese.trimmed());
            insertName.addBindValue(name.japanese.trimmed());
            insertName.addBindValue(name.english.trimmed());
            insertName.addBindValue(name.kana.trimmed());
            if (actress)
                insertName.addBindValue(previousNameId > 0 ? QVariant(previousNameId) : QVariant());
            if (!insertName.exec())
            {
                if (errorMessage != nullptr)
                    *errorMessage = insertName.lastError().text();
                return false;
            }
            previousNameId = insertName.lastInsertId().toLongLong();
        }
    }
    if (!transaction.commit())
    {
        if (errorMessage != nullptr)
            *errorMessage = transaction.errorString();
        return false;
    }
    return true;
}

bool PersonRepository::deletePerson(PersonKind kind, qint64 personId, QString *errorMessage)
{
    if (personId <= 0)
    {
        if (errorMessage != nullptr) *errorMessage = QStringLiteral("人物不存在");
        return false;
    }
    const bool actress = kind == PersonKind::Actress;
    const QString relationTable = actress ? QStringLiteral("work_actress_relation")
                                          : QStringLiteral("work_actor_relation");
    const QString idColumn = actress ? QStringLiteral("actress_id") : QStringLiteral("actor_id");
    QSqlQuery references(m_database);
    references.prepare(QStringLiteral("SELECT 1 FROM %1 WHERE %2=? LIMIT 1").arg(relationTable, idColumn));
    references.addBindValue(personId);
    if (!references.exec())
    {
        if (errorMessage != nullptr) *errorMessage = references.lastError().text();
        return false;
    }
    if (references.next())
    {
        if (errorMessage != nullptr) *errorMessage = QStringLiteral("存在包含该人物的作品，无法删除");
        return false;
    }
    Transaction transaction(m_database);
    if (!transaction.isActive())
    {
        if (errorMessage != nullptr) *errorMessage = transaction.errorString();
        return false;
    }
    QSqlQuery names(m_database);
    names.prepare(QStringLiteral("DELETE FROM %1_name WHERE %2=?")
                      .arg(actress ? QStringLiteral("actress") : QStringLiteral("actor"), idColumn));
    names.addBindValue(personId);
    QSqlQuery entity(m_database);
    entity.prepare(QStringLiteral("DELETE FROM %1 WHERE %2=?")
                       .arg(actress ? QStringLiteral("actress") : QStringLiteral("actor"), idColumn));
    entity.addBindValue(personId);
    if (!names.exec() || !entity.exec() || entity.numRowsAffected() != 1 || !transaction.commit())
    {
        if (errorMessage != nullptr)
            *errorMessage = !names.lastError().text().isEmpty() ? names.lastError().text()
                : (!entity.lastError().text().isEmpty() ? entity.lastError().text()
                                                        : transaction.errorString());
        return false;
    }
    return true;
}

} // namespace darkeye
