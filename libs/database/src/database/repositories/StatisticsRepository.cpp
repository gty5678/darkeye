#include "database/repositories/StatisticsRepository.h"

#include <QDate>
#include <QHash>
#include <QSqlError>
#include <QSqlQuery>
#include <QMap>
#include <QSet>

namespace
{

std::optional<int> scalarCount(QSqlDatabase database, const QString &sql,
                               QString *errorMessage)
{
    QSqlQuery query(database);
    if (!query.exec(sql) || !query.next())
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = query.lastError().text();
        }
        return std::nullopt;
    }
    return query.value(0).toInt();
}

QString recordTable(darkeye::PersonalRecordKind kind)
{
    switch (kind)
    {
    case darkeye::PersonalRecordKind::Masturbation:
        return QStringLiteral("masturbation");
    case darkeye::PersonalRecordKind::LoveMaking:
        return QStringLiteral("love_making");
    case darkeye::PersonalRecordKind::SexualArousal:
        return QStringLiteral("sexual_arousal");
    }
    return {};
}

QString recordTimeColumn(darkeye::PersonalRecordKind kind)
{
    switch (kind)
    {
    case darkeye::PersonalRecordKind::Masturbation:
        return QStringLiteral("start_time");
    case darkeye::PersonalRecordKind::LoveMaking:
        return QStringLiteral("event_time");
    case darkeye::PersonalRecordKind::SexualArousal:
        return QStringLiteral("arousal_time");
    }
    return {};
}

QHash<qint64, qreal> workWeights(QSqlDatabase database, int scope, QString *errorMessage)
{
    QHash<qint64, qreal> result;
    QSqlQuery query(database);
    const QString sql = scope == 0
                            ? QStringLiteral("SELECT work_id, 1 FROM favorite_work")
                            : QStringLiteral("SELECT work_id, COUNT(*) FROM masturbation "
                                             "WHERE work_id IS NOT NULL GROUP BY work_id");
    if (!query.exec(sql))
    {
        if (errorMessage) *errorMessage = query.lastError().text();
        return result;
    }
    while (query.next()) result.insert(query.value(0).toLongLong(),
                                       scope == 2 ? query.value(1).toDouble() : 1.0);
    return result;
}

QSet<qint64> favoriteActresses(QSqlDatabase database, QString *errorMessage)
{
    QSet<qint64> result;
    QSqlQuery query(database);
    if (!query.exec(QStringLiteral("SELECT actress_id FROM favorite_actress")))
    {
        if (errorMessage) *errorMessage = query.lastError().text();
        return result;
    }
    while (query.next()) result.insert(query.value(0).toLongLong());
    return result;
}

} // namespace

