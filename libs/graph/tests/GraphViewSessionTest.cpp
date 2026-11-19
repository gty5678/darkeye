#include "graph/GraphManager.h"
#include "graph/GraphRepository.h"
#include "graph/GraphStore.h"
#include "graph/GraphViewSession.h"

#include <QCoreApplication>
#include <QEventLoop>
#include <QSet>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QThreadPool>
#include <QTimer>

#include <iostream>

using darkeye::graph::GraphDeltaOperation;
using darkeye::graph::GraphEdge;
using darkeye::graph::GraphFilterMode;
using darkeye::graph::GraphNode;
using darkeye::graph::GraphStore;
using darkeye::graph::GraphViewSession;

namespace {

bool hasNode(const darkeye::graph::GraphViewSnapshot &snapshot, const QString &id)
{
    for (const auto &node : snapshot.nodes) {
        if (node.node.id == id) {
            return true;
        }
    }
    return false;
}

QString edgeType(const GraphStore &store, const QString &first, const QString &second)
{
    for (const GraphEdge &edge : store.edges()) {
        if ((edge.sourceId == first && edge.targetId == second)
            || (edge.sourceId == second && edge.targetId == first)) {
            return edge.type;
        }
    }
    return {};
}

bool testFiltersAndInducedEdges()
{
    GraphStore store;
    store.replace({{"a1", "Actor", "actress"},
                   {"w1", "Work 1", "work"},
                   {"w2", "Work 2", "work"},
                   {"w3", "Work 3", "work"}},
                  {{"a1", "w1", "cast"},
                   {"w1", "w2", "reference"},
                   {"w2", "w3", "series"}});

    GraphViewSession session(store);
    session.setFilterMode(GraphFilterMode::Ego);
    session.setEgoCenter("w1");
    session.setEgoRadius(1);
    const auto ego = session.snapshot();
    if (ego.nodes.size() != 3 || ego.edges.size() != 2 || !hasNode(ego, "a1")
        || !hasNode(ego, "w2") || hasNode(ego, "w3")) {
        std::cerr << "ego filtering did not produce the expected induced graph\n";
        return false;
    }

    session.setEgoRadius(0);
    const auto egoCenterOnly = session.snapshot();
    if (egoCenterOnly.nodes.size() != 1 || egoCenterOnly.edges.size() != 0
        || !hasNode(egoCenterOnly, "w1")) {
        std::cerr << "zero-radius ego filtering must retain only its center node\n";
        return false;
    }

    session.setFilterMode(GraphFilterMode::FavoriteWorks);
    session.setFavoriteWorkIds(QSet<QString>{"w1"});
    const auto favorite = session.snapshot();
    if (favorite.nodes.size() != 2 || favorite.edges.size() != 1
        || !hasNode(favorite, "a1") || hasNode(favorite, "w2")) {
        std::cerr << "favorite filtering did not retain only favorite works and actresses\n";
        return false;
    }
    return true;
}

bool testIncrementalDelta()
{
    GraphStore store;
    store.replace({{"w1", "Work 1", "work"},
                   {"a1", "Actor", "actress"},
                   {"w2", "Work 2", "work"}},
                  {});
    GraphViewSession session(store);
    (void)session.reload();

    (void)store.upsertEdge({"w1", "a1", "cast"});
    const auto delta = session.refresh();
    bool sawEdgeAdd = false;
    bool sawNodeUpdate = false;
    for (const auto &operation : delta) {
        sawEdgeAdd |= operation.type == GraphDeltaOperation::Type::AddEdge;
        sawNodeUpdate |= operation.type == GraphDeltaOperation::Type::UpdateNode;
    }
    if (!sawEdgeAdd || !sawNodeUpdate) {
        std::cerr << "topology changes must emit an edge and affected radius updates\n";
        return false;
    }
    return true;
}

bool testEdgeTypeUpdateDelta()
{
    GraphStore store;
    store.replace({{"w1", "Work 1", "work"}, {"w2", "Work 2", "work"}},
                  {{"w1", "w2", "series"}});
    GraphViewSession session(store);
    (void)session.reload();
    if (!store.upsertEdge({"w1", "w2", "reference"})) {
        std::cerr << "edge type mutation was rejected\n";
        return false;
    }
    const auto delta = session.refresh();
    if (delta.size() != 1 || delta.front().type != GraphDeltaOperation::Type::UpdateEdge
        || delta.front().edge.type != QStringLiteral("reference")) {
        std::cerr << "edge type mutation must emit exactly one update-edge delta\n";
        return false;
    }
    return true;
}

bool testInteractiveGraphEditsEmitUsableDeltas()
{
    darkeye::graph::GraphManager manager({}, {});
    int changes = 0;
    QObject::connect(&manager, &darkeye::graph::GraphManager::graphChanged,
                     [&changes] { ++changes; });
    GraphViewSession session(manager.store());
    (void)session.reload();

    if (!manager.upsertRuntimeNode({"custom-1", "Custom", "custom"})
        || !manager.upsertRuntimeNode({"custom-2", "Other", "custom"})
        || !manager.upsertRuntimeEdge({"custom-1", "custom-2", "custom"})) {
        std::cerr << "interactive graph edits were rejected\n";
        return false;
    }
    const auto addDelta = session.refresh();
    bool sawNode = false;
    bool sawEdge = false;
    for (const auto &operation : addDelta) {
        sawNode |= operation.type == GraphDeltaOperation::Type::AddNode;
        sawEdge |= operation.type == GraphDeltaOperation::Type::AddEdge;
    }
    if (changes != 3 || !sawNode || !sawEdge
        || !manager.removeRuntimeEdge("custom-1", "custom-2")
        || !manager.removeRuntimeNode("custom-1")) {
        std::cerr << "interactive graph edits did not emit the expected state changes\n";
        return false;
    }
    const auto removeDelta = session.refresh();
    bool sawRemovedNode = false;
    bool sawRemovedEdge = false;
    for (const auto &operation : removeDelta) {
        sawRemovedNode |= operation.type == GraphDeltaOperation::Type::RemoveNode;
        sawRemovedEdge |= operation.type == GraphDeltaOperation::Type::RemoveEdge;
    }
    return sawRemovedNode && sawRemovedEdge;
}

bool testRepositoryBuildsPythonCompatibleTopology()
{
    const QString connectionName = QStringLiteral("graph_repository_test");
    {
        QSqlDatabase database = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connectionName);
        database.setDatabaseName(QStringLiteral(":memory:"));
        if (!database.open()) {
            std::cerr << "could not open in-memory SQLite database\n";
            return false;
        }
        QSqlQuery query(database);
        const QStringList statements = {
            QStringLiteral("CREATE TABLE actress (actress_id INTEGER PRIMARY KEY)"),
            QStringLiteral("CREATE TABLE actress_name (actress_id INTEGER, cn TEXT, jp TEXT)"),
            QStringLiteral("CREATE TABLE work (work_id INTEGER PRIMARY KEY, serial_number TEXT, notes TEXT, series_id INTEGER, is_deleted INTEGER)"),
            QStringLiteral("CREATE TABLE work_actress_relation (work_id INTEGER, actress_id INTEGER)"),
            QStringLiteral("INSERT INTO actress VALUES (1)"),
            QStringLiteral("INSERT INTO actress_name VALUES (1, '演员一', '')"),
            QStringLiteral("INSERT INTO work VALUES (10, 'AAA-001', '关联 [[BBB-002]]', 2, 0)"),
            QStringLiteral("INSERT INTO work VALUES (11, 'BBB-002', '', 2, 0)"),
            QStringLiteral("INSERT INTO work_actress_relation VALUES (10, 1)"),
        };
        for (const QString &statement : statements) {
            if (!query.exec(statement)) {
                std::cerr << "could not build graph repository fixture\n";
                return false;
            }
        }

        darkeye::graph::GraphRepository repository(database);
        GraphStore store;
        QString error;
        if (!repository.load(store, &error) || store.nodes().size() != 3
            || store.edges().size() != 2) {
            std::cerr << "repository did not create the expected work, actress and induced edges\n";
            return false;
        }
        bool sawReference = false;
        for (const GraphEdge &edge : store.edges()) {
            sawReference |= edge.type == QStringLiteral("reference");
        }
        if (!sawReference) {
            std::cerr << "reference edge must take precedence over a duplicate series edge\n";
            return false;
        }
        const QStringList mutations = {
            QStringLiteral("INSERT INTO actress VALUES (2)"),
            QStringLiteral("INSERT INTO actress_name VALUES (2, '演员二', '')"),
            QStringLiteral("INSERT INTO work VALUES (12, 'CCC-003', '', 3, 0)"),
            QStringLiteral("DELETE FROM work_actress_relation WHERE work_id=10"),
            QStringLiteral("INSERT INTO work_actress_relation VALUES (10, 2)"),
            QStringLiteral("UPDATE work SET notes='关联 [[CCC-003]]', series_id=3 WHERE work_id=10"),
        };
        for (const QString &statement : mutations) {
            if (!query.exec(statement)) {
                std::cerr << "could not mutate graph repository fixture\n";
                return false;
            }
        }
        if (!repository.syncWork(store, 10, &error)
            || !store.containsNode(QStringLiteral("a2"))
            || !edgeType(store, QStringLiteral("w10"), QStringLiteral("a1")).isEmpty()
            || edgeType(store, QStringLiteral("w10"), QStringLiteral("a2")) != QStringLiteral("cast")
            || !edgeType(store, QStringLiteral("w10"), QStringLiteral("w11")).isEmpty()
            || edgeType(store, QStringLiteral("w10"), QStringLiteral("w12"))
                   != QStringLiteral("reference")) {
            std::cerr << "single-work synchronization did not reconcile cast, reference and series relations\n";
            return false;
        }
        if (!query.exec(QStringLiteral("UPDATE work SET is_deleted=1 WHERE work_id=10"))
            || !repository.syncWork(store, 10, &error)
            || store.containsNode(QStringLiteral("w10"))) {
            std::cerr << "single-work synchronization did not prune a deleted work\n";
            return false;
        }
        database.close();
    }
    QSqlDatabase::removeDatabase(connectionName);
    return true;
}

