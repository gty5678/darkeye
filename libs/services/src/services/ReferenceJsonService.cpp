#include "services/ReferenceJsonService.h"

#include "database/Transaction.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QSaveFile>
#include <QSet>
#include <QSqlError>
#include <QSqlQuery>
#include <QVariant>

#include <optional>

namespace
{

using darkeye::reference_schema::extraColumn;
using darkeye::reference_schema::idColumn;
using darkeye::reference_schema::tableName;

struct ImportEntry final
{
    QJsonObject object;
    QString chineseName;
    QSet<QString> aliases;
    QStringList prefixes;
    qint64 assignedId = 0;
};

struct OldEntry final
{
    qint64 id = 0;
    QString chineseName;
};

QJsonValue jsonValue(const QVariant &value)
{
    return value.isNull() ? QJsonValue(QJsonValue::Null) : QJsonValue(value.toString());
}

QVariant stringField(const QJsonObject &object, const QString &key, bool *valid)
{
    const QJsonValue value = object.value(key);
    if (value.isUndefined() || value.isNull())
        return {};
    if (!value.isString())
    {
        *valid = false;
        return {};
    }
    return value.toString();
}

QSet<QString> aliasSet(const QVariant &aliases)
{
    QString normalized = aliases.toString();
    normalized.replace(QChar(0xff0c), QChar(','));
    QSet<QString> result;
    for (const QString &alias : normalized.split(',', Qt::SkipEmptyParts))
    {
        const QString trimmed = alias.trimmed();
        if (!trimmed.isEmpty())
            result.insert(trimmed);
    }
    return result;
}

bool matches(const OldEntry &old, const ImportEntry &entry)
{
    const QString name = old.chineseName.trimmed();
    return !name.isEmpty() && (entry.chineseName.trimmed() == name || entry.aliases.contains(name));
}

bool setError(QString *errorMessage, const QString &message)
{
    if (errorMessage != nullptr)
        *errorMessage = message;
    return false;
}

QList<OldEntry> loadOldEntries(QSqlDatabase database, darkeye::ReferenceKind kind,
                               QString *errorMessage)
{
    QSqlQuery query(database);
    if (!query.exec(QStringLiteral("SELECT %1, COALESCE(cn_name, '') FROM %2 ORDER BY %1")
                        .arg(idColumn(kind), tableName(kind))))
    {
        if (errorMessage != nullptr)
            *errorMessage = query.lastError().text();
        return {};
    }
    QList<OldEntry> entries;
    while (query.next())
        entries.append({query.value(0).toLongLong(), query.value(1).toString()});
    return entries;
}

bool bindAndExecuteReference(QSqlDatabase database, darkeye::ReferenceKind kind,
                             const ImportEntry &entry, bool update, qint64 id, qint64 *insertedId,
                             QString *errorMessage)
{
    bool valid = true;
    const QVariant chinese = stringField(entry.object, QStringLiteral("cn_name"), &valid);
    const QVariant japanese = stringField(entry.object, QStringLiteral("jp_name"), &valid);
    const QVariant aliases = stringField(entry.object, QStringLiteral("aliases"), &valid);
    const QVariant detail = stringField(entry.object, QStringLiteral("detail"), &valid);
    // The JSON format uses the database column name for the extra field.
    const QString extraKey = extraColumn(kind);
    const QVariant extra =
        extraKey.isEmpty() ? QVariant() : stringField(entry.object, extraKey, &valid);
    if (!valid)
        return setError(errorMessage, QStringLiteral("参考资料字段必须是字符串或 null"));

    QStringList columns{QStringLiteral("cn_name"), QStringLiteral("jp_name"),
                        QStringLiteral("aliases"), QStringLiteral("detail")};
    QList<QVariant> values{chinese, japanese, aliases, detail};
    if (!extraKey.isEmpty())
    {
        columns.append(extraKey);
        values.append(extra);
    }
    QSqlQuery query(database);
    if (update)
    {
        QStringList assignments;
        for (const QString &column : columns)
            assignments.append(column + QStringLiteral("=?"));
        query.prepare(QStringLiteral("UPDATE %1 SET %2 WHERE %3=?")
                          .arg(tableName(kind), assignments.join(','), idColumn(kind)));
    }
    else
    {
        QStringList placeholders;
        placeholders.fill(QStringLiteral("?"), columns.size());
        query.prepare(QStringLiteral("INSERT INTO %1(%2) VALUES(%3)")
                          .arg(tableName(kind), columns.join(','), placeholders.join(',')));
    }
    for (const QVariant &value : values)
        query.addBindValue(value);
    if (update)
        query.addBindValue(id);
    if (!query.exec())
        return setError(errorMessage, query.lastError().text());
    if (!update && insertedId != nullptr)
        *insertedId = query.lastInsertId().toLongLong();
    return true;
}

} // namespace

