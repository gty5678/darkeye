#pragma once

#include <QSqlDatabase>
#include <QString>

namespace darkeye
{

struct MakerAssignmentResult final
{
    bool succeeded = false;
    int updated = 0;
    int alreadyMatched = 0;
    int noRule = 0;
    int noPrefix = 0;
    QString errorMessage;

    [[nodiscard]] QString summary() const;
};

struct CoverNormalizationResult final
{
    bool succeeded = false;
    int renamed = 0;
    int databaseOnly = 0;
    int alreadyNormalized = 0;
    int missing = 0;
    int conflict = 0;
    int invalidOrOutside = 0;
    int renameFailed = 0;
    QString errorMessage;

    [[nodiscard]] QString summary() const;
};

class WorkMaintenanceService final
{
public:
    explicit WorkMaintenanceService(QSqlDatabase database, QString coverDirectory = {});

    [[nodiscard]] MakerAssignmentResult assignMakersFromPrefixes() const;
    [[nodiscard]] CoverNormalizationResult normalizeCoverFileNames() const;

private:
    QSqlDatabase m_database;
    QString m_coverDirectory;
};

} // namespace darkeye