namespace darkeye
{

StatisticsRepository::StatisticsRepository(QSqlDatabase publicDatabase,
                                           QSqlDatabase privateDatabase)
    : m_publicDatabase(std::move(publicDatabase)), m_privateDatabase(std::move(privateDatabase))
{
}

std::optional<DashboardStatistics> StatisticsRepository::dashboard(QString *errorMessage) const
{
    DashboardStatistics statistics;
    const auto queryPublic = [this, errorMessage](const QString &sql)
    {
        return scalarCount(m_publicDatabase, sql, errorMessage);
    };
    const auto queryPrivate = [this, errorMessage](const QString &sql)
    {
        return scalarCount(m_privateDatabase, sql, errorMessage);
    };

    const auto works = queryPublic(QStringLiteral("SELECT COUNT(*) FROM work WHERE IFNULL(is_deleted, 0)=0"));
    const auto actresses = queryPublic(QStringLiteral("SELECT COUNT(*) FROM actress"));
    const auto actors = queryPublic(QStringLiteral("SELECT COUNT(*) FROM actor"));
    const auto tags =
        queryPublic(QStringLiteral("SELECT COUNT(*) FROM tag WHERE redirect_tag_id IS NULL"));
    const auto recent = queryPublic(QStringLiteral(
        "SELECT COUNT(*) FROM work WHERE IFNULL(is_deleted, 0)=0 AND create_time >= date('now', '-30 day')"));
    const auto favoriteWorks = queryPrivate(QStringLiteral("SELECT COUNT(*) FROM favorite_work"));
    const auto favoriteActresses =
        queryPrivate(QStringLiteral("SELECT COUNT(*) FROM favorite_actress"));
    if (!works || !actresses || !actors || !tags || !recent || !favoriteWorks ||
        !favoriteActresses)
    {
        return std::nullopt;
    }

    statistics.workCount = *works;
    statistics.actressCount = *actresses;
    statistics.actorCount = *actors;
    statistics.tagCount = *tags;
    statistics.recentThirtyDays = *recent;
    statistics.favoriteWorkCount = *favoriteWorks;
    statistics.favoriteActressCount = *favoriteActresses;
    return statistics;
}

std::optional<TopActressStatistic> StatisticsRepository::topActress(
    int days, QString *errorMessage) const
{
    QSqlQuery recordsQuery(m_privateDatabase);
    recordsQuery.prepare(QStringLiteral(
        "SELECT work_id, MAX(start_time), COUNT(work_id) FROM masturbation "
        "WHERE work_id IS NOT NULL AND start_time >= DATE('now', printf('-%d day', ?)) "
        "GROUP BY work_id"));
    recordsQuery.addBindValue(qMax(0, days));
    if (!recordsQuery.exec())
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = recordsQuery.lastError().text();
        }
        return std::nullopt;
    }
    QHash<qint64, QPair<int, QString>> recordsByWork;
    while (recordsQuery.next())
    {
        recordsByWork.insert(recordsQuery.value(0).toLongLong(),
                             {recordsQuery.value(2).toInt(), recordsQuery.value(1).toString()});
    }
    if (recordsByWork.isEmpty())
    {
        return std::nullopt;
    }

    QSqlQuery query(m_publicDatabase);
    QStringList placeholders;
    placeholders.fill(QStringLiteral("?"), recordsByWork.size());
    query.prepare(QStringLiteral(
                      "SELECT a.actress_id, "
                      "(SELECT cn FROM actress_name WHERE actress_id=a.actress_id AND name_type=1), "
                      "a.image_urlA, relation.work_id FROM actress a "
                      "JOIN work_actress_relation relation ON relation.actress_id=a.actress_id "
                      "WHERE relation.work_id IN (%1)")
                      .arg(placeholders.join(QLatin1Char(','))));
    for (auto iterator = recordsByWork.cbegin(); iterator != recordsByWork.cend(); ++iterator)
    {
        query.addBindValue(iterator.key());
    }
    if (!query.exec())
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = query.lastError().text();
        }
        return std::nullopt;
    }

    QHash<qint64, TopActressStatistic> byActress;
    while (query.next())
    {
        const qint64 actressId = query.value(0).toLongLong();
        const auto record = recordsByWork.value(query.value(3).toLongLong());
        TopActressStatistic &statistic = byActress[actressId];
        statistic.actressId = actressId;
        statistic.name = query.value(1).toString();
        statistic.imagePath = query.value(2).toString();
        statistic.recordCount += record.first;
        if (record.second > statistic.latestRecordTime)
        {
            statistic.latestRecordTime = record.second;
        }
    }

    std::optional<TopActressStatistic> top;
    for (const TopActressStatistic &statistic : byActress)
    {
        if (!top.has_value() || statistic.recordCount > top->recordCount ||
            (statistic.recordCount == top->recordCount &&
             statistic.latestRecordTime > top->latestRecordTime))
        {
            top = statistic;
        }
    }
    return top;
}

int StatisticsRepository::recordCountInDays(int days, PersonalRecordKind kind,
                                             QString *errorMessage) const
{
    QSqlQuery query(m_privateDatabase);
    query.prepare(QStringLiteral("SELECT COUNT(*) FROM %1 WHERE %2 >= "
                                 "DATE('now', printf('-%d day', ?))")
                      .arg(recordTable(kind), recordTimeColumn(kind)));
    query.addBindValue(qMax(0, days));
    if (!query.exec() || !query.next())
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = query.lastError().text();
        }
        return 0;
    }
    return query.value(0).toInt();
}

int StatisticsRepository::earliestRecordYear(QString *errorMessage) const
{
    QSqlQuery query(m_privateDatabase);
    if (!query.exec(QStringLiteral(
            "SELECT MIN(record_time) FROM ("
            "SELECT start_time AS record_time FROM masturbation UNION ALL "
            "SELECT event_time FROM love_making UNION ALL "
            "SELECT arousal_time FROM sexual_arousal)")) ||
        !query.next())
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = query.lastError().text();
        }
        return QDate::currentDate().year();
    }
    const int year = query.value(0).toString().left(4).toInt();
    return year > 0 ? year : QDate::currentDate().year();
}

int StatisticsRepository::favoriteUnwatchedSalesCycle(QString *errorMessage) const
{
    const auto unwatched = scalarCount(
        m_privateDatabase,
        QStringLiteral("SELECT COUNT(*) FROM favorite_work favorite WHERE NOT EXISTS ("
                       "SELECT 1 FROM masturbation watched WHERE watched.work_id=favorite.work_id)"),
        errorMessage);
    if (!unwatched.has_value())
    {
        return 0;
    }
    const int recentRecords =
        recordCountInDays(90, PersonalRecordKind::Masturbation, errorMessage);
    return recentRecords == 0 ? 114514 : *unwatched * 90 / recentRecords;
}