namespace darkeye
{

ReferenceJsonService::ReferenceJsonService(QSqlDatabase database) : m_database(std::move(database))
{
}

bool ReferenceJsonService::exportToFile(ReferenceKind kind, const QString &path,
                                        QString *errorMessage) const
{
    if (path.trimmed().isEmpty())
        return setError(errorMessage, QStringLiteral("导出路径不能为空"));
    const QString extra = extraColumn(kind);
    QSqlQuery query(m_database);
    if (!query.exec(QStringLiteral("SELECT %1, cn_name, jp_name, aliases, detail, %2 "
                                   "FROM %3 ORDER BY %1")
                        .arg(idColumn(kind), extra.isEmpty() ? QStringLiteral("NULL") : extra,
                             tableName(kind))))
        return setError(errorMessage, query.lastError().text());

    QJsonArray result;
    while (query.next())
    {
        QJsonObject object;
        object.insert(QStringLiteral("cn_name"), jsonValue(query.value(1)));
        object.insert(QStringLiteral("jp_name"), jsonValue(query.value(2)));
        object.insert(QStringLiteral("aliases"), jsonValue(query.value(3)));
        object.insert(QStringLiteral("detail"), jsonValue(query.value(4)));
        if (!extra.isEmpty())
            object.insert(extra, jsonValue(query.value(5)));
        if (kind == ReferenceKind::Maker)
        {
            QSqlQuery prefixQuery(m_database);
            prefixQuery.prepare(QStringLiteral(
                "SELECT prefix FROM prefix_maker_relation WHERE maker_id=? ORDER BY prefix"));
            prefixQuery.addBindValue(query.value(0));
            if (!prefixQuery.exec())
                return setError(errorMessage, prefixQuery.lastError().text());
            QJsonArray prefixes;
            QSet<QString> seen;
            while (prefixQuery.next())
            {
                const QString prefix = prefixQuery.value(0).toString();
                if (!prefix.isEmpty() && !seen.contains(prefix))
                {
                    prefixes.append(prefix);
                    seen.insert(prefix);
                }
            }
            object.insert(QStringLiteral("prefixes"), prefixes);
        }
        result.append(object);
    }

    const QFileInfo info(path);
    if (!QDir().mkpath(info.absolutePath()))
        return setError(errorMessage, QStringLiteral("无法创建导出目录"));
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        return setError(errorMessage, file.errorString());
    if (file.write(QJsonDocument(result).toJson(QJsonDocument::Indented)) < 0 || !file.commit())
        return setError(errorMessage, file.errorString());
    return true;
}

bool ReferenceJsonService::importFromFile(ReferenceKind kind, const QString &path,
                                          QString *errorMessage)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return setError(errorMessage, file.errorString());
    constexpr qint64 maximumBytes = 64 * 1024 * 1024;
    if (file.size() > maximumBytes)
        return setError(errorMessage, QStringLiteral("JSON 文件超过 64 MiB 限制"));
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError)
        return setError(errorMessage,
                        QStringLiteral("解析 JSON 失败：%1").arg(parseError.errorString()));
    if (!document.isArray())
        return setError(errorMessage, QStringLiteral("JSON 顶层结构必须是列表"));

    const QString extraKey = extraColumn(kind);
    QList<ImportEntry> entries;
    for (const QJsonValue &value : document.array())
    {
        if (!value.isObject())
            return setError(errorMessage, QStringLiteral("参考资料列表中的每一项都必须是对象"));
        ImportEntry entry;
        entry.object = value.toObject();
        bool valid = true;
        const QVariant chinese = stringField(entry.object, QStringLiteral("cn_name"), &valid);
        const QVariant japanese = stringField(entry.object, QStringLiteral("jp_name"), &valid);
        const QVariant aliases = stringField(entry.object, QStringLiteral("aliases"), &valid);
        stringField(entry.object, QStringLiteral("detail"), &valid);
        if (!extraKey.isEmpty())
            stringField(entry.object, extraKey, &valid);
        if (!valid)
            return setError(errorMessage, QStringLiteral("参考资料字段必须是字符串或 null"));
        if (chinese.toString().trimmed().isEmpty() && japanese.toString().trimmed().isEmpty())
            return setError(errorMessage, QStringLiteral("中文名和日文名不能同时为空"));
        entry.chineseName = chinese.toString();
        entry.aliases = aliasSet(aliases);
        if (kind == ReferenceKind::Maker)
        {
            const QJsonValue prefixes = entry.object.value(QStringLiteral("prefixes"));
            if (!prefixes.isUndefined() && !prefixes.isNull() && !prefixes.isArray())
                return setError(errorMessage, QStringLiteral("prefixes 必须是列表"));
            QSet<QString> seen;
            for (const QJsonValue &prefixValue : prefixes.toArray())
            {
                if (!prefixValue.isString())
                    return setError(errorMessage, QStringLiteral("番号前缀必须是字符串"));
                const QString prefix = prefixValue.toString().trimmed();
                if (!prefix.isEmpty() && !seen.contains(prefix))
                {
                    entry.prefixes.append(prefix);
                    seen.insert(prefix);
                }
            }
        }
        entries.append(entry);
    }

    QString loadError;
    const QList<OldEntry> oldEntries = loadOldEntries(m_database, kind, &loadError);
    if (!loadError.isEmpty())
        return setError(errorMessage, loadError);
    Transaction transaction(m_database);
    if (!transaction.isActive())
        return setError(errorMessage, transaction.errorString());

    QSet<qint64> claimedOldIds;
    for (ImportEntry &entry : entries)
    {
        for (const OldEntry &old : oldEntries)
        {
            if (!claimedOldIds.contains(old.id) && matches(old, entry))
            {
                entry.assignedId = old.id;
                claimedOldIds.insert(old.id);
                break;
            }
        }
        if (entry.assignedId > 0)
        {
            if (!bindAndExecuteReference(m_database, kind, entry, true, entry.assignedId, nullptr,
                                         errorMessage))
                return false;
        }
        else if (!bindAndExecuteReference(m_database, kind, entry, false, 0, &entry.assignedId,
                                          errorMessage))
        {
            return false;
        }
    }

    QSet<qint64> desiredIds;
    for (const ImportEntry &entry : entries)
        desiredIds.insert(entry.assignedId);
    const QString foreignKey = idColumn(kind);
    for (const OldEntry &old : oldEntries)
    {
        qint64 mappedId = 0;
        for (const ImportEntry &entry : entries)
        {
            if (matches(old, entry))
            {
                mappedId = entry.assignedId;
                break;
            }
        }
        if (mappedId > 0 && mappedId != old.id)
        {
            QSqlQuery remap(m_database);
            remap.prepare(QStringLiteral("UPDATE work SET %1=? WHERE %1=?").arg(foreignKey));
            remap.addBindValue(mappedId);
            remap.addBindValue(old.id);
            if (!remap.exec())
                return setError(errorMessage, remap.lastError().text());
        }
    }

    if (kind == ReferenceKind::Maker)
    {
        QSqlQuery clearPrefixes(m_database);
        if (!clearPrefixes.exec(QStringLiteral("DELETE FROM prefix_maker_relation")))
            return setError(errorMessage, clearPrefixes.lastError().text());
        QSqlQuery insertPrefix(m_database);
        insertPrefix.prepare(
            QStringLiteral("INSERT INTO prefix_maker_relation(prefix, maker_id) VALUES(?, ?)"));
        for (const ImportEntry &entry : entries)
        {
            for (const QString &prefix : entry.prefixes)
            {
                insertPrefix.bindValue(0, prefix);
                insertPrefix.bindValue(1, entry.assignedId);
                if (!insertPrefix.exec())
                    return setError(errorMessage, insertPrefix.lastError().text());
            }
        }
    }

    for (const OldEntry &old : oldEntries)
    {
        if (desiredIds.contains(old.id))
            continue;
        QSqlQuery used(m_database);
        used.prepare(
            QStringLiteral("SELECT EXISTS(SELECT 1 FROM work WHERE %1=?)").arg(foreignKey));
        used.addBindValue(old.id);
        if (!used.exec() || !used.next())
            return setError(errorMessage, used.lastError().text());
        if (used.value(0).toBool())
            continue;
        QSqlQuery remove(m_database);
        remove.prepare(
            QStringLiteral("DELETE FROM %1 WHERE %2=?").arg(tableName(kind), idColumn(kind)));
        remove.addBindValue(old.id);
        if (!remove.exec())
            return setError(errorMessage, remove.lastError().text());
    }

    if (!transaction.commit())
        return setError(errorMessage, transaction.errorString());
    return true;
}

} // namespace darkeye
