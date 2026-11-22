#pragma once

#include "graph/GraphRepository.h"

#include <QObject>
#include <QMutex>

namespace darkeye::graph {

/**
 * Application-wide counterpart of Python's GraphManager.
 *
 * It owns the unfiltered topology. Views never query the database directly:
 * they create GraphViewSessions over store() and react to graphChanged().
 */
class GraphManager final : public QObject
{
    Q_OBJECT

public:
    explicit GraphManager(QSqlDatabase publicDatabase, QSqlDatabase privateDatabase = {},
                          QObject *parent = nullptr);

    bool initialize(QString *errorMessage = nullptr);
    /** Start the initial full graph load on a worker thread.  Repeated calls coalesce. */
    void scheduleInitialize();
    bool refresh(QString *errorMessage = nullptr);
    /** Queue a background incremental refresh; duplicate work IDs are coalesced. */
    void scheduleRefreshWork(qint64 workId);
    /**
     * Apply an interactive topology edit to the currently loaded graph.
     *
     * These edits deliberately live in the graph session only: database-backed
     * relationship changes enter through refresh(), while the settings-panel
     * tools can safely experiment with a graph without inventing database
     * records for arbitrary node ids.
     */
    bool upsertRuntimeNode(GraphNode node);
    bool removeRuntimeNode(const QString &nodeId);
    bool upsertRuntimeEdge(GraphEdge edge);
    bool removeRuntimeEdge(const QString &sourceId, const QString &targetId);
    [[nodiscard]] bool isInitialized() const noexcept;
    [[nodiscard]] const GraphStore &store() const noexcept;
    [[nodiscard]] QSet<QString> favoriteWorkNodeIds(QString *errorMessage = nullptr) const;
    /** Resolve the database image filename for a graph node on the UI thread. */
    [[nodiscard]] QString imagePathForNode(const QString &nodeId) const;
    /** Public database file path, used to locate the adjacent image directories. */
    [[nodiscard]] QString publicDatabasePath() const;

signals:
    void initialized();
    void graphChanged();
    void loadFailed(const QString &errorMessage);

private:
    void finishScheduledInitialize(GraphStore store, bool succeeded, QString errorMessage);
    void startNextScheduledRefresh();
    void finishScheduledRefresh(GraphStore store, bool succeeded, QString errorMessage,
                                quint64 refreshGeneration);

    GraphRepository m_repository;
    GraphStore m_store;
    QString m_publicDriverName;
    QString m_publicDatabaseName;
    QString m_publicConnectOptions;
    QString m_publicHostName;
    QString m_publicUserName;
    QString m_publicPassword;
    int m_publicPort = -1;
    QMutex m_initializationMutex;
    bool m_initializing = false;
    QMutex m_scheduledRefreshMutex;
    QSet<qint64> m_pendingWorkRefreshes;
    bool m_scheduledRefreshActive = false;
    bool m_initialized = false;
    quint64 m_fullRefreshGeneration = 0;
};

} // namespace darkeye::graph