QVector<WeightedChartValue> StatisticsRepository::workActressAges(int scope,
                                                                    QString *errorMessage) const
{
    const QHash<qint64, qreal> weights = scope < 0 ? QHash<qint64, qreal>()
                                                    : workWeights(m_privateDatabase, scope, errorMessage);
    QVector<WeightedChartValue> result;
    QSqlQuery query(m_publicDatabase);
    if (!query.exec(QStringLiteral("SELECT age.work_id, age.avg_age FROM v_work_avg_age_info age JOIN work w ON w.work_id=age.work_id "
                                   "WHERE IFNULL(w.is_deleted, 0)=0 AND avg_age IS NOT NULL")))
    { if (errorMessage) *errorMessage = query.lastError().text(); return result; }
    while (query.next())
    {
        const qint64 id = query.value(0).toLongLong();
        if (scope >= 0 && !weights.contains(id)) continue;
        result.append({query.value(1).toDouble(), scope < 0 ? 1.0 : weights.value(id)});
    }
    return result;
}

QVector<WeightedChartValue> StatisticsRepository::actressHeights(int scope,
                                                                   QString *errorMessage) const
{
    QHash<qint64, qreal> weights;
    QSet<qint64> favorite;
    if (scope == 0) favorite = favoriteActresses(m_privateDatabase, errorMessage);
    if (scope >= 1) weights = workWeights(m_privateDatabase, scope, errorMessage);
    QHash<qint64, qreal> actressWeights;
    if (scope >= 1)
    {
        QSqlQuery relations(m_publicDatabase);
        if (!relations.exec(QStringLiteral("SELECT work_id, actress_id FROM work_actress_relation")))
        { if (errorMessage) *errorMessage = relations.lastError().text(); return {}; }
        while (relations.next())
            if (weights.contains(relations.value(0).toLongLong()))
                actressWeights[relations.value(1).toLongLong()] += weights.value(relations.value(0).toLongLong());
    }
    QVector<WeightedChartValue> result;
    QSqlQuery query(m_publicDatabase);
    if (!query.exec(QStringLiteral("SELECT actress_id, height FROM actress WHERE height IS NOT NULL "
                                   "AND height != '' AND height != 0")))
    { if (errorMessage) *errorMessage = query.lastError().text(); return result; }
    while (query.next()) {
        const qint64 id = query.value(0).toLongLong();
        if (scope == 0 && !favorite.contains(id)) continue;
        if (scope >= 1 && !actressWeights.contains(id)) continue;
        result.append({query.value(1).toDouble(), scope >= 1 ? actressWeights.value(id) : 1.0});
    }
    return result;
}

QVector<WeightedChartValue> StatisticsRepository::actressDebutAges(QString *errorMessage) const
{
    QVector<WeightedChartValue> result;
    QSqlQuery query(m_publicDatabase);
    if (!query.exec(QStringLiteral("SELECT (julianday(debut_date)-julianday(birthday))/365.25-0.5 "
                                   "FROM actress WHERE birthday IS NOT NULL AND debut_date IS NOT NULL "
                                   "AND birthday != '' AND debut_date != ''")))
    { if (errorMessage) *errorMessage = query.lastError().text(); return result; }
    while (query.next()) result.append({query.value(0).toDouble(), 1.0});
    return result;
}

QVector<ChartValue> StatisticsRepository::workReleaseYears(int scope, QString *errorMessage) const
{
    const QHash<qint64, qreal> weights = scope < 0 ? QHash<qint64, qreal>() : workWeights(m_privateDatabase, scope, errorMessage);
    QMap<QString, qreal> counts;
    QSqlQuery query(m_publicDatabase);
    if (!query.exec(QStringLiteral("SELECT work_id, SUBSTR(release_date, 1, 4) FROM work "
                                   "WHERE IFNULL(is_deleted, 0)=0 AND release_date IS NOT NULL AND release_date != ''")))
    { if (errorMessage) *errorMessage = query.lastError().text(); return {}; }
    while (query.next()) { const qint64 id=query.value(0).toLongLong(); if (scope<0 || weights.contains(id)) counts[query.value(1).toString()] += scope==2 ? weights.value(id) : 1.0; }
    if (counts.isEmpty()) return {};
    const int firstYear = counts.firstKey().toInt();
    const int lastYear = counts.lastKey().toInt();
    QVector<ChartValue> result;
    for (int year = firstYear; year <= lastYear; ++year)
        result.append({QString::number(year), counts.value(QString::number(year))});
    return result;
}

