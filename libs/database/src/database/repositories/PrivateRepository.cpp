#include "database/repositories/PrivateRepository.h"

#include <QSqlError>
#include <QSqlQuery>

namespace darkeye
{

PrivateRepository::PrivateRepository(QSqlDatabase database) : m_database(std::move(database)) {}

bool PrivateRepository::isFavoriteWork(qint64 workId, QString *errorMessage) const
{
    return exists(QStringLiteral("favorite_work"), QStringLiteral("work_id"), workId, errorMessage);
}

bool PrivateRepository::isFavoriteActress(qint64 actressId, QString *errorMessage) const
{
    return exists(QStringLiteral("favorite_actress"), QStringLiteral("actress_id"), actressId,
                  errorMessage);
}

bool PrivateRepository::addFavoriteWork(qint64 workId, const QString &serialNumber,
                                        QString *errorMessage)
{
    if (isFavoriteWork(workId, errorMessage))
    {
        return true;
    }
    if (serialNumber.trimmed().isEmpty())
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = QStringLiteral("收藏作品必须保留番号");
        }
        return false;
    }
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral("INSERT INTO favorite_work(work_id, serial_number) VALUES(?, ?)"));
    query.addBindValue(workId);
    query.addBindValue(serialNumber.trimmed());
    if (!query.exec())
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = query.lastError().text();
        }
        return false;
    }
    return true;
}

bool PrivateRepository::addFavoriteActress(qint64 actressId, const QString &japaneseName,
                                           QString *errorMessage)
{
    if (isFavoriteActress(actressId, errorMessage))
    {
        return true;
    }
    if (japaneseName.trimmed().isEmpty())
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = QStringLiteral("收藏女演员必须保留日文名");
        }
        return false;
    }
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral("INSERT INTO favorite_actress(actress_id, jp_name) VALUES(?, ?)"));
    query.addBindValue(actressId);
    query.addBindValue(japaneseName.trimmed());
    if (!query.exec())
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = query.lastError().text();
        }
        return false;
    }
    return true;
}

bool PrivateRepository::removeFavoriteWork(qint64 workId, QString *errorMessage)
{
    return remove(QStringLiteral("favorite_work"), QStringLiteral("work_id"), workId, errorMessage);
}

bool PrivateRepository::removeFavoriteActress(qint64 actressId, QString *errorMessage)
{
    return remove(QStringLiteral("favorite_actress"), QStringLiteral("actress_id"), actressId,
                  errorMessage);
}

bool PrivateRepository::addMasturbationRecord(qint64 workId, const QString &serialNumber,
                                              const QString &startTime, const QString &toolName,
                                              int rating, const QString &comment,
                                              QString *errorMessage)
{
    if (rating < 1 || rating > 5)
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = QStringLiteral("评分必须在 1 到 5 之间");
        }
        return false;
    }
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral("INSERT INTO masturbation(work_id, serial_number, "
                                 "start_time, tool_name, rating, comment) "
                                 "VALUES(?, ?, ?, ?, ?, ?)"));
    query.addBindValue(workId > 0 ? QVariant(workId) : QVariant());
    query.addBindValue(serialNumber);
    query.addBindValue(startTime);
    query.addBindValue(toolName);
    query.addBindValue(rating);
    query.addBindValue(comment);
    if (!query.exec())
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = query.lastError().text();
        }
        return false;
    }
    return true;
}

bool PrivateRepository::addLoveMakingRecord(const QString &eventTime, int rating,
                                             const QString &comment, QString *errorMessage)
{
    if (rating < 1 || rating > 5)
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = QStringLiteral("评分必须在 1 到 5 之间");
        }
        return false;
    }

    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "INSERT INTO love_making(event_time, rating, comment) VALUES(?, ?, ?)"));
    query.addBindValue(eventTime);
    query.addBindValue(rating);
    query.addBindValue(comment);
    if (!query.exec())
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = query.lastError().text();
        }
        return false;
    }
    return true;
}

bool PrivateRepository::addSexualArousalRecord(const QString &arousalTime,
                                                const QString &comment,
                                                QString *errorMessage)
{
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "INSERT INTO sexual_arousal(arousal_time, comment) VALUES(?, ?)"));
    query.addBindValue(arousalTime);
    query.addBindValue(comment);
    if (!query.exec())
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = query.lastError().text();
        }
        return false;
    }
    return true;
}

