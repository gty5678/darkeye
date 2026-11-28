#pragma once

#include "domain/Person.h"

#include <QList>
#include <QSet>
#include <QSqlDatabase>
#include <QString>
#include <optional>

namespace darkeye
{

class PersonRepository final
{
public:
    explicit PersonRepository(QSqlDatabase database);

    std::optional<qint64> create(PersonKind kind, const QString &chineseName,
                                 const QString &japaneseName, QString *errorMessage = nullptr);
    [[nodiscard]] std::optional<qint64> findByName(PersonKind kind, const QString &name,
                                                   QString *errorMessage = nullptr) const;
    [[nodiscard]] QList<PersonSummary> search(const PersonSearch &search,
                                              QString *errorMessage = nullptr) const;
    [[nodiscard]] std::optional<int> count(const PersonSearch &search,
                                           QString *errorMessage = nullptr) const;
    [[nodiscard]] QSet<qint64> matchingPersonIds(const PersonSearch &search) const;
    [[nodiscard]] QList<qint64> actressIdsNeedingUpdate(QString *errorMessage = nullptr) const;
    [[nodiscard]] QStringList nameSuggestions(PersonKind kind) const;
    [[nodiscard]] QStringList cupOptions() const;
    [[nodiscard]] std::optional<PersonDetails> findDetails(PersonKind kind, qint64 personId,
                                                           QString *errorMessage = nullptr) const;
    bool updateDetails(const PersonDetails &details, QString *errorMessage = nullptr);
    bool deletePerson(PersonKind kind, qint64 personId, QString *errorMessage = nullptr);

private:
    QSqlDatabase m_database;
};

} // namespace darkeye
