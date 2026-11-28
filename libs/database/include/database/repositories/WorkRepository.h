#pragma once

#include "domain/Work.h"

#include <QSet>
#include <QList>
#include <QSqlDatabase>
#include <QString>
#include <QVariantMap>
#include <optional>

namespace darkeye
{

struct WorkCrawlSnapshot final
{
    QString serial;
    QVariantMap values;
};

class WorkRepository final
{
public:
    explicit WorkRepository(QSqlDatabase database);

    [[nodiscard]] std::optional<Work> findById(qint64 workId,
                                               QString *errorMessage = nullptr) const;
    [[nodiscard]] std::optional<WorkDetails> findDetailsById(qint64 workId,
                                                             QString *errorMessage = nullptr) const;
    [[nodiscard]] QList<qint64> allIds(bool includeDeleted = true,
                                       QString *errorMessage = nullptr) const;
    [[nodiscard]] QList<WorkSummary> search(const WorkSearch &search,
                                            QString *errorMessage = nullptr) const;
    [[nodiscard]] QSet<qint64> matchingWorkIds(const WorkSearch &search) const;
    [[nodiscard]] std::optional<int> count(const WorkSearch &search,
                                           QString *errorMessage = nullptr) const;
    [[nodiscard]] bool existsSerial(const QString &serialNumber,
                                    QString *errorMessage = nullptr) const;
    [[nodiscard]] std::optional<QSet<QString>> existingSerials(
        const QStringList &normalizedSerials, QString *errorMessage = nullptr) const;
    [[nodiscard]] QList<WorkCrawlSnapshot> loadCrawlSnapshots(QString *errorMessage = nullptr) const;
    [[nodiscard]] std::optional<qint64> findIdBySerial(
        const QString &serialNumber, QString *errorMessage = nullptr) const;
    [[nodiscard]] QStringList serialSuggestions() const;
    [[nodiscard]] QStringList actressSuggestions() const;
    [[nodiscard]] QStringList actorSuggestions() const;
    [[nodiscard]] QStringList directorSuggestions() const;
    [[nodiscard]] QStringList makerSuggestions() const;
    [[nodiscard]] QStringList labelSuggestions() const;
    [[nodiscard]] QStringList seriesSuggestions() const;
    [[nodiscard]] QList<NamedIdOption> makerOptions() const;
    [[nodiscard]] QList<NamedIdOption> labelOptions() const;
    [[nodiscard]] QList<NamedIdOption> seriesOptions() const;
    [[nodiscard]] QList<TagOption> tagOptions() const;
    [[nodiscard]] QList<WorkStateRecord> listByDeletedState(bool deleted,
                                                            const QString &keyword = {},
                                                            QString *errorMessage = nullptr) const;

    std::optional<qint64> insertSerial(const QString &serialNumber,
                                       QString *errorMessage = nullptr);
    std::optional<qint64> insertComplete(const Work &work, const QList<qint64> &actressIds,
                                         const QList<qint64> &actorIds, const QList<qint64> &tagIds,
                                         QString *errorMessage = nullptr);
    bool setDeleted(qint64 workId, bool deleted, QString *errorMessage = nullptr);
    bool setDeletedMany(const QList<qint64> &workIds, bool deleted,
                        QString *errorMessage = nullptr);
    bool permanentlyRemoveDeletedMany(const QList<qint64> &workIds,
                                      QStringList *removedImageUrls = nullptr,
                                      QString *errorMessage = nullptr,
                                      QStringList *removedFanartFiles = nullptr);
    bool updateDetails(const Work &work, QString *errorMessage = nullptr);
    bool updateComplete(const Work &work, const QList<qint64> &actressIds,
                        const QList<qint64> &actorIds, const QList<qint64> &tagIds,
                        QString *errorMessage = nullptr);

private:
    QSqlDatabase m_database;
};

} // namespace darkeye
