#include "graph/GraphRepository.h"

#include <QHash>
#include <QRegularExpression>
#include <QSet>
#include <QSqlError>
#include <QSqlQuery>

#include <algorithm>
#include <utility>

namespace darkeye::graph {
namespace {

bool execute(QSqlQuery &query, const QString &statement, QString *errorMessage)
{
    if (query.exec(statement)) {
        return true;
    }
    if (errorMessage != nullptr) {
        *errorMessage = query.lastError().text();
    }
    return false;
}

QString workNodeId(qint64 workId)
{
    return QStringLiteral("w%1").arg(workId);
}

QString actressNodeId(qint64 actressId)
{
    return QStringLiteral("a%1").arg(actressId);
}

} // namespace

GraphRepository::GraphRepository(QSqlDatabase publicDatabase, QSqlDatabase privateDatabase)
    : m_publicDatabase(std::move(publicDatabase)), m_privateDatabase(std::move(privateDatabase))
{
}

bool GraphRepository::load(GraphStore &store, QString *errorMessage) const
{
    if (!m_publicDatabase.isOpen()) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("公共数据库尚未打开。");
        }
        return false;
    }

    QVector<GraphNode> nodes;
    QVector<GraphEdge> edges;
    QSet<QString> edgePairs;
    const auto appendEdge = [&edges, &edgePairs](GraphEdge edge) {
        const auto [first, second] = std::minmax(edge.sourceId, edge.targetId);
        const QString key = first + QChar(0x001f) + second;
        if (!edgePairs.contains(key)) {
            edgePairs.insert(key);
            edges.append(std::move(edge));
        }
    };
    QHash<QString, QString> workNodeBySerial;
    QHash<qint64, QVector<QString>> worksBySeries;

    QSqlQuery actressQuery(m_publicDatabase);
    if (!execute(actressQuery, QStringLiteral(
            "SELECT a.actress_id, COALESCE("
            "(SELECT n.cn FROM actress_name n WHERE n.actress_id=a.actress_id "
            " AND n.cn IS NOT NULL AND n.cn<>'' LIMIT 1), "
            "(SELECT n.jp FROM actress_name n WHERE n.actress_id=a.actress_id "
            " AND n.jp IS NOT NULL AND n.jp<>'' LIMIT 1), '') "
            "FROM actress a"), errorMessage)) {
        return false;
    }
    while (actressQuery.next()) {
        const qint64 actressId = actressQuery.value(0).toLongLong();
        const QString id = actressNodeId(actressId);
        nodes.append({id, actressQuery.value(1).toString().trimmed().isEmpty()
                               ? id : actressQuery.value(1).toString(),
                      QStringLiteral("actress")});
    }

    QSqlQuery workQuery(m_publicDatabase);
    if (!execute(workQuery, QStringLiteral(
            "SELECT work_id, serial_number, notes, series_id FROM work "
            "WHERE IFNULL(is_deleted, 0)=0"), errorMessage)) {
        return false;
    }
    struct WorkNote { QString sourceId; QString notes; };
    QVector<WorkNote> notes;
    while (workQuery.next()) {
        const qint64 workId = workQuery.value(0).toLongLong();
        const QString serial = workQuery.value(1).toString();
        const QString id = workNodeId(workId);
        nodes.append({id, serial.isEmpty() ? id : serial, QStringLiteral("work")});
        if (!serial.isEmpty()) {
            workNodeBySerial.insert(serial, id);
        }
        notes.append({id, workQuery.value(2).toString()});
        bool seriesOk = false;
        const qint64 seriesId = workQuery.value(3).toLongLong(&seriesOk);
        if (seriesOk && seriesId > 1) {
            worksBySeries[seriesId].append(id);
        }
    }

    QSqlQuery relationQuery(m_publicDatabase);
    if (!execute(relationQuery, QStringLiteral(
            "SELECT relation.work_id, relation.actress_id "
            "FROM work_actress_relation relation "
            "JOIN work ON work.work_id=relation.work_id "
            "WHERE IFNULL(work.is_deleted, 0)=0"), errorMessage)) {
        return false;
    }
    while (relationQuery.next()) {
        appendEdge({workNodeId(relationQuery.value(0).toLongLong()),
                    actressNodeId(relationQuery.value(1).toLongLong()),
                    QStringLiteral("cast")});
    }

    static const QRegularExpression wikilink(
        QStringLiteral(R"(\[\[([^\]|]+)(?:\|[^\]]*)?\]\])"));
    for (const WorkNote &work : notes) {
        auto links = wikilink.globalMatch(work.notes);
        while (links.hasNext()) {
            const QString targetSerial = links.next().captured(1).trimmed();
            const auto target = workNodeBySerial.constFind(targetSerial);
            if (target != workNodeBySerial.cend() && *target != work.sourceId) {
                appendEdge({work.sourceId, *target, QStringLiteral("reference")});
            }
        }
    }

    for (const QVector<QString> &seriesWorks : worksBySeries) {
        for (int first = 0; first < seriesWorks.size(); ++first) {
            for (int second = first + 1; second < seriesWorks.size(); ++second) {
                appendEdge({seriesWorks[first], seriesWorks[second],
                            QStringLiteral("series")});
            }
        }
    }

    store.replace(nodes, edges);
    return true;
}

