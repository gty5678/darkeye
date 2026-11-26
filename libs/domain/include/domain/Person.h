#pragma once

#include <QList>
#include <QMetaType>
#include <QString>
#include <optional>

namespace darkeye
{

enum class PersonKind
{
    Actress,
    Actor,
};

enum class PersonSortOrder
{
    Random,
    CreatedAscending,
    CreatedDescending,
    ImageFirst,
    BirthdayAscending,
    BirthdayDescending,
    DebutAscending,
    DebutDescending,
    HeightAscending,
    HeightDescending,
    CupAscending,
    CupDescending,
    WaistHipRatioAscending,
    WaistHipRatioDescending,
};

struct PersonSummary final
{
    qint64 id = 0;
    QString name;
    QString imagePath;
};

struct PersonName final
{
    qint64 id = 0;
    QString chinese;
    QString japanese;
    QString english;
    QString kana;
};

struct PersonWorkSummary final
{
    qint64 id = 0;
    QString serialNumber;
    QString title;
    QString releaseDate;
    QString imageUrl;
    int highlightTagId = 0;
    bool standard = false;
};

struct PersonBodyMetrics final
{
    std::optional<int> height;
    std::optional<int> bust;
    std::optional<int> waist;
    std::optional<int> hip;
    QString cup;
};

struct PersonDetails final
{
    PersonKind kind = PersonKind::Actress;
    qint64 id = 0;
    QList<PersonName> names;
    QString imagePath;
    QString birthday;
    std::optional<int> height;
    std::optional<int> bust;
    std::optional<int> waist;
    std::optional<int> hip;
    QString cup;
    QString debutDate;
    std::optional<int> handsome;
    std::optional<int> fat;
    bool needUpdate = true;
    QString minnanoUrl;
    QString notes;
    QList<PersonWorkSummary> works;
    QList<PersonBodyMetrics> bodyReference;
};

struct PersonSearch final
{
    PersonKind kind = PersonKind::Actress;
    QString name;
    QString cup;
    PersonSortOrder sortOrder = PersonSortOrder::CreatedDescending;
    QList<qint64> includedIds;
    bool restrictToIncludedIds = false;
    int limit = 70;
    int offset = 0;
    quint32 randomSeed = 1;
    quint32 randomSeed2 = 1;
};

} // namespace darkeye

Q_DECLARE_METATYPE(darkeye::PersonKind)
