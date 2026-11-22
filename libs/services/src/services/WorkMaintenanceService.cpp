#include "services/WorkMaintenanceService.h"

#include "database/Transaction.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QList>
#include <QSqlError>
#include <QSqlQuery>
#include <utility>

namespace
{

bool isWithinDirectory(const QString &path, const QString &root)
{
    const QString normalizedPath = QDir::fromNativeSeparators(QDir::cleanPath(path));
    const QString normalizedRoot = QDir::fromNativeSeparators(QDir::cleanPath(root));
    return normalizedPath.compare(normalizedRoot, Qt::CaseInsensitive) == 0 ||
           normalizedPath.startsWith(normalizedRoot + QChar('/'), Qt::CaseInsensitive);
}

bool isSafeSerialFileName(const QString &serial)
{
    if (serial.isEmpty() || serial == QStringLiteral(".") || serial == QStringLiteral(".."))
        return false;
    for (const QChar character : QStringLiteral("\\/:*?\"<>|"))
    {
        if (serial.contains(character))
            return false;
    }
    return true;
}

struct FileMove final
{
    QString oldPath;
    QString newPath;
};

bool rollbackMoves(const QList<FileMove> &moves)
{
    bool succeeded = true;
    for (auto iterator = moves.crbegin(); iterator != moves.crend(); ++iterator)
    {
        if (QFileInfo::exists(iterator->newPath))
            succeeded = QFile::rename(iterator->newPath, iterator->oldPath) && succeeded;
    }
    return succeeded;
}

} // namespace

