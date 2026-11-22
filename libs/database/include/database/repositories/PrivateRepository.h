#pragma once

#include <QDate>
#include <QList>
#include <QMap>
#include <QSqlDatabase>
#include <QString>

namespace darkeye
{

enum class PersonalRecordKind
{
    Masturbation,
    LoveMaking,
    SexualArousal,
};

class PrivateRepository final
{
public:
    explicit PrivateRepository(QSqlDatabase database);

    [[nodiscard]] bool isFavoriteWork(qint64 workId, QString *errorMessage = nullptr) const;
    [[nodiscard]] bool isFavoriteActress(qint64 actressId, QString *errorMessage = nullptr) const;
    bool addFavoriteWork(qint64 workId, const QString &serialNumber,
                         QString *errorMessage = nullptr);
    bool addFavoriteActress(qint64 actressId, const QString &japaneseName,
                            QString *errorMessage = nullptr);
    bool removeFavoriteWork(qint64 workId, QString *errorMessage = nullptr);
    bool removeFavoriteActress(qint64 actressId, QString *errorMessage = nullptr);

    bool addMasturbationRecord(qint64 workId, const QString &serialNumber, const QString &startTime,
                               const QString &toolName, int rating, const QString &comment,
                               QString *errorMessage = nullptr);
    bool addLoveMakingRecord(const QString &eventTime, int rating, const QString &comment,
                             QString *errorMessage = nullptr);
    bool addSexualArousalRecord(const QString &arousalTime, const QString &comment,
                                QString *errorMessage = nullptr);
    [[nodiscard]] QStringList masturbationToolSuggestions(
        QString *errorMessage = nullptr) const;
    [[nodiscard]] QMap<QDate, int> dailyCounts(int year, PersonalRecordKind kind,
                                               QString *errorMessage = nullptr) const;
    [[nodiscard]] QList<qint64> favoriteWorkIds(QString *errorMessage = nullptr) const;
    [[nodiscard]] QList<qint64> favoriteActressIds(QString *errorMessage = nullptr) const;
    [[nodiscard]] QList<qint64> masturbationWorkIds(QString *errorMessage = nullptr) const;
    [[nodiscard]] QList<qint64> favoriteUnwatchedWorkIds(QString *errorMessage = nullptr) const;

private:
    [[nodiscard]] bool exists(const QString &table, const QString &idColumn, qint64 id,
                              QString *errorMessage) const;
    bool remove(const QString &table, const QString &idColumn, qint64 id, QString *errorMessage);
    [[nodiscard]] QList<qint64> queryWorkIds(const QString &sql, QString *errorMessage) const;

    QSqlDatabase m_database;
};

} // namespace darkeye
