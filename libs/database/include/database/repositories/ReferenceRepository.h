#pragma once

#include <QList>
#include <QSqlDatabase>
#include <QString>
#include <QStringList>
#include <optional>

namespace darkeye
{

enum class ReferenceKind
{
    Maker,
    Label,
    Series,
};

struct ReferenceRecord final
{
    ReferenceKind kind = ReferenceKind::Maker;
    qint64 id = 0;
    QString chineseName;
    QString japaneseName;
    QString aliases;
    QString detail;
    QString extra;
};

struct TagTypeRecord final
{
    qint64 id = 0;
    QString name;
    int order = 0;
};

struct TagRecord final
{
    qint64 id = 0;
    QString name;
    std::optional<qint64> typeId;
    QString typeName;
    QString color = QStringLiteral("#cccccc");
    QString detail;
    std::optional<qint64> redirectTagId;
    std::optional<qint64> groupId;
    QStringList aliases;
};

struct MakerPrefixRecord final
{
    qint64 id = 0;
    QString prefix;
    qint64 makerId = 0;
    QString makerName;
};

class ReferenceRepository final
{
public:
    explicit ReferenceRepository(QSqlDatabase database);

    [[nodiscard]] QList<ReferenceRecord> list(ReferenceKind kind,
                                              QString *errorMessage = nullptr) const;
    std::optional<qint64> create(ReferenceKind kind, const QString &name,
                                 QString *errorMessage = nullptr);
    std::optional<qint64> create(const ReferenceRecord &record, QString *errorMessage = nullptr);
    bool update(const ReferenceRecord &record, QString *errorMessage = nullptr);
    bool remove(ReferenceKind kind, qint64 id, QString *errorMessage = nullptr);
    bool redirect(ReferenceKind kind, qint64 sourceId, qint64 targetId,
                  QString *errorMessage = nullptr);
    [[nodiscard]] std::optional<qint64> resolveTagId(qint64 tagId) const;
    [[nodiscard]] QList<TagRecord> listTags(QString *errorMessage = nullptr) const;
    [[nodiscard]] QList<TagTypeRecord> listTagTypes(QString *errorMessage = nullptr) const;
    std::optional<qint64> createTag(const TagRecord &record, QString *errorMessage = nullptr);
    std::optional<qint64> createTag(const QString &name, std::optional<qint64> typeId,
                                    const QString &color, const QString &detail,
                                    QString *errorMessage = nullptr);
    bool updateTag(const TagRecord &record, QString *errorMessage = nullptr);
    bool updateTagColors(const QList<qint64> &tagIds, const QString &color,
                         QString *errorMessage = nullptr);
    bool removeTag(qint64 tagId, QString *errorMessage = nullptr);
    bool redirectTag(qint64 sourceId, qint64 targetId, QString *errorMessage = nullptr);
    std::optional<qint64> createTagType(const QString &name, int order,
                                        QString *errorMessage = nullptr);
    bool updateTagType(const TagTypeRecord &record, QString *errorMessage = nullptr);
    bool removeTagType(qint64 typeId, QString *errorMessage = nullptr);
    bool moveTagType(qint64 typeId, int offset, QString *errorMessage = nullptr);
    [[nodiscard]] QList<MakerPrefixRecord> listMakerPrefixes(QString *errorMessage = nullptr) const;
    std::optional<qint64> createMakerPrefix(const QString &prefix, qint64 makerId,
                                            QString *errorMessage = nullptr);
    bool updateMakerPrefix(const MakerPrefixRecord &record, QString *errorMessage = nullptr);
    bool removeMakerPrefix(qint64 id, QString *errorMessage = nullptr);
    [[nodiscard]] std::optional<qint64> findByName(ReferenceKind kind, const QString &name,
                                                   QString *errorMessage = nullptr) const;

private:
    QSqlDatabase m_database;
};

} // namespace darkeye