QVector<ChartValue> StatisticsRepository::actressDebutYears(int scope, QString *errorMessage) const
{
    QSet<qint64> included;
    QHash<qint64, qreal> weights;
    if (scope == 0) included = favoriteActresses(m_privateDatabase, errorMessage);
    if (scope >= 1) {
        const auto work = workWeights(m_privateDatabase, scope, errorMessage);
        QSqlQuery relation(m_publicDatabase);
        if (!relation.exec(QStringLiteral("SELECT work_id, actress_id FROM work_actress_relation"))) { if(errorMessage) *errorMessage=relation.lastError().text(); return {}; }
        while (relation.next()) if (work.contains(relation.value(0).toLongLong())) weights[relation.value(1).toLongLong()] += work.value(relation.value(0).toLongLong());
    }
    QMap<QString,qreal> counts; QSqlQuery query(m_publicDatabase);
    if (!query.exec(QStringLiteral("SELECT actress_id, SUBSTR(debut_date,1,4) FROM actress WHERE debut_date IS NOT NULL AND debut_date != ''"))) { if(errorMessage)*errorMessage=query.lastError().text(); return {}; }
    while(query.next()) { const auto id=query.value(0).toLongLong(); if(scope==0&&!included.contains(id))continue; if(scope>=1&&!weights.contains(id))continue; counts[query.value(1).toString()]+=scope==2?weights.value(id):1.0; }
    if (counts.isEmpty()) return {};
    const int firstYear = counts.firstKey().toInt();
    const int lastYear = counts.lastKey().toInt();
    QVector<ChartValue> result;
    for (int year = firstYear; year <= lastYear; ++year)
        result.append({QString::number(year), counts.value(QString::number(year))});
    return result;
}

QVector<ChartValue> StatisticsRepository::cupDistribution(int scope, QString *errorMessage) const
{
    QMap<QString,qreal> counts;
    QSet<qint64> favorite; QHash<qint64,qreal> weights; if(scope==0) favorite=favoriteActresses(m_privateDatabase,errorMessage);
    if(scope>=1) { const auto work=workWeights(m_privateDatabase,scope,errorMessage); QSqlQuery r(m_publicDatabase); r.exec(QStringLiteral("SELECT work_id,actress_id FROM work_actress_relation")); while(r.next()) if(work.contains(r.value(0).toLongLong())) weights[r.value(1).toLongLong()]+=work.value(r.value(0).toLongLong()); }
    QSqlQuery query(m_publicDatabase); if(!query.exec(QStringLiteral("SELECT actress_id,cup FROM actress WHERE cup IS NOT NULL AND cup != ''"))) {if(errorMessage)*errorMessage=query.lastError().text();return{};}
    while(query.next()){const auto id=query.value(0).toLongLong();if(scope==0&&!favorite.contains(id))continue;if(scope>=1&&!weights.contains(id))continue;counts[query.value(1).toString()]+=scope==2?weights.value(id):1.0;}
    QVector<ChartValue> result;for(auto it=counts.cbegin();it!=counts.cend();++it)result.append({it.key(),it.value()});return result;
}

QVector<WaistHipChartValue> StatisticsRepository::waistHipDistribution(
    int scope, QString *errorMessage) const
{
    QHash<qint64, qreal> work;
    QSet<qint64> favorite;
    if (scope == 0) favorite = favoriteActresses(m_privateDatabase, errorMessage);
    if (scope >= 1) work = workWeights(m_privateDatabase, scope, errorMessage);
    QHash<qint64, qreal> actressWeights;
    if (scope >= 1) {
        QSqlQuery relation(m_publicDatabase);
        if (!relation.exec(QStringLiteral("SELECT work_id, actress_id FROM work_actress_relation"))) { if(errorMessage)*errorMessage=relation.lastError().text(); return {}; }
        while (relation.next()) if (work.contains(relation.value(0).toLongLong())) actressWeights[relation.value(1).toLongLong()] += work.value(relation.value(0).toLongLong());
    }
    QMap<QPair<int,int>, qreal> counts;
    QSqlQuery query(m_publicDatabase);
    if (!query.exec(QStringLiteral("SELECT actress_id, waist, hip FROM actress WHERE waist IS NOT NULL AND hip IS NOT NULL AND waist != 0 AND hip != 0"))) { if(errorMessage)*errorMessage=query.lastError().text(); return {}; }
    while (query.next()) { const auto id=query.value(0).toLongLong(); if(scope==0&&!favorite.contains(id))continue; if(scope>=1&&!actressWeights.contains(id))continue; counts[{query.value(1).toInt(),query.value(2).toInt()}] += scope==2?actressWeights.value(id):1.0; }
    QVector<WaistHipChartValue> result;
    for(auto it=counts.cbegin();it!=counts.cend();++it) result.append({static_cast<qreal>(it.key().first),static_cast<qreal>(it.key().second),it.value(),it.key().first/static_cast<qreal>(it.key().second)});
    return result;
}

