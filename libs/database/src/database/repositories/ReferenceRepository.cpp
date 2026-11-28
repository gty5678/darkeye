#include "database/repositories/ReferenceRepository.h"

#include "database/Transaction.h"

#include <QColor>
#include <QSet>
#include <QSqlError>
#include <QSqlQuery>

namespace darkeye::reference_schema
{

QString tableName(ReferenceKind kind)
{
    switch (kind)
    {
    case ReferenceKind::Maker:
        return QStringLiteral("maker");
    case ReferenceKind::Label:
        return QStringLiteral("label");
    case ReferenceKind::Series:
        return QStringLiteral("series");
    }
    return {};
}

QString idColumn(ReferenceKind kind)
{
    return tableName(kind) + QStringLiteral("_id");
}

QString extraColumn(ReferenceKind kind)
{
    switch (kind)
    {
    case ReferenceKind::Maker:
        return QStringLiteral("logo_url");
    case ReferenceKind::Series:
        return QStringLiteral("related_series");
    case ReferenceKind::Label:
        return {};
    }
    return {};
}

} // namespace darkeye::reference_schema

namespace
{

using darkeye::reference_schema::extraColumn;
using darkeye::reference_schema::idColumn;
using darkeye::reference_schema::tableName;

bool validateRecord(const darkeye::ReferenceRecord &record, QString *errorMessage)
{
    if (!record.chineseName.trimmed().isEmpty() || !record.japaneseName.trimmed().isEmpty())
        return true;
    if (errorMessage != nullptr)
        *errorMessage = QStringLiteral("中文名和日文名不能同时为空");
    return false;
}

std::optional<darkeye::ReferenceRecord>
findRecord(QSqlDatabase database, darkeye::ReferenceKind kind, qint64 id, QString *errorMessage)
{
    const QString extra = extraColumn(kind);
    QSqlQuery query(database);
    query.prepare(QStringLiteral("SELECT %1, cn_name, jp_name, aliases, detail, %2 "
                                 "FROM %3 WHERE %1=?")
                      .arg(idColumn(kind), extra.isEmpty() ? QStringLiteral("NULL") : extra,
                           tableName(kind)));
    query.addBindValue(id);
    if (!query.exec())
    {
        if (errorMessage != nullptr)
            *errorMessage = query.lastError().text();
        return std::nullopt;
    }
    if (!query.next())
    {
        if (errorMessage != nullptr)
            *errorMessage = QStringLiteral("参考资料不存在");
        return std::nullopt;
    }
    return darkeye::ReferenceRecord{kind,
                                    query.value(0).toLongLong(),
                                    query.value(1).toString(),
                                    query.value(2).toString(),
                                    query.value(3).toString(),
                                    query.value(4).toString(),
                                    query.value(5).toString()};
}

QString mergedAliases(const darkeye::ReferenceRecord &source,
                      const darkeye::ReferenceRecord &target)
{
    QStringList result;
    QSet<QString> seen;
    const auto append = [&result, &seen](const QString &value)
    {
        const QString normalized = value.trimmed();
        if (normalized.isEmpty() || seen.contains(normalized))
            return;
        seen.insert(normalized);
        result.append(normalized);
    };
    for (const QString &alias : target.aliases.split(',', Qt::SkipEmptyParts))
        append(alias);
    append(source.chineseName);
    append(source.japaneseName);
    for (const QString &alias : source.aliases.split(',', Qt::SkipEmptyParts))
        append(alias);
    return result.join(',');
}

bool validateTag(const darkeye::TagRecord &record, QString *errorMessage)
{
    if (record.name.trimmed().isEmpty())
    {
        if (errorMessage != nullptr)
            *errorMessage = QStringLiteral("标签名称不能为空");
        return false;
    }
    if (!QColor(record.color).isValid())
    {
        if (errorMessage != nullptr)
            *errorMessage = QStringLiteral("标签颜色无效");
        return false;
    }
    return true;
}

bool insertTagAliases(QSqlDatabase database, qint64 tagId, const QString &rootName,
                      const QStringList &aliases, QString *errorMessage)
{
    QSet<QString> inserted;
    inserted.insert(rootName.trimmed());
    QSqlQuery query(database);
    query.prepare(QStringLiteral("INSERT INTO tag(tag_name, redirect_tag_id) VALUES(?, ?)"));
    for (const QString &rawAlias : aliases)
    {
        const QString alias = rawAlias.trimmed();
        if (alias.isEmpty() || inserted.contains(alias))
            continue;
        query.bindValue(0, alias);
        query.bindValue(1, tagId);
        if (!query.exec())
        {
            if (errorMessage != nullptr)
                *errorMessage = query.lastError().text();
            return false;
        }
        inserted.insert(alias);
    }
    return true;
}

} // namespace

