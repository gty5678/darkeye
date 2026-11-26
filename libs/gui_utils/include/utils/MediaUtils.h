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
[[nodiscard]] QStringList findVideos(
    const QString &serialNumber, const QStringList &folders,
    const QStringList &extensions = {});

bool deleteImage(const QString &path);
bool saveAsJpeg(const QString &inputPath, const QString &outputPath = {}, int quality = 95);
bool revealInFileManager(const QString &path);
bool playVideo(const QString &videoPath, const QString &configuredPlayer = {});

} // namespace darkeye::utils
