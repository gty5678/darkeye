#pragma once

#include "database/repositories/PrivateRepository.h"

#include <QSqlDatabase>
#include <QDate>
#include <QString>
#include <QVector>
#include <optional>

namespace darkeye
{

struct DashboardStatistics final
{
    int workCount = 0;
    int actressCount = 0;
    int actorCount = 0;
    int tagCount = 0;
    int recentThirtyDays = 0;
    int favoriteWorkCount = 0;
    int favoriteActressCount = 0;
};

struct DashboardActivity final
{
    enum class Kind { Work, Actress };

    Kind kind;
    QString name;
    QString time;
};

struct DashboardPendingCounts final
{
    int missingCover;
    int unlinkedActress;
};

struct TopActressStatistic final
{
    qint64 actressId = 0;
    QString name;
    QString imagePath;
    QString latestRecordTime;
    int recordCount = 0;
};

struct ChartValue final
{
    QString label;
    qreal value = 0;
};

struct WeightedChartValue final
{
    qreal value = 0;
    qreal weight = 1;
};

struct WaistHipChartValue final
{
    qreal waist = 0;
    qreal hip = 0;
    qreal weight = 1;
    qreal ratio = 0;
};

class StatisticsRepository final
{
public:
    StatisticsRepository(QSqlDatabase publicDatabase, QSqlDatabase privateDatabase);

    [[nodiscard]] std::optional<DashboardStatistics> dashboard(
        QString *errorMessage = nullptr) const;
    [[nodiscard]] std::optional<QVector<DashboardActivity>> recentWatching() const;
    [[nodiscard]] std::optional<QVector<DashboardActivity>> recentAdditions() const;
    [[nodiscard]] std::optional<DashboardPendingCounts> dashboardPendingCounts() const;
    [[nodiscard]] std::optional<TopActressStatistic> topActress(
        int days, QString *errorMessage = nullptr) const;
    [[nodiscard]] int recordCountInDays(int days, PersonalRecordKind kind,
                                        QString *errorMessage = nullptr) const;
    [[nodiscard]] int earliestRecordYear(QString *errorMessage = nullptr) const;
    [[nodiscard]] int favoriteUnwatchedSalesCycle(QString *errorMessage = nullptr) const;

    // Scope matches the Python statistics page: -1 public, 0 favorite,
    // 1 recorded works, and 2 recorded works weighted by record count.
    [[nodiscard]] QVector<WeightedChartValue> workActressAges(
        int scope, QString *errorMessage = nullptr) const;
    [[nodiscard]] QVector<WeightedChartValue> actressHeights(
        int scope, QString *errorMessage = nullptr) const;
    [[nodiscard]] QVector<WeightedChartValue> actressDebutAges(
        QString *errorMessage = nullptr) const;
    [[nodiscard]] QVector<ChartValue> workReleaseYears(
        int scope, QString *errorMessage = nullptr) const;
    [[nodiscard]] QVector<ChartValue> actressDebutYears(
        int scope, QString *errorMessage = nullptr) const;
    [[nodiscard]] QVector<ChartValue> cupDistribution(
        int scope, QString *errorMessage = nullptr) const;
    [[nodiscard]] QVector<WaistHipChartValue> waistHipDistribution(
        int scope, QString *errorMessage = nullptr) const;
    [[nodiscard]] QVector<ChartValue> topDirectors(
        int scope, QString *errorMessage = nullptr) const;
    [[nodiscard]] QVector<ChartValue> topMakers(
        int scope, QString *errorMessage = nullptr) const;
    [[nodiscard]] QVector<ChartValue> mostRecordedActresses(
        QString *errorMessage = nullptr) const;
    [[nodiscard]] QVector<QDate> workAddedDates(QString *errorMessage = nullptr) const;

private:
    QSqlDatabase m_publicDatabase;
    QSqlDatabase m_privateDatabase;
};

} // namespace darkeye