namespace darkeye
{

ReferenceRepository::ReferenceRepository(QSqlDatabase database) : m_database(std::move(database)) {}

QList<ReferenceRecord> ReferenceRepository::list(ReferenceKind kind, QString *errorMessage) const
{
    const QString extra = extraColumn(kind);
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral("SELECT %1, cn_name, jp_name, aliases, detail, %2 "
                                 "FROM %3 ORDER BY %1 DESC")
                      .arg(idColumn(kind), extra.isEmpty() ? QStringLiteral("NULL") : extra,
                           tableName(kind)));
    if (!query.exec())
    {
        if (errorMessage != nullptr)
            *errorMessage = query.lastError().text();
        return {};
    }
    QList<ReferenceRecord> records;
    while (query.next())
    {
        records.append({kind, query.value(0).toLongLong(), query.value(1).toString(),
                        query.value(2).toString(), query.value(3).toString(),
                        query.value(4).toString(), query.value(5).toString()});
    }
    return records;
}

std::optional<qint64> ReferenceRepository::create(const ReferenceRecord &record,
                                                  QString *errorMessage)
{
    if (!validateRecord(record, errorMessage))
        return std::nullopt;
    const QString extra = extraColumn(record.kind);
    QSqlQuery query(m_database);
    if (extra.isEmpty())
    {
        query.prepare(QStringLiteral("INSERT INTO %1(cn_name, jp_name, aliases, detail) "
                                     "VALUES(?, ?, ?, ?)")
                          .arg(tableName(record.kind)));
    }
    else
    {
        query.prepare(QStringLiteral("INSERT INTO %1(cn_name, jp_name, aliases, detail, %2) "
                                     "VALUES(?, ?, ?, ?, ?)")
                          .arg(tableName(record.kind), extra));
    }
    query.addBindValue(record.chineseName.trimmed());
    query.addBindValue(record.japaneseName.trimmed());
    query.addBindValue(record.aliases.trimmed());
    query.addBindValue(record.detail.trimmed());
    if (!extra.isEmpty())
        query.addBindValue(record.extra.trimmed());
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

bool ReferenceRepository::update(const ReferenceRecord &record, QString *errorMessage)
{
    if (record.id <= 0 || !validateRecord(record, errorMessage))
    {
        if (record.id <= 0 && errorMessage != nullptr)
            *errorMessage = QStringLiteral("参考资料 ID 无效");
        return false;
    }
    const QString extra = extraColumn(record.kind);
    QSqlQuery query(m_database);
    QString assignments = QStringLiteral("cn_name=?, jp_name=?, aliases=?, detail=?");
    if (!extra.isEmpty())
        assignments += QStringLiteral(", %1=?").arg(extra);
    query.prepare(QStringLiteral("UPDATE %1 SET %2 WHERE %3=?")
                      .arg(tableName(record.kind), assignments, idColumn(record.kind)));
    query.addBindValue(record.chineseName.trimmed());
    query.addBindValue(record.japaneseName.trimmed());
    query.addBindValue(record.aliases.trimmed());
    query.addBindValue(record.detail.trimmed());
    if (!extra.isEmpty())
        query.addBindValue(record.extra.trimmed());
    query.addBindValue(record.id);
    if (!query.exec())
    {
        if (errorMessage != nullptr)
            *errorMessage = query.lastError().text();
        return false;
    }
    if (query.numRowsAffected() == 0)
    {
        if (errorMessage != nullptr)
            *errorMessage = QStringLiteral("参考资料不存在");
        return false;
    }
    return true;
}

bool ReferenceRepository::remove(ReferenceKind kind, qint64 id, QString *errorMessage)
{
    if (id <= 0)
    {
        if (errorMessage != nullptr)
            *errorMessage = QStringLiteral("参考资料 ID 无效");
        return false;
    }
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral("DELETE FROM %1 WHERE %2=?").arg(tableName(kind), idColumn(kind)));
    query.addBindValue(id);
    if (!query.exec())
    {
        if (errorMessage != nullptr)
            *errorMessage = query.lastError().text();
        return false;
    }
    if (query.numRowsAffected() == 0)
    {
        if (errorMessage != nullptr)
            *errorMessage = QStringLiteral("参考资料不存在");
        return false;
    }
    return true;
}

