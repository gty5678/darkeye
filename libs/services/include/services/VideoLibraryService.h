#pragma once

#include <QList>
#include <QPair>
#include <QSqlDatabase>
#include <QStringList>

namespace darkeye {

struct VideoLibraryScanResult
{
    bool succeeded = false;
    int scannedFiles = 0;
    int unmatchedSerials = 0;
    int updatedWorks = 0;
    QStringList missingSerials;
    QList<QPair<QString, QString>> filesWithoutSerial;
    QString errorMessage;
};

class VideoLibraryService final
{
public:
    explicit VideoLibraryService(QSqlDatabase database);

    [[nodiscard]] VideoLibraryScanResult scanMissingSerials(
        const QStringList &folders);
    [[nodiscard]] VideoLibraryScanResult synchronizeVideoUrls(
        const QStringList &folders);

private:
    QSqlDatabase m_database;
};

} // namespace darkeye