QVector<ChartValue> StatisticsRepository::topDirectors(int scope, QString *errorMessage) const
{
    const auto weights=scope<0?QHash<qint64,qreal>():workWeights(m_privateDatabase,scope,errorMessage); QMap<QString,qreal> counts; QSqlQuery query(m_publicDatabase);
    if(!query.exec(QStringLiteral("SELECT work_id,director FROM work WHERE IFNULL(is_deleted, 0)=0 AND director IS NOT NULL AND director != '' AND director != '----'"))){if(errorMessage)*errorMessage=query.lastError().text();return{};}
    while(query.next()){const auto id=query.value(0).toLongLong();if(scope>=0&&!weights.contains(id))continue;counts[query.value(1).toString()]+=scope==2?weights.value(id):1.0;}
    QVector<ChartValue> result;for(auto it=counts.cbegin();it!=counts.cend();++it)result.append({it.key(),it.value()});std::sort(result.begin(),result.end(),[](const auto&a,const auto&b){return a.value>b.value;});return result.mid(0,20);
}

QVector<ChartValue> StatisticsRepository::topMakers(int scope, QString *errorMessage) const
{
    const auto weights=scope<0?QHash<qint64,qreal>():workWeights(m_privateDatabase,scope,errorMessage); QMap<QString,qreal> counts; QSqlQuery query(m_publicDatabase);
    if(!query.exec(QStringLiteral("SELECT w.work_id,m.cn_name FROM work w JOIN maker m ON m.maker_id=w.maker_id WHERE IFNULL(w.is_deleted, 0)=0 AND m.cn_name IS NOT NULL"))){if(errorMessage)*errorMessage=query.lastError().text();return{};}
    while(query.next()){const auto id=query.value(0).toLongLong();if(scope>=0&&!weights.contains(id))continue;counts[query.value(1).toString()]+=scope==2?weights.value(id):1.0;}
    QVector<ChartValue> result;for(auto it=counts.cbegin();it!=counts.cend();++it)result.append({it.key(),it.value()});std::sort(result.begin(),result.end(),[](const auto&a,const auto&b){return a.value>b.value;});return result.mid(0,20);
}

QVector<ChartValue> StatisticsRepository::mostRecordedActresses(QString *errorMessage) const
{
    QHash<qint64,qreal> weights=workWeights(m_privateDatabase,2,errorMessage); QHash<qint64,qreal> counts; QHash<qint64,QString> names; QSqlQuery query(m_publicDatabase);
    if(!query.exec(QStringLiteral("SELECT r.work_id,a.actress_id,COALESCE((SELECT cn FROM actress_name WHERE actress_id=a.actress_id AND name_type=1),(SELECT jp FROM actress_name WHERE actress_id=a.actress_id AND name_type=1),'未知女优') FROM work_actress_relation r JOIN actress a ON a.actress_id=r.actress_id"))){if(errorMessage)*errorMessage=query.lastError().text();return{};}
    while(query.next()){const auto workId=query.value(0).toLongLong();if(!weights.contains(workId))continue;const auto actress=query.value(1).toLongLong();counts[actress]+=weights.value(workId);names[actress]=query.value(2).toString();}
    QVector<ChartValue> result;for(auto it=counts.cbegin();it!=counts.cend();++it)result.append({names.value(it.key()),it.value()});std::sort(result.begin(),result.end(),[](const auto&a,const auto&b){return a.value>b.value;});return result.mid(0,25);
}

QVector<QDate> StatisticsRepository::workAddedDates(QString *errorMessage) const
{
    QVector<QDate> result; QSqlQuery query(m_publicDatabase);
    if(!query.exec(QStringLiteral("SELECT create_time FROM work WHERE IFNULL(is_deleted, 0)=0 AND create_time IS NOT NULL AND create_time != ''"))){if(errorMessage)*errorMessage=query.lastError().text();return result;}
    while(query.next()){const auto date=QDate::fromString(query.value(0).toString().left(10),Qt::ISODate);if(date.isValid())result.append(date);} return result;
}

} // namespace darkeye