bool testScheduledWorkRefreshIsCoalesced()
{
    QTemporaryDir temporaryDirectory;
    if (!temporaryDirectory.isValid()) {
        std::cerr << "could not create incremental graph test directory\n";
        return false;
    }
    const QString connectionName = QStringLiteral("graph_incremental_queue_test");
    {
        QSqlDatabase database = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connectionName);
        database.setDatabaseName(temporaryDirectory.filePath(QStringLiteral("graph.sqlite")));
        if (!database.open()) {
            std::cerr << "could not open incremental graph database\n";
            return false;
        }
        QSqlQuery query(database);
        const QStringList statements = {
            QStringLiteral("CREATE TABLE actress (actress_id INTEGER PRIMARY KEY)"),
            QStringLiteral("CREATE TABLE actress_name (actress_id INTEGER, cn TEXT, jp TEXT)"),
            QStringLiteral("CREATE TABLE work (work_id INTEGER PRIMARY KEY, serial_number TEXT, notes TEXT, series_id INTEGER, is_deleted INTEGER)"),
            QStringLiteral("CREATE TABLE work_actress_relation (work_id INTEGER, actress_id INTEGER)"),
            QStringLiteral("INSERT INTO actress VALUES (1)"),
            QStringLiteral("INSERT INTO actress_name VALUES (1, '演员一', '')"),
            QStringLiteral("INSERT INTO work VALUES (10, 'QUEUE-001', '', 0, 0)"),
        };
        for (const QString &statement : statements) {
            if (!query.exec(statement)) {
                std::cerr << "could not build incremental graph fixture\n";
                return false;
            }
        }

        darkeye::graph::GraphManager manager(database);
        QString error;
        if (!manager.initialize(&error)) {
            std::cerr << "could not initialize incremental graph manager\n";
            return false;
        }
        if (!query.exec(QStringLiteral("INSERT INTO work_actress_relation VALUES (10, 1)"))) {
            std::cerr << "could not mutate incremental graph fixture\n";
            return false;
        }
        int changeCount = 0;
        QObject::connect(&manager, &darkeye::graph::GraphManager::graphChanged,
                         [&changeCount] { ++changeCount; });
        manager.scheduleRefreshWork(10);
        manager.scheduleRefreshWork(10);
        manager.scheduleRefreshWork(10);

        QEventLoop eventLoop;
        QTimer::singleShot(1000, &eventLoop, &QEventLoop::quit);
        eventLoop.exec();
        if (changeCount != 1 || edgeType(manager.store(), QStringLiteral("w10"),
                                         QStringLiteral("a1")) != QStringLiteral("cast")) {
            std::cerr << "scheduled incremental graph refresh was not coalesced\n";
            return false;
        }
        // Let the worker finish without delivering its queued result, then load a newer graph.
        manager.scheduleRefreshWork(10);
        QCoreApplication::sendPostedEvents(&manager, QEvent::MetaCall);
        if (!QThreadPool::globalInstance()->waitForDone(5000)
            || !query.exec(QStringLiteral("INSERT INTO actress VALUES (2)"))
            || !manager.refresh(&error)) {
            std::cerr << "could not reload the graph while an incremental result was queued\n";
            return false;
        }
        QCoreApplication::sendPostedEvents(&manager, QEvent::MetaCall);
        if (changeCount != 2 || !manager.store().containsNode(QStringLiteral("a2"))) {
            std::cerr << "an older incremental result overwrote the full graph reload\n";
            return false;
        }
        database.close();
    }
    QSqlDatabase::removeDatabase(connectionName);
    return true;
}

