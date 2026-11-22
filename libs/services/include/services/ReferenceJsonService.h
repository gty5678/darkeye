#pragma once

#include "database/repositories/ReferenceRepository.h"

#include <QSqlDatabase>
#include <QString>

namespace darkeye
{

class ReferenceJsonService final
{
public:
    explicit ReferenceJsonService(QSqlDatabase database);

    bool exportToFile(ReferenceKind kind, const QString &path,
                      QString *errorMessage = nullptr) const;
    bool importFromFile(ReferenceKind kind, const QString &path, QString *errorMessage = nullptr);

private:
    QSqlDatabase m_database;
};

} // namespace darkeye