QStringList PrivateRepository::masturbationToolSuggestions(QString *errorMessage) const
{
    QSqlQuery query(m_database);
    if (!query.exec(QStringLiteral(
            "SELECT tool_name, COUNT(*) AS usage_count FROM masturbation "
            "GROUP BY tool_name ORDER BY usage_count DESC")))
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = query.lastError().text();
        }
        return {};
    }

    QStringList tools;
    while (query.next())
    {
        tools.append(query.value(0).toString());
    }
    return tools;
}

QMap<QDate, int> PrivateRepository::dailyCounts(int year, PersonalRecordKind kind,
                                                QString *errorMessage) const
{
    QString table;
    QString timeColumn;
    switch (kind)
    {
    case PersonalRecordKind::Masturbation:
        table = QStringLiteral("masturbation");
        timeColumn = QStringLiteral("start_time");
        break;
    case PersonalRecordKind::LoveMaking:
        table = QStringLiteral("love_making");
        timeColumn = QStringLiteral("event_time");
        break;
    case PersonalRecordKind::SexualArousal:
        table = QStringLiteral("sexual_arousal");
        timeColumn = QStringLiteral("arousal_time");
        break;
    }

    QSqlQuery query(m_database);
    query.prepare(QStringLiteral("SELECT DATE(%1), COUNT(*) FROM %2 WHERE strftime('%Y', %1)=? "
                                 "GROUP BY DATE(%1) ORDER BY DATE(%1)")
                      .arg(timeColumn, table));
    query.addBindValue(QString::number(year));
    if (!query.exec())
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = query.lastError().text();
        }
        return {};
    }

    QMap<QDate, int> counts;
    while (query.next())
    {
        const QDate date = QDate::fromString(query.value(0).toString(), Qt::ISODate);
        if (date.isValid())
        {
            counts.insert(date, query.value(1).toInt());
        }
    }
    return counts;
}

QList<qint64> PrivateRepository::favoriteWorkIds(QString *errorMessage) const
{
    return queryWorkIds(QStringLiteral("SELECT DISTINCT work_id FROM "
                                       "favorite_work WHERE work_id IS NOT NULL"),
                        errorMessage);
}

QList<qint64> PrivateRepository::favoriteActressIds(QString *errorMessage) const
{
    return queryWorkIds(QStringLiteral("SELECT DISTINCT actress_id FROM favorite_actress WHERE "
                                       "actress_id IS NOT NULL"),
                        errorMessage);
}

QList<qint64> PrivateRepository::masturbationWorkIds(QString *errorMessage) const
{
    return queryWorkIds(QStringLiteral("SELECT DISTINCT work_id FROM "
                                       "masturbation WHERE work_id IS NOT NULL"),
                        errorMessage);
}

QList<qint64> PrivateRepository::favoriteUnwatchedWorkIds(QString *errorMessage) const
{
    return queryWorkIds(
        QStringLiteral("SELECT DISTINCT favorite.work_id FROM favorite_work favorite "
                       "WHERE favorite.work_id IS NOT NULL AND NOT EXISTS ("
                       "SELECT 1 FROM masturbation watched WHERE "
                       "watched.work_id=favorite.work_id)"),
        errorMessage);
}

QList<qint64> PrivateRepository::queryWorkIds(const QString &sql, QString *errorMessage) const
{
    QSqlQuery query(m_database);
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

bool PrivateRepository::exists(const QString &table, const QString &idColumn, qint64 id,
                               QString *errorMessage) const
{
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral("SELECT 1 FROM %1 WHERE %2=? LIMIT 1").arg(table, idColumn));
    query.addBindValue(id);
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

bool PrivateRepository::remove(const QString &table, const QString &idColumn, qint64 id,
                               QString *errorMessage)
{
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral("DELETE FROM %1 WHERE %2=?").arg(table, idColumn));
    query.addBindValue(id);
    if (!query.exec())
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = query.lastError().text();
        }
        return false;
    }
    return true;
}

} // namespace darkeye
