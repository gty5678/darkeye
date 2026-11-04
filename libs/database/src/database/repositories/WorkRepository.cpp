#include "database/repositories/WorkRepository.h"

#include "database/Transaction.h"

#include <QDir>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <QSqlError>
#include <QSqlQuery>
#include <QSqlRecord>
#include <QVariantList>

namespace
{

std::optional<qint64> optionalId(const QVariant &value)
{
    return value.isNull() ? std::nullopt : std::optional<qint64>(value.toLongLong());
}

QString likePattern(QString value)
{
    return QStringLiteral("%") + value.trimmed() + QStringLiteral("%");
}

struct WorkSearchClause final
{
    QString sql = QStringLiteral(" WHERE work.is_deleted=0");
    QVariantList values;
};

WorkSearchClause buildSearchClause(const darkeye::WorkSearch &search)
{
    WorkSearchClause clause;
    const auto appendLike = [&clause](const QString &sql, const QString &value, int repeats = 1)
    {
        if (value.trimmed().isEmpty())
        {
            return;
        }
        clause.sql += sql;
        const QString pattern = likePattern(value);
        for (int index = 0; index < repeats; ++index)
        {
            clause.values.append(pattern);
        }
    };

    appendLike(QStringLiteral(" AND (work.serial_number LIKE ? "
                              "OR work.cn_title LIKE ? "
                              "OR work.jp_title LIKE ? "
                              "OR work.director LIKE ? "
                              "OR work.notes LIKE ?)"),
               search.keyword, 5);
    appendLike(QStringLiteral(" AND work.serial_number LIKE ?"), search.serialNumber);
    appendLike(QStringLiteral(" AND work.cn_title LIKE ?"), search.title);
    appendLike(QStringLiteral(" AND work.cn_story LIKE ?"), search.chineseStory);
    appendLike(QStringLiteral(" AND work.notes LIKE ?"), search.notes);
    appendLike(QStringLiteral(" AND work.director LIKE ?"), search.director);
    appendLike(QStringLiteral(" AND EXISTS (SELECT 1 FROM work_actress_relation war "
                              "JOIN actress_name an ON an.actress_id=war.actress_id "
                              "WHERE war.work_id=work.work_id AND (an.cn LIKE ? "
                              "OR an.jp LIKE ?))"),
               search.actressName, 2);
    appendLike(QStringLiteral(" AND EXISTS (SELECT 1 FROM work_actor_relation wor "
                              "JOIN actor_name aon ON aon.actor_id=wor.actor_id "
                              "WHERE wor.work_id=work.work_id AND (aon.cn LIKE ? "
                              "OR aon.jp LIKE ?))"),
               search.actorName, 2);
    appendLike(QStringLiteral(" AND EXISTS (SELECT 1 FROM work_tag_relation wtr "
                              "JOIN tag t ON t.tag_id=wtr.tag_id "
                              "WHERE wtr.work_id=work.work_id AND t.tag_name LIKE ? ESCAPE '\\')"),
               search.tagName);
    appendLike(QStringLiteral(" AND EXISTS (SELECT 1 FROM maker m WHERE m.maker_id=work.maker_id "
                              "AND (m.cn_name LIKE ? ESCAPE '\\' OR m.jp_name LIKE ? ESCAPE '\\' "
                              "OR m.aliases LIKE ? ESCAPE '\\'))"),
               search.makerName, 3);
    appendLike(QStringLiteral(" AND EXISTS (SELECT 1 FROM label l WHERE l.label_id=work.label_id "
                              "AND (l.cn_name LIKE ? ESCAPE '\\' OR l.jp_name LIKE ? ESCAPE '\\' "
                              "OR l.aliases LIKE ? ESCAPE '\\'))"),
               search.labelName, 3);
    appendLike(
        QStringLiteral(" AND EXISTS (SELECT 1 FROM series s WHERE s.series_id=work.series_id "
                       "AND (s.cn_name LIKE ? ESCAPE '\\' OR s.jp_name LIKE ? ESCAPE '\\' "
                       "OR s.aliases LIKE ? ESCAPE '\\'))"),
        search.seriesName, 3);
    const auto appendId = [&clause](const QString &column, const std::optional<qint64> &id)
    {
        if (!id.has_value()) return;
        clause.sql += QStringLiteral(" AND work.%1=?").arg(column);
        clause.values.append(*id);
    };
    appendId(QStringLiteral("maker_id"), search.makerId);
    appendId(QStringLiteral("label_id"), search.labelId);
    appendId(QStringLiteral("series_id"), search.seriesId);
    for (const qint64 tagId : search.tagIds)
    {
        clause.sql +=
            QStringLiteral(" AND EXISTS (SELECT 1 FROM work_tag_relation selected_tag "
                           "WHERE selected_tag.work_id=work.work_id AND selected_tag.tag_id=?)");
        clause.values.append(tagId);
    }
    if (search.restrictToIncludedWorkIds)
    {
        if (search.includedWorkIds.isEmpty())
        {
            clause.sql += QStringLiteral(" AND 0");
        }
        else
        {
            QStringList placeholders;
            placeholders.fill(QStringLiteral("?"), search.includedWorkIds.size());
            clause.sql += QStringLiteral(" AND work.work_id IN (%1)")
                              .arg(placeholders.join(QLatin1Char(',')));
            for (const qint64 workId : search.includedWorkIds)
            {
                clause.values.append(workId);
            }
        }
    }
    if (search.requireLocalVideo)
    {
        clause.sql += QStringLiteral(" AND COALESCE(TRIM(work.video_url), '')<>''");
    }
    if (search.order == darkeye::WorkSortOrder::ReleaseDateAscending
        || search.order == darkeye::WorkSortOrder::ReleaseDateDescending)
    {
        clause.sql += QStringLiteral(
            " AND work.release_date IS NOT NULL AND work.release_date!=''");
    }
    if (search.order == darkeye::WorkSortOrder::ActressAgeAscending
        || search.order == darkeye::WorkSortOrder::ActressAgeDescending)
    {
        clause.sql += QStringLiteral(
            " AND EXISTS (SELECT 1 FROM v_work_avg_age_info age "
            "WHERE age.work_id=work.work_id AND age.avg_age IS NOT NULL)");
    }
    return clause;
}

QList<darkeye::NamedIdOption> queryNamedOptions(const QSqlDatabase &database,
                                                const QString &table,
                                                const QString &idColumn)
{
    QSqlQuery query(database);
    query.prepare(QStringLiteral(
        "SELECT %1,COALESCE(NULLIF(cn_name,''),jp_name,''),COALESCE(cn_name,''),"
        "COALESCE(jp_name,''),COALESCE(aliases,'') FROM %2 ORDER BY 2 COLLATE NOCASE")
                      .arg(idColumn, table));
    QList<darkeye::NamedIdOption> options;
    if (!query.exec()) return options;
    while (query.next())
    {
        darkeye::NamedIdOption option;
        option.id = query.value(0).toLongLong();
        option.name = query.value(1).toString();
        option.chineseName = query.value(2).toString();
        option.japaneseName = query.value(3).toString();
        option.aliases = query.value(4).toString().split(',', Qt::SkipEmptyParts);
        for (QString &alias : option.aliases) alias = alias.trimmed();
        options.append(std::move(option));
    }
    return options;
}

QStringList queryStringList(const QSqlDatabase &database, const QString &sql)
{
    QSqlQuery query(database);
    QStringList values;
    if (!query.exec(sql))
    {
        return values;
    }
    while (query.next())
    {
        const QString value = query.value(0).toString().trimmed();
        if (!value.isEmpty())
            values.append(value);
    }
    return values;
}

} // namespace

