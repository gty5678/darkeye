#pragma once

#include <QStringList>
#include <optional>

namespace darkeye::utils {

struct VideoFile
{
    QString path;
    std::optional<QString> serial;
};

struct VideoScanResult
{
    QStringList serials;
    QList<QPair<QString, QString>> filesWithoutSerial;
};

[[nodiscard]] QStringList defaultVideoExtensions();
[[nodiscard]] QList<VideoFile> collectVideos(
    const QStringList &folders, const QStringList &extensions = {});
[[nodiscard]] VideoScanResult videoNamesFromPaths(
    const QStringList &folders, const QStringList &extensions = {});
bool revealInFileManager(const QString &path);
bool playVideo(const QString &videoPath, const QString &configuredPlayer = {});

} // namespace darkeye::utils