bool ReferenceRepository::redirect(ReferenceKind kind, qint64 sourceId, qint64 targetId,
                                   QString *errorMessage)
{
    if (sourceId <= 0 || targetId <= 0 || sourceId == targetId)
    {
        if (errorMessage != nullptr)
            *errorMessage = QStringLiteral("请选择两个不同的有效条目");
        return false;
    }
    Transaction transaction(m_database);
    if (!transaction.isActive())
    {
        if (errorMessage != nullptr)
            *errorMessage = transaction.errorString();
        return false;
    }
    const auto source = findRecord(m_database, kind, sourceId, errorMessage);
    const auto target = findRecord(m_database, kind, targetId, errorMessage);
    if (!source.has_value() || !target.has_value())
        return false;

    ReferenceRecord merged = *target;
    merged.aliases = mergedAliases(*source, *target);
    if (!update(merged, errorMessage))
        return false;

    QSqlQuery workUpdate(m_database);
    const QString foreignKey = idColumn(kind);
    workUpdate.prepare(QStringLiteral("UPDATE work SET %1=? WHERE %1=?").arg(foreignKey));
    workUpdate.addBindValue(targetId);
    workUpdate.addBindValue(sourceId);
    if (!workUpdate.exec())
    {
        if (errorMessage != nullptr)
            *errorMessage = workUpdate.lastError().text();
        return false;
    }
    if (kind == ReferenceKind::Maker)
    {
        QSqlQuery prefixUpdate(m_database);
        prefixUpdate.prepare(
            QStringLiteral("UPDATE prefix_maker_relation SET maker_id=? WHERE maker_id=?"));
        prefixUpdate.addBindValue(targetId);
        prefixUpdate.addBindValue(sourceId);
        if (!prefixUpdate.exec())
        {
            if (errorMessage != nullptr)
                *errorMessage = prefixUpdate.lastError().text();
            return false;
        }
    }
    if (!remove(kind, sourceId, errorMessage))
        return false;
    if (!transaction.commit())
    {
        if (errorMessage != nullptr)
            *errorMessage = transaction.errorString();
        return false;
    }
    return true;
}

std::optional<qint64> ReferenceRepository::resolveTagId(qint64 tagId) const
{
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral("SELECT COALESCE(redirect_tag_id, tag_id) FROM tag WHERE tag_id=?"));
    query.addBindValue(tagId);
    if (!query.exec() || !query.next())
        return std::nullopt;
    return query.value(0).toLongLong();
}