namespace darkeye
{

WorkRepository::WorkRepository(QSqlDatabase database) : m_database(std::move(database)) {}

std::optional<Work> WorkRepository::findById(qint64 workId, QString *errorMessage) const
{
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "SELECT work_id, serial_number, director, runtime, notes, release_date, image_url, "
        "video_url, cn_title, jp_title, cn_story, jp_story, maker_id, label_id, series_id, "
        "fanart, is_deleted FROM work WHERE work_id=?"));
    query.addBindValue(workId);
    if (!query.exec())
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = query.lastError().text();
        }
        return std::nullopt;
    }
    if (!query.next())
    {
        return std::nullopt;
    }

    Work work;
    work.id = query.value(0).toLongLong();
    work.serialNumber = query.value(1).toString();
    work.director = query.value(2).toString();
    if (!query.value(3).isNull())
    {
        work.runtime = query.value(3).toInt();
    }
    work.notes = query.value(4).toString();
    work.releaseDate = query.value(5).toString();
    work.imageUrl = query.value(6).toString();
    work.videoUrl = query.value(7).toString();
    work.chineseTitle = query.value(8).toString();
    work.japaneseTitle = query.value(9).toString();
    work.chineseStory = query.value(10).toString();
    work.japaneseStory = query.value(11).toString();
    work.makerId = optionalId(query.value(12));
    work.labelId = optionalId(query.value(13));
    work.seriesId = optionalId(query.value(14));
    work.fanartJson = query.value(15).toString();
    work.deleted = query.value(16).toBool();
    return work;
}

