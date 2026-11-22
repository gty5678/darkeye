#include "database/SchemaSnapshot.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QSqlError>
#include <QSqlQuery>

namespace darkeye {
namespace {

QString quotedIdentifier(QString identifier)
{
    identifier.replace(QLatin1Char('"'), QStringLiteral("\"\""));
    return QStringLiteral("\"%1\"").arg(identifier);
}

QJsonValue value(const QVariant &variant)
{
    return variant.isNull() ? QJsonValue(QJsonValue::Null)
                            : QJsonValue::fromVariant(variant);
}

bool execute(QSqlQuery &query, const QString &sql, QString *errorMessage)
{
    if (query.exec(sql)) {
        return true;
    }
    if (errorMessage != nullptr) {
        *errorMessage = query.lastError().text();
    }
    return false;
}

QJsonArray tableInfo(const QSqlDatabase &database, const QString &table,
                     QString *errorMessage)
{
    QSqlQuery query(database);
    if (!execute(query,
                 QStringLiteral("PRAGMA table_xinfo(%1)").arg(quotedIdentifier(table)),
                 errorMessage)) {
        return {};
    }
    QJsonArray result;
    while (query.next()) {
        result.append(QJsonObject{
            {QStringLiteral("cid"), query.value(0).toInt()},
            {QStringLiteral("name"), query.value(1).toString()},
            {QStringLiteral("type"), query.value(2).toString()},
            {QStringLiteral("notNull"), query.value(3).toBool()},
            {QStringLiteral("default"), value(query.value(4))},
            {QStringLiteral("primaryKey"), query.value(5).toInt()},
            {QStringLiteral("hidden"), query.value(6).toInt()},
        });
    }
    return result;
}

QJsonArray foreignKeys(const QSqlDatabase &database, const QString &table,
                       QString *errorMessage)
{
    QSqlQuery query(database);
    if (!execute(query,
                 QStringLiteral("PRAGMA foreign_key_list(%1)")
                     .arg(quotedIdentifier(table)),
                 errorMessage)) {
        return {};
    }
    QJsonArray result;
    while (query.next()) {
        result.append(QJsonObject{
            {QStringLiteral("id"), query.value(0).toInt()},
            {QStringLiteral("seq"), query.value(1).toInt()},
            {QStringLiteral("table"), query.value(2).toString()},
            {QStringLiteral("from"), query.value(3).toString()},
            {QStringLiteral("to"), query.value(4).toString()},
            {QStringLiteral("onUpdate"), query.value(5).toString()},
            {QStringLiteral("onDelete"), query.value(6).toString()},
            {QStringLiteral("match"), query.value(7).toString()},
        });
    }
    return result;
}

QJsonArray indexes(const QSqlDatabase &database, const QString &table,
                   QString *errorMessage)
{
    QSqlQuery query(database);
    if (!execute(query,
                 QStringLiteral("PRAGMA index_list(%1)").arg(quotedIdentifier(table)),
                 errorMessage)) {
        return {};
    }
    QJsonArray result;
    while (query.next()) {
        const QString indexName = query.value(1).toString();
        QSqlQuery columnQuery(database);
        if (!execute(columnQuery,
                     QStringLiteral("PRAGMA index_xinfo(%1)")
                         .arg(quotedIdentifier(indexName)),
                     errorMessage)) {
            return {};
        }
        QJsonArray columns;
        while (columnQuery.next()) {
            columns.append(QJsonObject{
                {QStringLiteral("seq"), columnQuery.value(0).toInt()},
                {QStringLiteral("cid"), columnQuery.value(1).toInt()},
                {QStringLiteral("name"), value(columnQuery.value(2))},
                {QStringLiteral("descending"), columnQuery.value(3).toBool()},
                {QStringLiteral("collation"), value(columnQuery.value(4))},
                {QStringLiteral("key"), columnQuery.value(5).toBool()},
            });
        }
        result.append(QJsonObject{
            {QStringLiteral("seq"), query.value(0).toInt()},
            {QStringLiteral("name"), indexName},
            {QStringLiteral("unique"), query.value(2).toBool()},
            {QStringLiteral("origin"), query.value(3).toString()},
            {QStringLiteral("partial"), query.value(4).toBool()},
            {QStringLiteral("columns"), columns},
        });
    }
    return result;
}

} // namespace

QJsonDocument SchemaSnapshot::capture(const QSqlDatabase &database,
                                      QString *errorMessage)
{
    if (!database.isOpen()) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("数据库尚未打开");
        }
        return {};
    }

    QSqlQuery versionQuery(database);
    if (!execute(versionQuery, QStringLiteral("PRAGMA user_version"), errorMessage)
        || !versionQuery.next()) {
        return {};
    }

    QJsonObject root{
        {QStringLiteral("formatVersion"), 1},
        {QStringLiteral("userVersion"), versionQuery.value(0).toInt()},
    };
    QJsonArray objects;
    QSqlQuery objectQuery(database);
    if (!execute(objectQuery,
                 QStringLiteral(
                     "SELECT type, name, tbl_name, sql FROM sqlite_schema "
                     "WHERE name NOT LIKE 'sqlite_%' "
                     "ORDER BY type, name"),
                 errorMessage)) {
        return {};
    }
    while (objectQuery.next()) {
        const QString type = objectQuery.value(0).toString();
        const QString name = objectQuery.value(1).toString();
        QJsonObject object{
            {QStringLiteral("type"), type},
            {QStringLiteral("name"), name},
            {QStringLiteral("table"), objectQuery.value(2).toString()},
            {QStringLiteral("sql"), value(objectQuery.value(3))},
        };
        if (type == QStringLiteral("table")) {
            object.insert(QStringLiteral("columns"),
                          tableInfo(database, name, errorMessage));
            object.insert(QStringLiteral("foreignKeys"),
                          foreignKeys(database, name, errorMessage));
            object.insert(QStringLiteral("indexes"),
                          indexes(database, name, errorMessage));
            if (errorMessage != nullptr && !errorMessage->isEmpty()) {
                return {};
            }
        }
        objects.append(object);
    }
    root.insert(QStringLiteral("objects"), objects);
    return QJsonDocument(root);
}

} // namespace darkeye
