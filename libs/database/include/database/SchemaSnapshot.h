#pragma once

#include <QJsonDocument>
#include <QSqlDatabase>
#include <QString>

namespace darkeye {

class SchemaSnapshot final
{
public:
    static QJsonDocument capture(const QSqlDatabase &database,
                                 QString *errorMessage = nullptr);
};

} // namespace darkeye
