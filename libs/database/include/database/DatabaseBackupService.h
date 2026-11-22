#pragma once

#include "database/SqliteConnection.h"

#include <QString>

namespace darkeye {

class DatabaseBackupService final
{
public:
    static bool createConsistentBackup(const SqliteConnection &source,
                                       const QString &destinationPath,
                                       QString *errorMessage = nullptr);
};

} // namespace darkeye