std::optional<WorkDetails> WorkRepository::findDetailsById(qint64 workId,
                                                           QString *errorMessage) const
{
    const std::optional<Work> work = findById(workId, errorMessage);
    if (!work.has_value())
        return std::nullopt;

    WorkDetails details;
    details.work = *work;
    QSqlQuery references(m_database);
    references.prepare(
        QStringLiteral("SELECT COALESCE(m.cn_name, m.jp_name, ''), "
                       "COALESCE(l.cn_name, l.jp_name, ''), COALESCE(s.cn_name, s.jp_name, '') "
                       "FROM work w LEFT JOIN maker m ON m.maker_id=w.maker_id "
                       "LEFT JOIN label l ON l.label_id=w.label_id "
                       "LEFT JOIN series s ON s.series_id=w.series_id WHERE w.work_id=?"));
    references.addBindValue(workId);
    if (!references.exec() || !references.next())
    {
        if (errorMessage != nullptr)
            *errorMessage = references.lastError().text();
        return std::nullopt;
    }
    details.makerName = references.value(0).toString();
    details.labelName = references.value(1).toString();
    details.seriesName = references.value(2).toString();

    const auto loadPeople = [this, workId, errorMessage](bool actress)
    {
        QList<WorkPersonReference> people;
        QSqlQuery query(m_database);
        const QString relation = actress ? QStringLiteral("work_actress_relation")
                                         : QStringLiteral("work_actor_relation");
        const QString nameTable =
            actress ? QStringLiteral("actress_name") : QStringLiteral("actor_name");
        const QString idColumn =
            actress ? QStringLiteral("actress_id") : QStringLiteral("actor_id");
        query.prepare(
            QStringLiteral(
                "SELECT relation.%1, COALESCE(NULLIF(names.cn,''), NULLIF(names.jp,''), "
                "NULLIF(names.en,''), names.kana, '') FROM %2 relation "
                "LEFT JOIN %3 names ON names.%1=relation.%1 AND names.name_type=1 "
                "WHERE relation.work_id=? GROUP BY relation.%1 ORDER BY names.cn, names.jp")
                .arg(idColumn, relation, nameTable));
        query.addBindValue(workId);
        if (!query.exec())
        {
            if (errorMessage != nullptr)
                *errorMessage = query.lastError().text();
            return people;
        }
        while (query.next())
        {
            people.append({query.value(0).toLongLong(), query.value(1).toString()});
        }
        return people;
    };
    details.actresses = loadPeople(true);
    details.actors = loadPeople(false);

    QSqlQuery tags(m_database);
    tags.prepare(QStringLiteral(
        "SELECT t.tag_id, t.tag_name, COALESCE(tt.tag_type_name, '未分类'), "
        "COALESCE(t.color, ''), COALESCE(t.detail, '') "
        "FROM work_tag_relation relation JOIN tag t ON t.tag_id=relation.tag_id "
        "LEFT JOIN tag_type tt ON tt.tag_type_id=t.tag_type_id "
        "WHERE relation.work_id=? ORDER BY COALESCE(tt.tag_order, 2147483647), t.tag_name"));
    tags.addBindValue(workId);
    if (!tags.exec())
    {
        if (errorMessage != nullptr)
            *errorMessage = tags.lastError().text();
        return std::nullopt;
    }
    while (tags.next())
    {
        details.tags.append({tags.value(0).toLongLong(), tags.value(1).toString(),
                             tags.value(2).toString(), tags.value(3).toString(),
                             tags.value(4).toString()});
    }
    return details;
}

QList<qint64> WorkRepository::allIds(bool includeDeleted, QString *errorMessage) const
{
    QSqlQuery query(m_database);
    const QString sql =
        includeDeleted
            ? QStringLiteral("SELECT work_id FROM work ORDER BY work_id")
            : QStringLiteral("SELECT work_id FROM work WHERE is_deleted=0 ORDER BY work_id");
    if (!query.exec(sql))
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = query.lastError().text();
        }
        return {};
    }
    QList<qint64> ids;
    while (query.next())
    {
        ids.append(query.value(0).toLongLong());
    }
    return ids;
}