bool GraphRepository::syncWork(GraphStore &store, qint64 workId, QString *errorMessage) const
{
    if (!m_publicDatabase.isOpen()) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("公共数据库尚未打开。");
        }
        return false;
    }

    const QString sourceId = workNodeId(workId);
    QSqlQuery workQuery(m_publicDatabase);
    workQuery.prepare(QStringLiteral(
        "SELECT serial_number, notes, series_id, IFNULL(is_deleted, 0) "
        "FROM work WHERE work_id=?"));
    workQuery.addBindValue(workId);
    if (!workQuery.exec()) {
        if (errorMessage != nullptr) {
            *errorMessage = workQuery.lastError().text();
        }
        return false;
    }
    if (!workQuery.next() || workQuery.value(3).toInt() != 0) {
        store.removeNode(sourceId);
        return true;
    }

    const QString serial = workQuery.value(0).toString();
    const QString notes = workQuery.value(1).toString();
    bool seriesOk = false;
    const qint64 seriesId = workQuery.value(2).toLongLong(&seriesOk);
    store.upsertNode({sourceId, serial.isEmpty() ? sourceId : serial, QStringLiteral("work")});
    const auto hasEdge = [&store](const QString &first, const QString &second) {
        for (const GraphEdge &edge : store.edges()) {
            if ((edge.sourceId == first && edge.targetId == second)
                || (edge.sourceId == second && edge.targetId == first)) {
                return true;
            }
        }
        return false;
    };

    // Reconcile only the cast edges incident to this work; references and series links
    // must remain in place until their dedicated passes below.
    for (const GraphEdge &edge : store.edges()) {
        const QString otherId = edge.sourceId == sourceId ? edge.targetId
                                                           : edge.sourceId;
        if ((edge.sourceId == sourceId || edge.targetId == sourceId)
            && (edge.type == QStringLiteral("cast") || otherId.startsWith(QLatin1Char('a')))) {
            store.removeEdge(edge.sourceId, edge.targetId);
        }
    }

    QSqlQuery castQuery(m_publicDatabase);
    castQuery.prepare(QStringLiteral(
        "SELECT a.actress_id, COALESCE("
        "(SELECT n.cn FROM actress_name n WHERE n.actress_id=a.actress_id "
        " AND n.cn IS NOT NULL AND n.cn<>'' LIMIT 1), "
        "(SELECT n.jp FROM actress_name n WHERE n.actress_id=a.actress_id "
        " AND n.jp IS NOT NULL AND n.jp<>'' LIMIT 1), '') "
        "FROM work_actress_relation relation "
        "JOIN actress a ON a.actress_id=relation.actress_id "
        "WHERE relation.work_id=?"));
    castQuery.addBindValue(workId);
    if (!castQuery.exec()) {
        if (errorMessage != nullptr) {
            *errorMessage = castQuery.lastError().text();
        }
        return false;
    }
    while (castQuery.next()) {
        const QString actressId = actressNodeId(castQuery.value(0).toLongLong());
        const QString name = castQuery.value(1).toString();
        store.upsertNode({actressId, name.trimmed().isEmpty() ? actressId : name,
                          QStringLiteral("actress")});
        store.upsertEdge({sourceId, actressId, QStringLiteral("cast")});
    }

    for (const GraphEdge &edge : store.edges()) {
        if ((edge.sourceId == sourceId || edge.targetId == sourceId)
            && edge.type == QStringLiteral("reference")) {
            store.removeEdge(edge.sourceId, edge.targetId);
        }
    }
    static const QRegularExpression wikilink(
        QStringLiteral(R"(\[\[([^\]|]+)(?:\|[^\]]*)?\]\])"));
    auto links = wikilink.globalMatch(notes);
    while (links.hasNext()) {
        const QString targetSerial = links.next().captured(1).trimmed();
        QSqlQuery targetQuery(m_publicDatabase);
        targetQuery.prepare(QStringLiteral(
            "SELECT work_id, serial_number FROM work "
            "WHERE serial_number=? AND IFNULL(is_deleted, 0)=0 LIMIT 1"));
        targetQuery.addBindValue(targetSerial);
        if (!targetQuery.exec()) {
            if (errorMessage != nullptr) {
                *errorMessage = targetQuery.lastError().text();
            }
            return false;
        }
        if (!targetQuery.next()) {
            continue;
        }
        const QString targetId = workNodeId(targetQuery.value(0).toLongLong());
        if (targetId == sourceId) {
            continue;
        }
        const QString targetLabel = targetQuery.value(1).toString();
        store.upsertNode({targetId, targetLabel.isEmpty() ? targetId : targetLabel,
                          QStringLiteral("work")});
        if (!hasEdge(sourceId, targetId)) {
            store.upsertEdge({sourceId, targetId, QStringLiteral("reference")});
        }
    }

    // The work may have left a series.  Remove its old series edges first, then
    // rebuild its current series clique from the database's authoritative state.
    for (const GraphEdge &edge : store.edges()) {
        if ((edge.sourceId == sourceId || edge.targetId == sourceId)
            && edge.type == QStringLiteral("series")) {
            store.removeEdge(edge.sourceId, edge.targetId);
        }
    }
    if (!seriesOk || seriesId <= 1) {
        return true;
    }

    QSqlQuery seriesQuery(m_publicDatabase);
    seriesQuery.prepare(QStringLiteral(
        "SELECT work_id, serial_number FROM work "
        "WHERE series_id=? AND IFNULL(is_deleted, 0)=0 ORDER BY work_id"));
    seriesQuery.addBindValue(seriesId);
    if (!seriesQuery.exec()) {
        if (errorMessage != nullptr) {
            *errorMessage = seriesQuery.lastError().text();
        }
        return false;
    }
    QVector<GraphNode> seriesNodes;
    while (seriesQuery.next()) {
        const QString id = workNodeId(seriesQuery.value(0).toLongLong());
        const QString label = seriesQuery.value(1).toString();
        seriesNodes.append({id, label.isEmpty() ? id : label, QStringLiteral("work")});
    }
    QSet<QString> seriesNodeIds;
    for (const GraphNode &node : seriesNodes) {
        seriesNodeIds.insert(node.id);
        store.upsertNode(node);
    }
    for (const GraphEdge &edge : store.edges()) {
        if (edge.type == QStringLiteral("series") && seriesNodeIds.contains(edge.sourceId)
            && seriesNodeIds.contains(edge.targetId)) {
            store.removeEdge(edge.sourceId, edge.targetId);
        }
    }
    for (int first = 0; first < seriesNodes.size(); ++first) {
        for (int second = first + 1; second < seriesNodes.size(); ++second) {
            if (!hasEdge(seriesNodes[first].id, seriesNodes[second].id)) {
                store.upsertEdge({seriesNodes[first].id, seriesNodes[second].id,
                                  QStringLiteral("series")});
            }
        }
    }
    return true;
}