QList<TagRecord> ReferenceRepository::listTags(QString *errorMessage) const
{
    QSqlQuery query(m_database);
    if (!query.exec(QStringLiteral(
            "SELECT tag.tag_id, tag.tag_name, tag.tag_type_id, "
            "COALESCE(tag_type.tag_type_name, ''), COALESCE(tag.color, '#cccccc'), "
            "COALESCE(tag.detail, ''), tag.redirect_tag_id, tag.group_id, "
            "COALESCE(GROUP_CONCAT(alias.tag_name, CHAR(31)), '') "
            "FROM tag LEFT JOIN tag_type ON tag_type.tag_type_id=tag.tag_type_id "
            "LEFT JOIN tag AS alias ON alias.redirect_tag_id=tag.tag_id "
            "WHERE tag.redirect_tag_id IS NULL "
            "GROUP BY tag.tag_id "
            "ORDER BY COALESCE(tag_type.tag_order, 2147483647), tag.color")))
    {
        if (errorMessage != nullptr)
            *errorMessage = query.lastError().text();
        return {};
    }
    QList<TagRecord> records;
    while (query.next())
    {
        TagRecord record;
        record.id = query.value(0).toLongLong();
        record.name = query.value(1).toString();
        if (!query.value(2).isNull())
            record.typeId = query.value(2).toLongLong();
        record.typeName = query.value(3).toString();
        record.color = query.value(4).toString();
        record.detail = query.value(5).toString();
        if (!query.value(6).isNull())
            record.redirectTagId = query.value(6).toLongLong();
        if (!query.value(7).isNull())
            record.groupId = query.value(7).toLongLong();
        record.aliases = query.value(8).toString().split(QChar(31), Qt::SkipEmptyParts);
        records.append(record);
    }
    return records;
}

QList<TagTypeRecord> ReferenceRepository::listTagTypes(QString *errorMessage) const
{
    QSqlQuery query(m_database);
    if (!query.exec(QStringLiteral(
            "SELECT tag_type_id, tag_type_name, COALESCE(tag_order, 0) FROM tag_type "
            "ORDER BY COALESCE(tag_order, 2147483647), tag_type_id")))
    {
        if (errorMessage != nullptr)
            *errorMessage = query.lastError().text();
        return {};
    }
    QList<TagTypeRecord> records;
    while (query.next())
        records.append(
            {query.value(0).toLongLong(), query.value(1).toString(), query.value(2).toInt()});
    return records;
}

std::optional<qint64> ReferenceRepository::createTag(const TagRecord &record, QString *errorMessage)
{
    if (!validateTag(record, errorMessage))
        return std::nullopt;
    Transaction transaction(m_database);
    if (!transaction.isActive())
    {
        if (errorMessage != nullptr)
            *errorMessage = transaction.errorString();
        return std::nullopt;
    }
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral("INSERT INTO tag(tag_name, tag_type_id, color, detail, group_id) "
                                 "VALUES(?, ?, ?, ?, ?)"));
    query.addBindValue(record.name.trimmed());
    query.addBindValue(record.typeId.has_value() ? QVariant(*record.typeId) : QVariant());
    query.addBindValue(record.color.trimmed());
    query.addBindValue(record.detail.trimmed());
    query.addBindValue(record.groupId.has_value() ? QVariant(*record.groupId) : QVariant());
    if (!query.exec())
    {
        if (errorMessage != nullptr)
            *errorMessage = query.lastError().text();
        return std::nullopt;
    }
    const qint64 id = query.lastInsertId().toLongLong();
    if (!insertTagAliases(m_database, id, record.name, record.aliases, errorMessage))
        return std::nullopt;
    if (!transaction.commit())
    {
        if (errorMessage != nullptr)
            *errorMessage = transaction.errorString();
        return std::nullopt;
    }
    return id;
}

bool ReferenceRepository::updateTag(const TagRecord &record, QString *errorMessage)
{
    if (record.id <= 0 || !validateTag(record, errorMessage))
    {
        if (record.id <= 0 && errorMessage != nullptr)
            *errorMessage = QStringLiteral("标签 ID 无效");
        return false;
    }
    Transaction transaction(m_database);
    if (!transaction.isActive())
    {
        if (errorMessage != nullptr)
            *errorMessage = transaction.errorString();
        return false;
    }
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral("UPDATE tag SET tag_name=?, tag_type_id=?, color=?, detail=?, "
                                 "group_id=? WHERE tag_id=? AND redirect_tag_id IS NULL"));
    query.addBindValue(record.name.trimmed());
    query.addBindValue(record.typeId.has_value() ? QVariant(*record.typeId) : QVariant());
    query.addBindValue(record.color.trimmed());
    query.addBindValue(record.detail.trimmed());
    query.addBindValue(record.groupId.has_value() ? QVariant(*record.groupId) : QVariant());
    query.addBindValue(record.id);
    if (!query.exec() || query.numRowsAffected() == 0)
    {
        if (errorMessage != nullptr)
            *errorMessage = query.lastError().text().isEmpty() ? QStringLiteral("标签不存在")
                                                               : query.lastError().text();
        return false;
    }
    query.prepare(QStringLiteral("DELETE FROM tag WHERE redirect_tag_id=?"));
    query.addBindValue(record.id);
    if (!query.exec())
    {
        if (errorMessage != nullptr)
            *errorMessage = query.lastError().text();
        return false;
    }
    if (!insertTagAliases(m_database, record.id, record.name, record.aliases, errorMessage))
        return false;
    if (!transaction.commit())
    {
        if (errorMessage != nullptr)
            *errorMessage = transaction.errorString();
        return false;
    }
    return true;
}