QList<WorkSummary> WorkRepository::search(const WorkSearch &search, QString *errorMessage) const
{
    QString orderBy;
    switch (search.order)
    {
    case WorkSortOrder::Random:
        orderBy = QStringLiteral(
            "(((work.work_id * ?) % 1000003) + "
            "((work.work_id * work.work_id * ?) % 1000033)) % 1000037, work.work_id");
        break;
    case WorkSortOrder::UpdatedDescending:
        orderBy = QStringLiteral("work.update_time DESC");
        break;
    case WorkSortOrder::CreatedDescending:
        orderBy = QStringLiteral("work.create_time DESC");
        break;
    case WorkSortOrder::CreatedAscending:
        orderBy = QStringLiteral("work.create_time");
        break;
    case WorkSortOrder::UpdatedAscending:
        orderBy = QStringLiteral("work.update_time");
        break;
    case WorkSortOrder::ReleaseDateDescending:
        orderBy = QStringLiteral("work.release_date DESC");
        break;
    case WorkSortOrder::ReleaseDateAscending:
        orderBy = QStringLiteral("work.release_date");
        break;
    case WorkSortOrder::SerialAscending:
        orderBy = QStringLiteral("work.serial_number");
        break;
    case WorkSortOrder::SerialDescending:
        orderBy = QStringLiteral("work.serial_number DESC");
        break;
    case WorkSortOrder::MakerAscending:
        orderBy = QStringLiteral(
            "work.maker_id IS NULL, work.maker_id, work.serial_number");
        break;
    case WorkSortOrder::MakerDescending:
        orderBy = QStringLiteral(
            "work.maker_id IS NULL DESC, work.maker_id DESC, work.serial_number DESC");
        break;
    case WorkSortOrder::ActressAgeAscending:
    case WorkSortOrder::ActressAgeDescending:
    {
        const QString direction = search.order == WorkSortOrder::ActressAgeAscending
                                      ? QStringLiteral("ASC")
                                      : QStringLiteral("DESC");
        orderBy = QStringLiteral(
            "(SELECT avg_age FROM v_work_avg_age_info age "
            "WHERE age.work_id=work.work_id) %1")
                      .arg(direction);
        break;
    }
    }

    const WorkSearchClause clause = buildSearchClause(search);
    QString sql = QStringLiteral(
        "SELECT work.work_id,work.serial_number,work.cn_title,work.release_date,work.director,"
        "work.image_url,wtr.tag_id,"
        "CASE WHEN (SELECT cn_name FROM maker WHERE maker_id=work.maker_id) IS NULL "
        "THEN 0 ELSE 1 END,work.video_url FROM work "
        "LEFT JOIN work_tag_relation wtr ON work.work_id=wtr.work_id "
        "AND wtr.tag_id IN (1,2,3)");
    sql += clause.sql;
    if (!search.tagIds.isEmpty()) sql += QStringLiteral(" GROUP BY work.work_id");
    sql += QStringLiteral(" ORDER BY %1").arg(orderBy);
    if (search.limit > 0)
        sql += QStringLiteral(" LIMIT ? OFFSET ?");

    QSqlQuery query(m_database);
    query.prepare(sql);
    for (const QVariant &value : clause.values)
    {
        query.addBindValue(value);
    }
    if (search.order == WorkSortOrder::Random)
    {
        query.addBindValue(search.randomSeed);
        query.addBindValue(search.randomSeed2);
    }
    if (search.limit > 0)
    {
        query.addBindValue(qBound(1, search.limit, 1000));
        query.addBindValue(qMax(0, search.offset));
    }
    if (!query.exec())
    {
        qWarning() << "WorkRepository search failed:" << query.lastError().text()
                   << "SQL:" << query.lastQuery();
        if (errorMessage != nullptr)
        {
            *errorMessage = query.lastError().text();
        }
        return {};
    }

    QList<WorkSummary> works;
    while (query.next())
    {
        WorkSummary work;
        work.id = query.value(0).toLongLong();
        work.serialNumber = query.value(1).toString();
        work.chineseTitle = query.value(2).toString();
        work.releaseDate = query.value(3).toString();
        work.director = query.value(4).toString();
        work.imageUrl = query.value(5).toString();
        work.highlightTagId = query.value(6).toInt();
        work.standard = query.value(7).toBool();
        work.videoUrl = query.value(8).toString();
        works.append(work);
    }
    return works;
}

std::optional<int> WorkRepository::count(const WorkSearch &search, QString *errorMessage) const
{
    const WorkSearchClause clause = buildSearchClause(search);
    QSqlQuery query(m_database);
    QString rows = QStringLiteral(
        "SELECT work.work_id FROM work LEFT JOIN work_tag_relation wtr "
        "ON work.work_id=wtr.work_id AND wtr.tag_id IN (1,2,3)") + clause.sql;
    if (!search.tagIds.isEmpty()) rows += QStringLiteral(" GROUP BY work.work_id");
    query.prepare(QStringLiteral("SELECT COUNT(*) FROM (%1)").arg(rows));
    for (const QVariant &value : clause.values)
    {
        query.addBindValue(value);
    }
    if (!query.exec())
    {
        qWarning() << "WorkRepository count failed:" << query.lastError().text()
                   << "SQL:" << query.lastQuery();
        if (errorMessage != nullptr)
        {
            *errorMessage = query.lastError().text();
        }
        return std::nullopt;
    }
    if (!query.next())
    {
        qWarning() << "WorkRepository count returned no row:" << query.lastError().text()
                   << "SQL:" << query.lastQuery();
        if (errorMessage != nullptr)
        {
            *errorMessage = query.lastError().text();
        }
        return std::nullopt;
    }
    return query.value(0).toInt();
}

bool WorkRepository::existsSerial(const QString &serialNumber, QString *errorMessage) const
{
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral("SELECT 1 FROM work WHERE UPPER(serial_number)=UPPER(?) LIMIT 1"));
    query.addBindValue(serialNumber.trimmed());
    if (!query.exec())
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = query.lastError().text();
        }
        return false;
    }
    return query.next();
}

std::optional<qint64> WorkRepository::findIdBySerial(const QString &serialNumber,
                                                     QString *errorMessage) const
{
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral("SELECT work_id FROM work WHERE serial_number=?"));
    query.addBindValue(serialNumber);
    if (!query.exec())
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = query.lastError().text();
        }
        return std::nullopt;
    }
    if (!query.next())
    {
        return std::nullopt;
    }
    return query.value(0).toLongLong();
}

