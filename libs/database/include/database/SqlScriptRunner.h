#pragma once

#include <QSqlDatabase>
#include <QString>
#include <QStringList>

#include <optional>

namespace darkeye {

class SqlScriptRunner final
{
public:
    static QStringList splitStatements(const QString &script);
    static bool execute(QSqlDatabase database, const QString &script,
                        QString *errorMessage = nullptr);
    [[nodiscard]] static std::optional<QString> readResource(
        const QString &resourcePath, QString *errorMessage = nullptr);
    static bool executeResource(QSqlDatabase database, const QString &resourcePath,
                                QString *errorMessage = nullptr);
};

} // namespace darkeye

