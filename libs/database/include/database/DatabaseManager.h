#pragma once

#include "settings/Paths.h"
#include "database/SqliteConnection.h"

#include <QString>

namespace darkeye {

enum class DatabaseKind;

class DatabaseManager final
{
public:
    bool initialize(const settings::Paths &paths, QString *errorMessage = nullptr);
    bool initialize(QString *errorMessage = nullptr);

    [[nodiscard]] SqliteConnection &publicConnection() noexcept;
    [[nodiscard]] SqliteConnection &privateConnection() noexcept;

private:
    bool prepareDatabase(SqliteConnection &connection, const QString &databasePath,
                         const QString &backupDirectory, DatabaseKind kind,
                         QString *errorMessage);

    SqliteConnection m_publicConnection;
    SqliteConnection m_privateConnection;
};

} // namespace darkeye
