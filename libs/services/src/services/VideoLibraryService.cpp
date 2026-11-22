#include "services/VideoLibraryService.h"

#include "utils/MediaUtils.h"

#include <QMap>
#include <QFileInfo>
#include <QSet>
#include <QSqlError>
#include <QSqlQuery>

namespace darkeye {
namespace {
QStringList cleanPaths(const QString &value)
{
    QStringList result;
    QSet<QString> seen;
    for (const QString &part : value.split(QLatin1Char(','), Qt::SkipEmptyParts)) {
        const QString path = part.trimmed();
        if (!path.isEmpty() && !seen.contains(path)) {
            seen.insert(path);
            result.append(path);
        }
    }
    return result;
}

void collectScanDetails(const QList<utils::VideoFile> &videos,
                        VideoLibraryScanResult &result)
{
    result.scannedFiles = videos.size();
    for (const utils::VideoFile &video : videos) {
        if (!video.serial)
            result.filesWithoutSerial.append({QFileInfo(video.path).completeBaseName(), video.path});
    }
}
}

VideoLibraryService::VideoLibraryService(QSqlDatabase database)
    : m_database(std::move(database))
{
}

VideoLibraryScanResult VideoLibraryService::scanMissingSerials(const QStringList &folders)
{
    VideoLibraryScanResult result;
    if (!m_database.isOpen()) {
        result.errorMessage = QStringLiteral("公共数据库尚未打开");
        return result;
    }
    const QList<utils::VideoFile> videos = utils::collectVideos(folders);
    collectScanDetails(videos, result);
    QSet<QString> existing;
    QSqlQuery query(m_database);
    if (!query.exec(QStringLiteral("SELECT serial_number FROM work"))) {
        result.errorMessage = query.lastError().text();
        return result;
    }
    while (query.next()) existing.insert(query.value(0).toString());
    QSet<QString> seen;
    for (const utils::VideoFile &video : videos) {
        if (video.serial && !existing.contains(*video.serial) && !seen.contains(*video.serial)) {
            seen.insert(*video.serial);
            result.missingSerials.append(*video.serial);
        }
    }
    result.succeeded = true;
    return result;
}

VideoLibraryScanResult VideoLibraryService::synchronizeVideoUrls(const QStringList &folders)
{
    VideoLibraryScanResult result;
    if (!m_database.isOpen()) {
        result.errorMessage = QStringLiteral("公共数据库尚未打开");
        return result;
    }
    const QList<utils::VideoFile> videos = utils::collectVideos(folders);
    collectScanDetails(videos, result);

    QMap<QString, qint64> workBySerial;
    QSqlQuery works(m_database);
    if (!works.exec(QStringLiteral(
            "SELECT serial_number, work_id FROM work WHERE IFNULL(is_deleted,0)=0"))) {
        result.errorMessage = works.lastError().text();
        return result;
    }
    while (works.next()) workBySerial.insert(works.value(0).toString(), works.value(1).toLongLong());

    QMap<qint64, QStringList> pathsByWork;
    for (const utils::VideoFile &video : videos) {
        if (!video.serial) continue;
        const auto found = workBySerial.constFind(*video.serial);
        if (found == workBySerial.cend()) {
            ++result.unmatchedSerials;
            continue;
        }
        QStringList &paths = pathsByWork[*found];
        if (!paths.contains(video.path)) paths.append(video.path);
    }

    if (!m_database.transaction()) {
        result.errorMessage = m_database.lastError().text();
        return result;
    }
    QSet<qint64> seenWorkIds;
    QSqlQuery select(m_database);
    select.prepare(QStringLiteral("SELECT video_url FROM work WHERE work_id=?"));
    QSqlQuery update(m_database);
    update.prepare(QStringLiteral("UPDATE work SET video_url=? WHERE work_id=?"));
    for (auto it = pathsByWork.cbegin(); it != pathsByWork.cend(); ++it) {
        select.bindValue(0, it.key());
        if (!select.exec() || !select.next()) continue;
        seenWorkIds.insert(it.key());
        const QString newValue = it.value().join(QLatin1Char(','));
        if (cleanPaths(select.value(0).toString()).join(QLatin1Char(',')) == newValue) continue;
        update.bindValue(0, newValue);
        update.bindValue(1, it.key());
        if (!update.exec()) {
            result.errorMessage = update.lastError().text();
            m_database.rollback();
            return result;
        }
        ++result.updatedWorks;
    }

    QString clearSql = QStringLiteral(
        "UPDATE work SET video_url=NULL WHERE video_url IS NOT NULL AND TRIM(video_url)<>''");
    if (!seenWorkIds.isEmpty()) {
        QStringList placeholders;
        for (qsizetype index = 0; index < seenWorkIds.size(); ++index)
            placeholders.append(QStringLiteral("?"));
        clearSql += QStringLiteral(" AND work_id NOT IN (%1)").arg(placeholders.join(QLatin1Char(',')));
    }
    QSqlQuery clear(m_database);
    clear.prepare(clearSql);
    int bind = 0;
    for (qint64 id : seenWorkIds) clear.bindValue(bind++, id);
    if (!clear.exec()) {
        result.errorMessage = clear.lastError().text();
        m_database.rollback();
        return result;
    }
    result.updatedWorks += qMax<qint64>(0, clear.numRowsAffected());
    if (!m_database.commit()) {
        result.errorMessage = m_database.lastError().text();
        m_database.rollback();
        return result;
    }
    result.succeeded = true;
    return result;
}

} // namespace darkeye
