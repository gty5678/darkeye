#pragma once

#include <QList>
#include <QString>
#include <QStringList>
#include <optional>

namespace darkeye
{

struct Work final
{
    qint64 id = 0;
    QString serialNumber;
    QString director;
    std::optional<int> runtime;
    QString notes;
    QString releaseDate;
    QString imageUrl;
    QString videoUrl;
    QString chineseTitle;
    QString japaneseTitle;
    QString chineseStory;
    QString japaneseStory;
    std::optional<qint64> makerId;
    std::optional<qint64> labelId;
    std::optional<qint64> seriesId;
    QString fanartJson;
    bool deleted = false;
};

struct WorkSummary final
{
    qint64 id = 0;
    QString serialNumber;
    QString chineseTitle;
    QString releaseDate;
    QString director;
    QString imageUrl;
    int highlightTagId = 0;
    bool standard = false;
    QString videoUrl;
};

struct WorkStateRecord final
{
    qint64 id = 0;
    QString serialNumber;
    QString chineseTitle;
    QString japaneseTitle;
    QString releaseDate;
    QString imageUrl;
    // Raw work-table fields, in database column order.  The soft-delete page
    // deliberately presents the same complete record set as Python's SELECT * view.
    QStringList tableValues;
};

struct TagOption final
{
    qint64 id = 0;
    QString name;
    QString typeName;
    QString color;
    QString detail;
    QString mutexGroup;
    QStringList aliases;
};

struct NamedIdOption final
{
    qint64 id = 0;
    QString name;
    QString chineseName;
    QString japaneseName;
    QStringList aliases;
};

struct WorkPersonReference final
{
    qint64 id = 0;
    QString name;
};

struct WorkDetails final
{
    Work work;
    QString makerName;
    QString labelName;
    QString seriesName;
    QList<WorkPersonReference> actresses;
    QList<WorkPersonReference> actors;
    QList<TagOption> tags;
};

enum class WorkSortOrder
{
    Random,
    UpdatedDescending,
    UpdatedAscending,
    CreatedDescending,
    CreatedAscending,
    ReleaseDateDescending,
    ReleaseDateAscending,
    SerialAscending,
    SerialDescending,
    MakerAscending,
    MakerDescending,
    ActressAgeAscending,
    ActressAgeDescending,
};

struct WorkSearch final
{
    QString keyword;
    QString serialNumber;
    QString title;
    QString chineseStory;
    QString notes;
    QString director;
    QString actressName;
    QString actorName;
    QString tagName;
    QString makerName;
    QString labelName;
    QString seriesName;
    std::optional<qint64> makerId;
    std::optional<qint64> labelId;
    std::optional<qint64> seriesId;
    QList<qint64> tagIds;
    QList<qint64> includedWorkIds;
    bool restrictToIncludedWorkIds = false;
    bool requireLocalVideo = false;
    quint32 randomSeed = 1;
    quint32 randomSeed2 = 1;
    WorkSortOrder order = WorkSortOrder::UpdatedDescending;
    int limit = 20;
    int offset = 0;
};

} // namespace darkeye