namespace darkeye
{

QString MakerAssignmentResult::summary() const
{
    if (!succeeded)
        return errorMessage;
    return QStringLiteral("已更新 %1 条作品的片商；已匹配且相同 %2；无此前缀规则 %3；"
                          "番号无前缀段 %4")
        .arg(updated)
        .arg(alreadyMatched)
        .arg(noRule)
        .arg(noPrefix);
}

QString CoverNormalizationResult::summary() const
{
    if (!succeeded)
        return errorMessage;
    return QStringLiteral("已重命名并写库 %1 条；仅纠正 image_url 字符串 %2 条；"
                          "已是目标文件名跳过 %3 条；源文件缺失 %4 条；目标已存在跳过 %5 条；"
                          "番号无效或路径越界 %6 条；重命名失败 %7 条")
        .arg(renamed)
        .arg(databaseOnly)
        .arg(alreadyNormalized)
        .arg(missing)
        .arg(conflict)
        .arg(invalidOrOutside)
        .arg(renameFailed);
}

WorkMaintenanceService::WorkMaintenanceService(QSqlDatabase database, QString coverDirectory)
    : m_database(std::move(database)), m_coverDirectory(QDir::cleanPath(std::move(coverDirectory)))
{
}

MakerAssignmentResult WorkMaintenanceService::assignMakersFromPrefixes() const
{
    MakerAssignmentResult result;
    QHash<QString, qint64> prefixToMaker;
    QSqlQuery prefixes(m_database);
    if (!prefixes.exec(QStringLiteral("SELECT prefix, maker_id FROM prefix_maker_relation "
                                      "WHERE prefix IS NOT NULL AND TRIM(prefix)<>'' "
                                      "ORDER BY rowid")))
    {
        result.errorMessage = prefixes.lastError().text();
        return result;
    }
    while (prefixes.next())
    {
        if (!prefixes.value(1).isNull())
            prefixToMaker.insert(prefixes.value(0).toString().trimmed(),
                                 prefixes.value(1).toLongLong());
    }
    if (prefixToMaker.isEmpty())
    {
        result.succeeded = true;
        return result;
    }

    Transaction transaction(m_database);
    if (!transaction.isActive())
    {
        result.errorMessage = transaction.errorString();
        return result;
    }
    QSqlQuery works(m_database);
    if (!works.exec(QStringLiteral("SELECT work_id, serial_number, maker_id FROM work "
                                   "WHERE IFNULL(is_deleted, 0)=0")))
    {
        result.errorMessage = works.lastError().text();
        return result;
    }
    QSqlQuery update(m_database);
    update.prepare(QStringLiteral("UPDATE work SET maker_id=? WHERE work_id=?"));
    while (works.next())
    {
        const QString serial = works.value(1).toString().trimmed();
        const int dash = serial.indexOf(QChar('-'));
        if (dash <= 0)
        {
            ++result.noPrefix;
            continue;
        }
        const QString prefix = serial.left(dash);
        if (!prefixToMaker.contains(prefix))
        {
            ++result.noRule;
            continue;
        }
        const qint64 makerId = prefixToMaker.value(prefix);
        if (!works.value(2).isNull() && works.value(2).toLongLong() == makerId)
        {
            ++result.alreadyMatched;
            continue;
        }
        update.bindValue(0, makerId);
        update.bindValue(1, works.value(0));
        if (!update.exec() || update.numRowsAffected() != 1)
        {
            result.errorMessage = update.lastError().text();
            return result;
        }
        ++result.updated;
    }
    if (!transaction.commit())
    {
        result.errorMessage = transaction.errorString();
        return result;
    }
    result.succeeded = true;
    return result;
}

CoverNormalizationResult WorkMaintenanceService::normalizeCoverFileNames() const
{
    CoverNormalizationResult result;
    const QString root = QDir(m_coverDirectory).canonicalPath();
    if (root.isEmpty())
    {
        result.errorMessage = QStringLiteral("封面目录不存在：%1").arg(m_coverDirectory);
        return result;
    }
    QSqlQuery works(m_database);
    if (!works.exec(QStringLiteral("SELECT work_id, serial_number, image_url FROM work "
                                   "WHERE image_url IS NOT NULL AND TRIM(image_url)<>''")))
    {
        result.errorMessage = works.lastError().text();
        return result;
    }
    struct CoverRecord final
    {
        qint64 id;
        QString serial;
        QString imageUrl;
    };
    QList<CoverRecord> records;
    while (works.next())
        records.append({works.value(0).toLongLong(), works.value(1).toString().trimmed(),
                        works.value(2).toString().trimmed()});

    Transaction transaction(m_database);
    if (!transaction.isActive())
    {
        result.errorMessage = transaction.errorString();
        return result;
    }
    QList<FileMove> moves;
    QSqlQuery update(m_database);
    update.prepare(QStringLiteral("UPDATE work SET image_url=? WHERE work_id=?"));
    for (const CoverRecord &record : std::as_const(records))
    {
        if (!isSafeSerialFileName(record.serial) || QFileInfo(record.imageUrl).isAbsolute())
        {
            ++result.invalidOrOutside;
            continue;
        }
        const QString oldPath = QFileInfo(QDir(root).filePath(record.imageUrl)).canonicalFilePath();
        if (oldPath.isEmpty())
        {
            ++result.missing;
            continue;
        }
        if (!isWithinDirectory(oldPath, root))
        {
            ++result.invalidOrOutside;
            continue;
        }
        const QString targetRelative = record.serial + QStringLiteral(".jpg");
        const QString newPath = QDir(root).filePath(targetRelative);
        if (!isWithinDirectory(QFileInfo(newPath).absoluteFilePath(), root))
        {
            ++result.invalidOrOutside;
            continue;
        }
        if (oldPath.compare(newPath, Qt::CaseInsensitive) == 0)
        {
            if (record.imageUrl == targetRelative)
            {
                ++result.alreadyNormalized;
                continue;
            }
            update.bindValue(0, targetRelative);
            update.bindValue(1, record.id);
            if (!update.exec() || update.numRowsAffected() != 1)
            {
                result.errorMessage = update.lastError().text();
                rollbackMoves(moves);
                return result;
            }
            ++result.databaseOnly;
            continue;
        }
        if (QFileInfo::exists(newPath))
        {
            ++result.conflict;
            continue;
        }
        if (!QFile::rename(oldPath, newPath))
        {
            ++result.renameFailed;
            continue;
        }
        moves.append({oldPath, newPath});
        update.bindValue(0, targetRelative);
        update.bindValue(1, record.id);
        if (!update.exec() || update.numRowsAffected() != 1)
        {
            result.errorMessage = update.lastError().text();
            if (!rollbackMoves(moves))
                result.errorMessage += QStringLiteral("；文件回滚失败");
            return result;
        }
        ++result.renamed;
    }
    if (!transaction.commit())
    {
        result.errorMessage = transaction.errorString();
        if (!rollbackMoves(moves))
            result.errorMessage += QStringLiteral("；文件回滚失败");
        return result;
    }
    result.succeeded = true;
    return result;
}

} // namespace darkeye
