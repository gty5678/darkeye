#include "graph/GraphManager.h"

#include <QMetaObject>
#include <QMutexLocker>
#include <QPointer>
#include <QSqlError>
#include <QThreadPool>
#include <QUuid>

#include <utility>

namespace darkeye::graph {

GraphManager::GraphManager(QSqlDatabase publicDatabase, QSqlDatabase privateDatabase,
                           QObject *parent)
    : QObject(parent), m_repository(publicDatabase, privateDatabase),
      m_publicDriverName(publicDatabase.driverName()),
      m_publicDatabaseName(publicDatabase.databaseName()),
      m_publicConnectOptions(publicDatabase.connectOptions()),
      m_publicHostName(publicDatabase.hostName()), m_publicUserName(publicDatabase.userName()),
      m_publicPassword(publicDatabase.password()), m_publicPort(publicDatabase.port())
{
}

bool GraphManager::initialize(QString *errorMessage)
{
    if (m_initialized) {
        return true;
    }
    if (!refresh(errorMessage)) {
        return false;
    }
    m_initialized = true;
    emit initialized();
    return true;
}

void GraphManager::scheduleInitialize()
{
    {
        QMutexLocker lock(&m_initializationMutex);
        if (m_initialized || m_initializing) {
            return;
        }
        m_initializing = true;
    }

    const QString driverName = m_publicDriverName;
    const QString databaseName = m_publicDatabaseName;
    const QString connectOptions = m_publicConnectOptions;
    const QString hostName = m_publicHostName;
    const QString userName = m_publicUserName;
    const QString password = m_publicPassword;
    const int port = m_publicPort;
    QPointer<GraphManager> target(this);
    QThreadPool::globalInstance()->start(
        [target, driverName, databaseName, connectOptions, hostName, userName, password,
         port]() mutable
        {
            GraphStore store;
            bool succeeded = false;
            QString error;
            const QString connectionName =
                QStringLiteral("graph_initialize_%1")
                    .arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
            {
                QSqlDatabase database = QSqlDatabase::addDatabase(driverName, connectionName);
                database.setDatabaseName(databaseName);
                database.setConnectOptions(connectOptions);
                database.setHostName(hostName);
                database.setUserName(userName);
                database.setPassword(password);
                if (port >= 0) {
                    database.setPort(port);
                }
                if (!database.open()) {
                    error = database.lastError().text();
                } else {
                    GraphRepository repository(database);
                    succeeded = repository.load(store, &error);
                }
                database.close();
                database = {};
            }
            QSqlDatabase::removeDatabase(connectionName);

            if (target) {
                QMetaObject::invokeMethod(
                    target.data(),
                    [target, store = std::move(store), succeeded, error = std::move(error)]() mutable
                    {
                        if (target) {
                            target->finishScheduledInitialize(std::move(store), succeeded,
                                                            std::move(error));
                        }
                    },
                    Qt::QueuedConnection);
            }
        });
}

bool GraphManager::refresh(QString *errorMessage)
{
    QString localError;
    if (!m_repository.load(m_store, &localError)) {
        if (errorMessage != nullptr) {
            *errorMessage = localError;
        }
        emit loadFailed(localError);
        return false;
    }
    ++m_fullRefreshGeneration;
    emit graphChanged();
    return true;
}

void GraphManager::scheduleRefreshWork(qint64 workId)
{
    if (workId <= 0) {
        return;
    }
    bool startWorker = false;
    {
        QMutexLocker lock(&m_scheduledRefreshMutex);
        m_pendingWorkRefreshes.insert(workId);
        if (!m_scheduledRefreshActive) {
            m_scheduledRefreshActive = true;
            startWorker = true;
        }
    }
    if (startWorker) {
        QMetaObject::invokeMethod(this, &GraphManager::startNextScheduledRefresh,
                                  Qt::QueuedConnection);
    }
}

void GraphManager::startNextScheduledRefresh()
{
    if (!m_initialized) {
        scheduleInitialize();
        return;
    }

    qint64 workId = 0;
    {
        QMutexLocker lock(&m_scheduledRefreshMutex);
        if (m_pendingWorkRefreshes.isEmpty()) {
            m_scheduledRefreshActive = false;
            return;
        }
        auto next = m_pendingWorkRefreshes.cbegin();
        workId = *next;
        m_pendingWorkRefreshes.remove(workId);
    }

    GraphStore workerStore = m_store;
    const auto refreshGeneration = m_fullRefreshGeneration;
    const QString driverName = m_publicDriverName;
    const QString databaseName = m_publicDatabaseName;
    const QString connectOptions = m_publicConnectOptions;
    const QString hostName = m_publicHostName;
    const QString userName = m_publicUserName;
    const QString password = m_publicPassword;
    const int port = m_publicPort;
    QPointer<GraphManager> target(this);

    QThreadPool::globalInstance()->start(
        [target, workId, workerStore = std::move(workerStore), driverName, databaseName,
         connectOptions, hostName, userName, password, port, refreshGeneration]() mutable
        {
            bool succeeded = false;
            QString error;
            const QString connectionName =
                QStringLiteral("graph_incremental_%1")
                    .arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
            {
                QSqlDatabase database = QSqlDatabase::addDatabase(driverName, connectionName);
                database.setDatabaseName(databaseName);
                database.setConnectOptions(connectOptions);
                database.setHostName(hostName);
                database.setUserName(userName);
                database.setPassword(password);
                if (port >= 0) {
                    database.setPort(port);
                }
                if (!database.open()) {
                    error = database.lastError().text();
                } else {
                    GraphRepository repository(database);
                    succeeded = repository.syncWork(workerStore, workId, &error);
                }
                database.close();
                database = {};
            }
            QSqlDatabase::removeDatabase(connectionName);

            if (target) {
                QMetaObject::invokeMethod(
                    target.data(),
                    [target, store = std::move(workerStore), succeeded, error = std::move(error),
                     refreshGeneration]() mutable
                    {
                        if (target) {
                            target->finishScheduledRefresh(std::move(store), succeeded,
                                                         std::move(error), refreshGeneration);
                        }
                    },
                    Qt::QueuedConnection);
            }
        });
}

void GraphManager::finishScheduledInitialize(GraphStore store, bool succeeded,
                                             QString errorMessage)
{
    bool acceptResult = false;
    {
        QMutexLocker lock(&m_initializationMutex);
        m_initializing = false;
        // A caller may have performed a synchronous initialization while the
        // worker was loading.  Preserve that newer main-thread result rather
        // than replacing it with the worker's older snapshot.
        acceptResult = !m_initialized;
    }
    if (succeeded && acceptResult) {
        m_store = std::move(store);
        m_initialized = true;
        emit initialized();
        emit graphChanged();
    } else if (!succeeded && acceptResult) {
        emit loadFailed(errorMessage);
    }

    bool hasPendingRefresh = false;
    {
        QMutexLocker lock(&m_scheduledRefreshMutex);
        hasPendingRefresh = m_scheduledRefreshActive && !m_pendingWorkRefreshes.isEmpty();
        if (!succeeded && acceptResult && m_scheduledRefreshActive) {
            m_scheduledRefreshActive = false;
        }
    }
    if (hasPendingRefresh) {
        QMetaObject::invokeMethod(this, &GraphManager::startNextScheduledRefresh,
                                  Qt::QueuedConnection);
    }
}

void GraphManager::finishScheduledRefresh(GraphStore store, bool succeeded, QString errorMessage,
                                          quint64 refreshGeneration)
{
    // A full reload supersedes a worker snapshot taken before that reload.
    if (refreshGeneration == m_fullRefreshGeneration) {
        if (succeeded) {
            m_store = std::move(store);
            emit graphChanged();
        } else {
            emit loadFailed(errorMessage);
        }
    }

    bool startNext = false;
    {
        QMutexLocker lock(&m_scheduledRefreshMutex);
        if (m_pendingWorkRefreshes.isEmpty()) {
            m_scheduledRefreshActive = false;
        } else {
            startNext = true;
        }
    }
    if (startNext) {
        QMetaObject::invokeMethod(this, &GraphManager::startNextScheduledRefresh,
                                  Qt::QueuedConnection);
    }
}

bool GraphManager::upsertRuntimeNode(GraphNode node)
{
    if (!m_store.upsertNode(std::move(node))) {
        return false;
    }
    emit graphChanged();
    return true;
}

bool GraphManager::removeRuntimeNode(const QString &nodeId)
{
    if (!m_store.removeNode(nodeId)) {
        return false;
    }
    emit graphChanged();
    return true;
}

bool GraphManager::upsertRuntimeEdge(GraphEdge edge)
{
    if (!m_store.upsertEdge(std::move(edge))) {
        return false;
    }
    emit graphChanged();
    return true;
}

bool GraphManager::removeRuntimeEdge(const QString &sourceId, const QString &targetId)
{
    if (!m_store.removeEdge(sourceId, targetId)) {
        return false;
    }
    emit graphChanged();
    return true;
}

bool GraphManager::isInitialized() const noexcept
{
    return m_initialized;
}

const GraphStore &GraphManager::store() const noexcept
{
    return m_store;
}

QSet<QString> GraphManager::favoriteWorkNodeIds(QString *errorMessage) const
{
    return m_repository.favoriteWorkNodeIds(errorMessage);
}

QString GraphManager::imagePathForNode(const QString &nodeId) const
{
    return m_repository.imagePathForNode(nodeId);
}

QString GraphManager::publicDatabasePath() const
{
    return m_publicDatabaseName;
}

} // namespace darkeye::graph
