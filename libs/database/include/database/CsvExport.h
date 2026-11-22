#pragma once

#include <QString>

class QAbstractItemModel;

namespace darkeye {

bool exportModelToCsv(const QAbstractItemModel *model, const QString &csvFilePath,
                      QString *errorMessage = nullptr);
bool exportSqlToCsv(const QString &sql, const QString &csvFilePath,
                    const QString &databasePath, QString *errorMessage = nullptr);

} // namespace darkeye