QSet<QString> GraphRepository::favoriteWorkNodeIds(QString *errorMessage) const
{
    QSet<QString> result;
    if (!m_privateDatabase.isOpen()) {
        return result;
    }
    QSqlQuery query(m_privateDatabase);
    if (!execute(query, QStringLiteral("SELECT work_id FROM favorite_work "
                                      "WHERE work_id IS NOT NULL"), errorMessage)) {
        return {};
    }
    while (query.next()) {
        result.insert(workNodeId(query.value(0).toLongLong()));
    }
    return result;
}

QString GraphRepository::imagePathForNode(const QString &nodeId) const
{
    if (!m_publicDatabase.isOpen() || nodeId.size() < 2) return {};

    bool ok = false;
    const qint64 id = nodeId.mid(1).toLongLong(&ok);
    if (!ok || id <= 0) return {};

    QSqlQuery query(m_publicDatabase);
    if (nodeId.startsWith(QLatin1Char('a'))) {
        query.prepare(QStringLiteral("SELECT image_urlA FROM actress WHERE actress_id=?"));
    } else if (nodeId.startsWith(QLatin1Char('w'))) {
        query.prepare(QStringLiteral("SELECT image_url FROM work WHERE work_id=?"));
    } else {
        return {};
    }
    query.addBindValue(id);
    return query.exec() && query.next() ? query.value(0).toString().trimmed() : QString();
}

} // namespace darkeye::graph