bool ReferenceRepository::updateTagColors(const QList<qint64> &tagIds, const QString &color,
                                          QString *errorMessage)
{
    if (tagIds.isEmpty() || color.trimmed().isEmpty())
    {
        if (errorMessage != nullptr)
            *errorMessage = QStringLiteral("请选择标签并指定颜色");
        return false;
    }
    Transaction transaction(m_database);
    if (!transaction.isActive())
    {
        if (errorMessage != nullptr)
            *errorMessage = transaction.errorString();
        return false;
    }
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral("UPDATE tag SET color=? "
                                 "WHERE tag_id=? AND redirect_tag_id IS NULL"));
    for (qint64 id : tagIds)
    {
        if (id <= 0)
            continue;
        query.bindValue(0, color.trimmed());
        query.bindValue(1, id);
        if (!query.exec())
        {
            if (errorMessage != nullptr)
                *errorMessage = query.lastError().text();
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

bool ReferenceRepository::removeTag(qint64 tagId, QString *errorMessage)
{
    if (tagId <= 0)
    {
        if (errorMessage != nullptr)
            *errorMessage = QStringLiteral("标签 ID 无效");
        return false;
    }
    Transaction transaction(m_database);
    if (!transaction.isActive())
    {
        if (errorMessage != nullptr)
            *errorMessage = transaction.errorString();
        return false;
    }
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral("DELETE FROM tag WHERE redirect_tag_id=?"));
    query.addBindValue(tagId);
    if (!query.exec())
    {
        if (errorMessage != nullptr)
            *errorMessage = query.lastError().text();
        return false;
    }
    query.prepare(QStringLiteral("DELETE FROM tag WHERE tag_id=? AND redirect_tag_id IS NULL"));
    query.addBindValue(tagId);
    if (!query.exec() || query.numRowsAffected() == 0)
    {
        if (errorMessage != nullptr)
            *errorMessage = query.lastError().text().isEmpty() ? QStringLiteral("标签不存在")
                                                               : query.lastError().text();
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

bool ReferenceRepository::redirectTag(qint64 sourceId, qint64 targetId, QString *errorMessage)
{
    if (sourceId <= 0 || targetId <= 0 || sourceId == targetId)
    {
        if (errorMessage != nullptr)
            *errorMessage = QStringLiteral("请选择两个不同的有效标签");
        return false;
    }
    Transaction transaction(m_database);
    if (!transaction.isActive())
    {
        if (errorMessage != nullptr)
            *errorMessage = transaction.errorString();
        return false;
    }
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "SELECT COUNT(*) FROM tag WHERE tag_id IN (?, ?) AND redirect_tag_id IS NULL"));
    query.addBindValue(sourceId);
    query.addBindValue(targetId);
    if (!query.exec() || !query.next() || query.value(0).toInt() != 2)
    {
        if (errorMessage != nullptr)
            *errorMessage = query.lastError().text().isEmpty() ? QStringLiteral("标签不存在")
                                                               : query.lastError().text();
        return false;
    }
    query.prepare(QStringLiteral("UPDATE tag SET redirect_tag_id=? WHERE redirect_tag_id=?"));
    query.addBindValue(targetId);
    query.addBindValue(sourceId);
    if (!query.exec())
    {
        if (errorMessage != nullptr)
            *errorMessage = query.lastError().text();
        return false;
    }
    query.prepare(QStringLiteral("DELETE FROM work_tag_relation WHERE tag_id=? AND work_id IN "
                                 "(SELECT work_id FROM work_tag_relation WHERE tag_id=?)"));
    query.addBindValue(sourceId);
    query.addBindValue(targetId);
    if (!query.exec())
    {
        if (errorMessage != nullptr)
            *errorMessage = query.lastError().text();
        return false;
    }
    query.prepare(QStringLiteral("UPDATE work_tag_relation SET tag_id=? WHERE tag_id=?"));
    query.addBindValue(targetId);
    query.addBindValue(sourceId);
    if (!query.exec())
    {
        if (errorMessage != nullptr)
            *errorMessage = query.lastError().text();
        return false;
    }
    query.prepare(QStringLiteral("UPDATE tag SET redirect_tag_id=? WHERE tag_id=?"));
    query.addBindValue(targetId);
    query.addBindValue(sourceId);
    if (!query.exec() || query.numRowsAffected() == 0)
    {
        if (errorMessage != nullptr)
            *errorMessage = query.lastError().text().isEmpty() ? QStringLiteral("标签不存在")
                                                               : query.lastError().text();
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

std::optional<qint64> ReferenceRepository::createTagType(const QString &name, int order,
                                                         QString *errorMessage)
{
    if (name.trimmed().isEmpty())
    {
        if (errorMessage != nullptr)
            *errorMessage = QStringLiteral("标签类型名称不能为空");
        return std::nullopt;
    }
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral("INSERT INTO tag_type(tag_type_name, tag_order) VALUES(?, ?)"));
    query.addBindValue(name.trimmed());
    query.addBindValue(order);
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

bool ReferenceRepository::updateTagType(const TagTypeRecord &record, QString *errorMessage)
{
    if (record.id <= 0 || record.name.trimmed().isEmpty())
    {
        if (errorMessage != nullptr)
            *errorMessage = QStringLiteral("标签类型名称或 ID 无效");
        return false;
    }
    QSqlQuery query(m_database);
    query.prepare(
        QStringLiteral("UPDATE tag_type SET tag_type_name=?, tag_order=? WHERE tag_type_id=?"));
    query.addBindValue(record.name.trimmed());
    query.addBindValue(record.order);
    query.addBindValue(record.id);
    if (!query.exec() || query.numRowsAffected() == 0)
    {
        if (errorMessage != nullptr)
            *errorMessage = query.lastError().text().isEmpty() ? QStringLiteral("标签类型不存在")
                                                               : query.lastError().text();
        return false;
    }
    return true;
}

bool ReferenceRepository::removeTagType(qint64 typeId, QString *errorMessage)
{
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral("DELETE FROM tag_type WHERE tag_type_id=?"));
    query.addBindValue(typeId);
    if (!query.exec() || query.numRowsAffected() == 0)
    {
        if (errorMessage != nullptr)
            *errorMessage = query.lastError().text().isEmpty() ? QStringLiteral("标签类型不存在")
                                                               : query.lastError().text();
        return false;
    }
    return true;
}

bool ReferenceRepository::moveTagType(qint64 typeId, int offset, QString *errorMessage)
{
    QList<TagTypeRecord> types = listTagTypes(errorMessage);
    int source = -1;
    for (int index = 0; index < types.size(); ++index)
    {
        if (types.at(index).id == typeId)
        {
            source = index;
            break;
        }
    }
    const int target = source + offset;
    if (source < 0 || target < 0 || target >= types.size())
    {
        if (errorMessage != nullptr)
            *errorMessage = source < 0 ? QStringLiteral("标签类型不存在")
                                       : QStringLiteral("标签类型已位于边界");
        return false;
    }
    const TagTypeRecord moved = types.takeAt(source);
    types.insert(target, moved);
    Transaction transaction(m_database);
    if (!transaction.isActive())
    {
        if (errorMessage != nullptr)
            *errorMessage = transaction.errorString();
        return false;
    }
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral("UPDATE tag_type SET tag_order=? WHERE tag_type_id=?"));
    for (int index = 0; index < types.size(); ++index)
    {
        query.bindValue(0, index + 1);
        query.bindValue(1, types.at(index).id);
        if (!query.exec())
        {
            if (errorMessage != nullptr)
                *errorMessage = query.lastError().text();
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

QList<MakerPrefixRecord> ReferenceRepository::listMakerPrefixes(QString *errorMessage) const
{
    QSqlQuery query(m_database);
    if (!query.exec(QStringLiteral(
            "SELECT relation.prefix_maker_relation_id, COALESCE(relation.prefix, ''), "
            "relation.maker_id, COALESCE(NULLIF(maker.cn_name, ''), maker.jp_name, '') "
            "FROM prefix_maker_relation AS relation "
            "LEFT JOIN maker ON maker.maker_id=relation.maker_id "
            "ORDER BY relation.prefix COLLATE NOCASE, relation.prefix_maker_relation_id")))
    {
        if (errorMessage != nullptr)
            *errorMessage = query.lastError().text();
        return {};
    }
    QList<MakerPrefixRecord> records;
    while (query.next())
    {
        records.append({query.value(0).toLongLong(), query.value(1).toString(),
                        query.value(2).toLongLong(), query.value(3).toString()});
    }
    return records;
}

std::optional<qint64> ReferenceRepository::createMakerPrefix(const QString &prefix, qint64 makerId,
                                                             QString *errorMessage)
{
    const QString normalized = prefix.trimmed();
    if (normalized.isEmpty() || makerId <= 0)
    {
        if (errorMessage != nullptr)
            *errorMessage = QStringLiteral("番号前缀或片商无效");
        return std::nullopt;
    }
    QSqlQuery query(m_database);
    query.prepare(
        QStringLiteral("INSERT INTO prefix_maker_relation(prefix, maker_id) VALUES(?, ?)"));
    query.addBindValue(normalized);
    query.addBindValue(makerId);
    if (!query.exec())
    {
        if (errorMessage != nullptr)
            *errorMessage = query.lastError().text();
        return std::nullopt;
    }
    return query.lastInsertId().toLongLong();
}

bool ReferenceRepository::updateMakerPrefix(const MakerPrefixRecord &record, QString *errorMessage)
{
    const QString normalized = record.prefix.trimmed();
    if (record.id <= 0 || normalized.isEmpty() || record.makerId <= 0)
    {
        if (errorMessage != nullptr)
            *errorMessage = QStringLiteral("番号前缀记录无效");
        return false;
    }
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral("UPDATE prefix_maker_relation SET prefix=?, maker_id=? "
                                 "WHERE prefix_maker_relation_id=?"));
    query.addBindValue(normalized);
    query.addBindValue(record.makerId);
    query.addBindValue(record.id);
    if (!query.exec() || query.numRowsAffected() == 0)
    {
        if (errorMessage != nullptr)
            *errorMessage = query.lastError().text().isEmpty() ? QStringLiteral("番号前缀不存在")
                                                               : query.lastError().text();
        return false;
    }
    return true;
}

bool ReferenceRepository::removeMakerPrefix(qint64 id, QString *errorMessage)
{
    QSqlQuery query(m_database);
    query.prepare(
        QStringLiteral("DELETE FROM prefix_maker_relation WHERE prefix_maker_relation_id=?"));
    query.addBindValue(id);
    if (!query.exec() || query.numRowsAffected() == 0)
    {
        if (errorMessage != nullptr)
            *errorMessage = query.lastError().text().isEmpty() ? QStringLiteral("番号前缀不存在")
                                                               : query.lastError().text();
        return false;
    }
    return true;
}

std::optional<qint64> ReferenceRepository::findByName(ReferenceKind kind, const QString &name,
                                                      QString *errorMessage) const
{
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral("SELECT %1 FROM %2 WHERE cn_name=? OR jp_name=? LIMIT 1")
                      .arg(idColumn(kind), tableName(kind)));
    query.addBindValue(name.trimmed());
    query.addBindValue(name.trimmed());
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

} // namespace darkeye