QStringList WorkRepository::serialSuggestions() const
{
    return queryStringList(m_database,
                           QStringLiteral("SELECT serial_number FROM work WHERE is_deleted=0 ORDER "
                                          "BY serial_number COLLATE NOCASE"));
}

QStringList WorkRepository::actressSuggestions() const
{
    return queryStringList(
        m_database,
        QStringLiteral(
            "SELECT DISTINCT COALESCE(NULLIF(cn,''), NULLIF(jp,''), NULLIF(en,''), kana) "
            "FROM actress_name ORDER BY 1 COLLATE NOCASE"));
}

QStringList WorkRepository::actorSuggestions() const
{
    return queryStringList(
        m_database,
        QStringLiteral(
            "SELECT DISTINCT COALESCE(NULLIF(cn,''), NULLIF(jp,''), NULLIF(en,''), kana) "
            "FROM actor_name ORDER BY 1 COLLATE NOCASE"));
}

QStringList WorkRepository::directorSuggestions() const
{
    return queryStringList(
        m_database,
        QStringLiteral(
            "SELECT DISTINCT director FROM work WHERE director IS NOT NULL AND director<>'' "
            "ORDER BY director COLLATE NOCASE"));
}

QStringList WorkRepository::makerSuggestions() const
{
    return queryStringList(
        m_database,
        QStringLiteral(
            "SELECT COALESCE(NULLIF(cn_name,''), jp_name) FROM maker ORDER BY 1 COLLATE NOCASE"));
}

QStringList WorkRepository::labelSuggestions() const
{
    return queryStringList(
        m_database,
        QStringLiteral(
            "SELECT COALESCE(NULLIF(cn_name,''), jp_name) FROM label ORDER BY 1 COLLATE NOCASE"));
}

QStringList WorkRepository::seriesSuggestions() const
{
    return queryStringList(
        m_database,
        QStringLiteral(
            "SELECT COALESCE(NULLIF(cn_name,''), jp_name) FROM series ORDER BY 1 COLLATE NOCASE"));
}

QList<NamedIdOption> WorkRepository::makerOptions() const
{
    return queryNamedOptions(m_database, QStringLiteral("maker"), QStringLiteral("maker_id"));
}

QList<NamedIdOption> WorkRepository::labelOptions() const
{
    return queryNamedOptions(m_database, QStringLiteral("label"), QStringLiteral("label_id"));
}

QList<NamedIdOption> WorkRepository::seriesOptions() const
{
    return queryNamedOptions(m_database, QStringLiteral("series"), QStringLiteral("series_id"));
}

QList<TagOption> WorkRepository::tagOptions() const
{
    QSqlQuery query(m_database);
    QList<TagOption> options;
    if (!query.exec(QStringLiteral(
            "SELECT tag.tag_id, tag.tag_name, COALESCE(tag_type.tag_type_name, '未分类'), "
            "COALESCE(tag.color, ''), COALESCE(tag.detail, ''), "
            "COALESCE(CAST(tag.group_id AS TEXT), '') FROM tag "
            "LEFT JOIN tag_type ON tag_type.tag_type_id=tag.tag_type_id "
            "WHERE tag.redirect_tag_id IS NULL "
            "ORDER BY COALESCE(tag_type.tag_order, 2147483647), tag.color")))
    {
        return options;
    }
    while (query.next())
    {
        options.append({query.value(0).toLongLong(), query.value(1).toString(),
                        query.value(2).toString(), query.value(3).toString(),
                        query.value(4).toString(), query.value(5).toString(), {}});
    }

    QHash<qint64, QStringList> aliasesByTagId;
    QSqlQuery aliasQuery(m_database);
    if (aliasQuery.exec(QStringLiteral(
            "SELECT redirect_tag_id, tag_name FROM tag "
            "WHERE redirect_tag_id IS NOT NULL ORDER BY tag_name COLLATE NOCASE")))
    {
        while (aliasQuery.next())
        {
            aliasesByTagId[aliasQuery.value(0).toLongLong()].append(
                aliasQuery.value(1).toString());
        }
    }
    for (TagOption &option : options)
    {
        option.aliases = aliasesByTagId.value(option.id);
    }
    return options;
}

std::optional<qint64> WorkRepository::insertSerial(const QString &serialNumber,
                                                   QString *errorMessage)
{
    const QString normalized = serialNumber.trimmed();
    if (normalized.isEmpty())
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = QStringLiteral("番号不能为空");
        }
        return std::nullopt;
    }

    QSqlQuery query(m_database);
    query.prepare(QStringLiteral("INSERT INTO work(serial_number) VALUES(?)"));
    query.addBindValue(normalized);
    if (!query.exec())
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = query.lastError().text();
        }
        return std::nullopt;
    }
    return query.lastInsertId().toLongLong();
}

