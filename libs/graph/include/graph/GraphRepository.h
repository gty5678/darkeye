#pragma once

#include "graph/GraphStore.h"

#include <QSqlDatabase>

namespace darkeye::graph {

/** Loads the relationship graph from Darkeye's public and private databases. */
class GraphRepository final
{
public:
    GraphRepository(QSqlDatabase publicDatabase, QSqlDatabase privateDatabase = {});

    bool load(GraphStore &store, QString *errorMessage = nullptr) const;
    /** Synchronize one work and its derived relationship edges without rebuilding the graph. */
    bool syncWork(GraphStore &store, qint64 workId, QString *errorMessage = nullptr) const;
    [[nodiscard]] QSet<QString> favoriteWorkNodeIds(QString *errorMessage = nullptr) const;
    /** Return the database image filename for an actress/work graph node. */
    [[nodiscard]] QString imagePathForNode(const QString &nodeId) const;

private:
    QSqlDatabase m_publicDatabase;
    QSqlDatabase m_privateDatabase;
};

} // namespace darkeye::graph
