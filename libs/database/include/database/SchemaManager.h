#pragma once

#include "database/SqliteConnection.h"

#include <QString>
#include <QStringList>

namespace darkeye {

enum class DatabaseKind
{
    Public,
    Private,
};

class SchemaManager final
{
public:
    static QString requiredVersion(DatabaseKind kind);
    static QStringList requiredTables(DatabaseKind kind);

    static bool initializeEmptyDatabase(SqliteConnection &connection, DatabaseKind kind,
                                        QString *errorMessage = nullptr);
    static bool migrateToCurrent(SqliteConnection &connection, DatabaseKind kind,
                                 QString *errorMessage = nullptr);
    static bool validateCurrentSchema(const SqliteConnection &connection, DatabaseKind kind,
                                      QString *errorMessage = nullptr);

private:
    static bool isEmpty(const QSqlDatabase &database);
    static bool migratePublicFromV1(SqliteConnection &connection, QString *errorMessage);
    static bool migratePrivateFromV1(SqliteConnection &connection, QString *errorMessage);
};

} // namespace darkeye