std::optional<qint64> WorkRepository::insertComplete(const Work &work,
                                                     const QList<qint64> &actressIds,
                                                     const QList<qint64> &actorIds,
                                                     const QList<qint64> &tagIds,
                                                     QString *errorMessage)
{
    const QString serialNumber = work.serialNumber.trimmed();
    if (serialNumber.isEmpty())
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = QStringLiteral("番号不能为空");
        }
        return std::nullopt;
    }

    Transaction transaction(m_database);
    if (!transaction.isActive())
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = transaction.errorString();
        }
        return std::nullopt;
    }

    QSqlQuery workQuery(m_database);
    workQuery.prepare(QStringLiteral(
        "INSERT INTO work(serial_number, director, runtime, notes, release_date, image_url, "
        "video_url, cn_title, jp_title, cn_story, jp_story, maker_id, label_id, series_id, "
        "fanart, is_deleted) VALUES(?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)"));
    workQuery.addBindValue(serialNumber);
    workQuery.addBindValue(work.director);
    workQuery.addBindValue(work.runtime.has_value() ? QVariant(*work.runtime) : QVariant());
    workQuery.addBindValue(work.notes);
    workQuery.addBindValue(work.releaseDate);
    workQuery.addBindValue(work.imageUrl);
    workQuery.addBindValue(work.videoUrl);
    workQuery.addBindValue(work.chineseTitle);
    workQuery.addBindValue(work.japaneseTitle);
    workQuery.addBindValue(work.chineseStory);
    workQuery.addBindValue(work.japaneseStory);
    workQuery.addBindValue(work.makerId.has_value() ? QVariant(*work.makerId) : QVariant());
    workQuery.addBindValue(work.labelId.has_value() ? QVariant(*work.labelId) : QVariant());
    workQuery.addBindValue(work.seriesId.has_value() ? QVariant(*work.seriesId) : QVariant());
    workQuery.addBindValue(work.fanartJson);
    workQuery.addBindValue(work.deleted ? 1 : 0);
    if (!workQuery.exec())
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = workQuery.lastError().text();
        }
        return std::nullopt;
    }
    const qint64 workId = workQuery.lastInsertId().toLongLong();

    const auto insertRelations = [&](const QString &sql, const QList<qint64> &ids)
    {
        QSqlQuery relationQuery(m_database);
        relationQuery.prepare(sql);
        const QSet<qint64> uniqueIds(ids.cbegin(), ids.cend());
        for (const qint64 id : uniqueIds)
        {
            relationQuery.bindValue(0, workId);
            relationQuery.bindValue(1, id);
            if (!relationQuery.exec())
            {
                if (errorMessage != nullptr)
                {
                    *errorMessage = relationQuery.lastError().text();
                }
                return false;
            }
        }
        return true;
    };

    if (!insertRelations(
            QStringLiteral("INSERT INTO work_actress_relation(work_id, actress_id) VALUES(?, ?)"),
            actressIds) ||
        !insertRelations(
            QStringLiteral("INSERT INTO work_actor_relation(work_id, actor_id) VALUES(?, ?)"),
            actorIds) ||
        !insertRelations(
            QStringLiteral("INSERT INTO work_tag_relation(work_id, tag_id) VALUES(?, ?)"), tagIds))
    {
        return std::nullopt;
    }
    if (!transaction.commit())
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = transaction.errorString();
        }
        return std::nullopt;
    }
    return workId;
}

bool WorkRepository::setDeleted(qint64 workId, bool deleted, QString *errorMessage)
{
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral("UPDATE work SET is_deleted=? WHERE work_id=?"));
    query.addBindValue(deleted ? 1 : 0);
    query.addBindValue(workId);
    if (!query.exec())
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = query.lastError().text();
        }
        return false;
    }
    if (query.numRowsAffected() != 1)
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = QStringLiteral("作品不存在：%1").arg(workId);
        }
        return false;
    }
    return true;
}