bool testScheduledInitializeIsAsynchronousAndCoalesced()
{
    QTemporaryDir temporaryDirectory;
    if (!temporaryDirectory.isValid()) {
        std::cerr << "could not create initialization graph test directory\n";
        return false;
    }
    const QString connectionName = QStringLiteral("graph_initialize_queue_test");
    {
        QSqlDatabase database = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connectionName);
        database.setDatabaseName(temporaryDirectory.filePath(QStringLiteral("graph.sqlite")));
        if (!database.open()) {
            std::cerr << "could not open initialization graph database\n";
            return false;
        }
        QSqlQuery query(database);
        const QStringList statements = {
            QStringLiteral("CREATE TABLE actress (actress_id INTEGER PRIMARY KEY)"),
            QStringLiteral("CREATE TABLE actress_name (actress_id INTEGER, cn TEXT, jp TEXT)"),
            QStringLiteral("CREATE TABLE work (work_id INTEGER PRIMARY KEY, serial_number TEXT, notes TEXT, series_id INTEGER, is_deleted INTEGER)"),
            QStringLiteral("CREATE TABLE work_actress_relation (work_id INTEGER, actress_id INTEGER)"),
            QStringLiteral("INSERT INTO work VALUES (10, 'ASYNC-001', '', 0, 0)"),
        };
        for (const QString &statement : statements) {
            if (!query.exec(statement)) {
                std::cerr << "could not build initialization graph fixture\n";
                return false;
            }
        }

        darkeye::graph::GraphManager manager(database);
        int initializedCount = 0;
        QEventLoop eventLoop;
        QObject::connect(&manager, &darkeye::graph::GraphManager::initialized,
                         [&initializedCount, &eventLoop] {
                             ++initializedCount;
                             eventLoop.quit();
                         });
        manager.scheduleInitialize();
        manager.scheduleInitialize();
        QTimer::singleShot(1000, &eventLoop, &QEventLoop::quit);
        eventLoop.exec();
        if (!manager.isInitialized() || initializedCount != 1
            || !manager.store().containsNode(QStringLiteral("w10"))) {
            std::cerr << "scheduled graph initialization was not coalesced\n";
            return false;
        }
        database.close();
    }
    QSqlDatabase::removeDatabase(connectionName);
    return true;
}

} // namespace

int main(int argc, char *argv[])
{
    QCoreApplication application(argc, argv);
    return testFiltersAndInducedEdges() && testIncrementalDelta() && testEdgeTypeUpdateDelta()
            && testInteractiveGraphEditsEmitUsableDeltas()
            && testRepositoryBuildsPythonCompatibleTopology()
            && testScheduledInitializeIsAsynchronousAndCoalesced()
            && testScheduledWorkRefreshIsCoalesced()
        ? 0
        : 1;
}