QList<WorkStateRecord> WorkRepository::listByDeletedState(bool deleted, const QString &keyword,
                                                          QString *errorMessage) const
{
    QSqlQuery query(m_database);
    // Keep the same active/deleted predicate and natural table order as Python's
    // WorkSoftDeletePage (SELECT * FROM work WHERE IFNULL(is_deleted, 0) = 0).
    QString sql = QStringLiteral("SELECT * FROM work WHERE IFNULL(is_deleted, 0)=?");
    if (!keyword.trimmed().isEmpty())
        sql += QStringLiteral(" AND (serial_number LIKE ? ESCAPE '\\' OR "
                              "cn_title LIKE ? ESCAPE '\\' OR jp_title LIKE ? ESCAPE '\\')");
    query.prepare(sql);
    query.addBindValue(deleted ? 1 : 0);
    if (!keyword.trimmed().isEmpty())
    {
        QString literal = keyword.trimmed();
        literal.replace(QStringLiteral("\\"), QStringLiteral("\\\\"));
        literal.replace(QStringLiteral("%"), QStringLiteral("\\%"));
        literal.replace(QStringLiteral("_"), QStringLiteral("\\_"));
        const QString pattern = QStringLiteral("%") + literal + QStringLiteral("%");
        query.addBindValue(pattern);
        query.addBindValue(pattern);
        query.addBindValue(pattern);
    }
    if (!query.exec())
    {
        if (errorMessage != nullptr)
            *errorMessage = query.lastError().text();
        return {};
    }
    QList<WorkStateRecord> records;
    const QSqlRecord record = query.record();
    while (query.next())
    {
        WorkStateRecord stateRecord;
        stateRecord.id = query.value(record.indexOf(QStringLiteral("work_id"))).toLongLong();
        stateRecord.serialNumber = query.value(record.indexOf(QStringLiteral("serial_number"))).toString();
        stateRecord.chineseTitle = query.value(record.indexOf(QStringLiteral("cn_title"))).toString();
        stateRecord.japaneseTitle = query.value(record.indexOf(QStringLiteral("jp_title"))).toString();
        stateRecord.releaseDate = query.value(record.indexOf(QStringLiteral("release_date"))).toString();
        stateRecord.imageUrl = query.value(record.indexOf(QStringLiteral("image_url"))).toString();
        stateRecord.tableValues.reserve(record.count());
        for (int column = 0; column < record.count(); ++column)
            stateRecord.tableValues.append(query.value(column).toString());
        records.append(std::move(stateRecord));
    }
    return records;
}

bool WorkRepository::setDeletedMany(const QList<qint64> &workIds, bool deleted,
                                    QString *errorMessage)
{
    const QSet<qint64> uniqueIds(workIds.cbegin(), workIds.cend());
    if (uniqueIds.isEmpty())
        return true;
    Transaction transaction(m_database);
    if (!transaction.isActive())
    {
        if (errorMessage != nullptr)
            *errorMessage = transaction.errorString();
        return false;
    }
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral("UPDATE work SET is_deleted=? WHERE work_id=?"));
    for (const qint64 id : uniqueIds)
    {
        if (id <= 0)
        {
            if (errorMessage != nullptr)
                *errorMessage = QStringLiteral("作品 ID 无效：%1").arg(id);
            return false;
        }
        query.bindValue(0, deleted ? 1 : 0);
        query.bindValue(1, id);
        if (!query.exec() || query.numRowsAffected() != 1)
        {
            if (errorMessage != nullptr)
                *errorMessage = query.lastError().text().isEmpty()
                                    ? QStringLiteral("作品不存在：%1").arg(id)
                                    : query.lastError().text();
            return false;
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

bool WorkRepository::permanentlyRemoveDeletedMany(const QList<qint64> &workIds,
                                                  QStringList *removedImageUrls,
                                                  QString *errorMessage,
                                                  QStringList *removedFanartFiles)
{
    const QSet<qint64> uniqueIds(workIds.cbegin(), workIds.cend());
    if (uniqueIds.isEmpty())
        return true;
    Transaction transaction(m_database);
    if (!transaction.isActive())
    {
        if (errorMessage != nullptr)
            *errorMessage = transaction.errorString();
        return false;
    }
    QStringList imageUrls;
    QStringList fanartFiles;
    QSqlQuery find(m_database);
    find.prepare(
        QStringLiteral("SELECT image_url, fanart FROM work WHERE work_id=? AND is_deleted=1"));
    for (const qint64 id : uniqueIds)
    {
        if (id <= 0)
        {
            if (errorMessage != nullptr)
                *errorMessage = QStringLiteral("作品 ID 无效：%1").arg(id);
            return false;
        }
        find.bindValue(0, id);
        if (!find.exec() || !find.next())
        {
            if (errorMessage != nullptr)
                *errorMessage = QStringLiteral("作品不在回收站中：%1").arg(id);
            return false;
        }
        const QString imageUrl = find.value(0).toString().trimmed();
        if (!imageUrl.isEmpty())
            imageUrls.append(imageUrl);
        QJsonParseError parseError;
        const QJsonDocument fanart =
            QJsonDocument::fromJson(find.value(1).toString().toUtf8(), &parseError);
        if (parseError.error == QJsonParseError::NoError && fanart.isArray())
        {
            for (const QJsonValue &value : fanart.array())
            {
                if (!value.isObject())
                    continue;
                const QString file = QDir::cleanPath(
                    value.toObject().value(QStringLiteral("file")).toString().trimmed());
                if (!file.isEmpty() && file != QStringLiteral(".") &&
                    file != QStringLiteral("..") && !QFileInfo(file).isAbsolute() &&
                    !file.startsWith(QStringLiteral("../")) &&
                    !file.startsWith(QStringLiteral("..\\")))
                    fanartFiles.append(file);
            }
        }
    }
    for (const qint64 id : uniqueIds)
    {
        for (const QString &table :
             {QStringLiteral("work_actress_relation"), QStringLiteral("work_actor_relation"),
              QStringLiteral("work_tag_relation")})
        {
            QSqlQuery relation(m_database);
            relation.prepare(QStringLiteral("DELETE FROM %1 WHERE work_id=?").arg(table));
            relation.addBindValue(id);
            if (!relation.exec())
            {
                if (errorMessage != nullptr)
                    *errorMessage = relation.lastError().text();
                return false;
            }
        }
        QSqlQuery remove(m_database);
        remove.prepare(QStringLiteral("DELETE FROM work WHERE work_id=? AND is_deleted=1"));
        remove.addBindValue(id);
        if (!remove.exec() || remove.numRowsAffected() != 1)
        {
            if (errorMessage != nullptr)
                *errorMessage = remove.lastError().text().isEmpty()
                                    ? QStringLiteral("删除作品失败：%1").arg(id)
                                    : remove.lastError().text();
            return false;
        }
    }
    if (!transaction.commit())
    {
        if (errorMessage != nullptr)
            *errorMessage = transaction.errorString();
        return false;
    }
    if (removedImageUrls != nullptr)
        *removedImageUrls = imageUrls;
    if (removedFanartFiles != nullptr)
        *removedFanartFiles = fanartFiles;
    return true;
}

bool WorkRepository::updateDetails(const Work &work, QString *errorMessage)
{
    if (work.id <= 0)
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = QStringLiteral("作品 ID 无效");
        }
        return false;
    }

    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "UPDATE work SET director=?, release_date=?, notes=?, runtime=?, cn_title=?, "
        "cn_story=?, jp_title=?, jp_story=?, image_url=?, video_url=?, maker_id=?, label_id=?, "
        "series_id=?, fanart=? WHERE work_id=?"));
    query.addBindValue(work.director.trimmed());
    query.addBindValue(work.releaseDate.trimmed());
    query.addBindValue(work.notes);
    query.addBindValue(work.runtime.has_value() ? QVariant(*work.runtime) : QVariant());
    query.addBindValue(work.chineseTitle.trimmed());
    query.addBindValue(work.chineseStory);
    query.addBindValue(work.japaneseTitle.trimmed());
    query.addBindValue(work.japaneseStory);
    query.addBindValue(work.imageUrl.trimmed());
    query.addBindValue(work.videoUrl.trimmed());
    query.addBindValue(work.makerId.has_value() ? QVariant(*work.makerId) : QVariant());
    query.addBindValue(work.labelId.has_value() ? QVariant(*work.labelId) : QVariant());
    query.addBindValue(work.seriesId.has_value() ? QVariant(*work.seriesId) : QVariant());
    query.addBindValue(work.fanartJson);
    query.addBindValue(work.id);
    if (!query.exec())
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = query.lastError().text();
        }
        return false;
    }
    if (query.numRowsAffected() != 1)
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = QStringLiteral("作品不存在：%1").arg(work.id);
        }
        return false;
    }
    return true;
}

bool WorkRepository::updateComplete(const Work &work, const QList<qint64> &actressIds,
                                    const QList<qint64> &actorIds, const QList<qint64> &tagIds,
                                    QString *errorMessage)
{
    if (work.id <= 0)
    {
        if (errorMessage != nullptr)
            *errorMessage = QStringLiteral("作品 ID 无效");
        return false;
    }
    Transaction transaction(m_database);
    if (!transaction.isActive())
    {
        if (errorMessage != nullptr)
            *errorMessage = transaction.errorString();
        return false;
    }
    if (!updateDetails(work, errorMessage))
        return false;

    const auto replaceRelations =
        [&](const QString &table, const QString &idColumn, const QList<qint64> &ids)
    {
        QSqlQuery remove(m_database);
        remove.prepare(QStringLiteral("DELETE FROM %1 WHERE work_id=?").arg(table));
        remove.addBindValue(work.id);
        if (!remove.exec())
        {
            if (errorMessage != nullptr)
                *errorMessage = remove.lastError().text();
            return false;
        }
        QSqlQuery insert(m_database);
        insert.prepare(
            QStringLiteral("INSERT INTO %1(work_id, %2) VALUES(?, ?)").arg(table, idColumn));
        const QSet<qint64> uniqueIds(ids.cbegin(), ids.cend());
        for (const qint64 id : uniqueIds)
        {
            insert.bindValue(0, work.id);
            insert.bindValue(1, id);
            if (!insert.exec())
            {
                if (errorMessage != nullptr)
                    *errorMessage = insert.lastError().text();
                return false;
            }
        }
        return true;
    };
    if (!replaceRelations(QStringLiteral("work_actress_relation"), QStringLiteral("actress_id"),
                          actressIds) ||
        !replaceRelations(QStringLiteral("work_actor_relation"), QStringLiteral("actor_id"),
                          actorIds) ||
        !replaceRelations(QStringLiteral("work_tag_relation"), QStringLiteral("tag_id"), tagIds))
    {
        return false;
    }
    if (!transaction.commit())
    {
        if (errorMessage != nullptr)
            *errorMessage = transaction.errorString();
        return false;
    }
    return true;
}

} // namespace darkeye
