#include "utils/MediaUtils.h"

#include "domain/SerialNumber.h"

#include <QDesktopServices>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QPainter>
#include <QProcess>
#include <QSet>
#include <QUrl>
#include <algorithm>

namespace darkeye::utils {
namespace {
QStringList normalizedExtensions(const QStringList &extensions)
{
    QStringList result = extensions.isEmpty() ? defaultVideoExtensions() : extensions;
    for (QString &extension : result) {
        extension = extension.trimmed().toLower();
        if (!extension.startsWith(QLatin1Char('.'))) extension.prepend(QLatin1Char('.'));
    }
    return result;
}

QString absolutePath(const QFileInfo &info)
{
    const QString canonical = info.canonicalFilePath();
    return canonical.isEmpty() ? info.absoluteFilePath() : canonical;
}
}

QStringList defaultVideoExtensions()
{
    return {QStringLiteral(".mp4"), QStringLiteral(".avi"), QStringLiteral(".mkv"),
            QStringLiteral(".mov"), QStringLiteral(".wmv"), QStringLiteral(".flv"),
            QStringLiteral(".rmvb"), QStringLiteral(".ts")};
}

QList<VideoFile> collectVideos(const QStringList &folders, const QStringList &extensions)
{
    const QStringList accepted = normalizedExtensions(extensions);
    QList<VideoFile> videos;
    for (const QString &folder : folders) {
        QDirIterator iterator(folder, QDir::Files | QDir::Readable,
                              QDirIterator::Subdirectories);
        while (iterator.hasNext()) {
            const QFileInfo info(iterator.next());
            if (!accepted.contains(QStringLiteral(".") + info.suffix().toLower())) continue;
            videos.append({absolutePath(info), serial::extract(info.completeBaseName())});
        }
    }
    return videos;
}

VideoScanResult videoNamesFromPaths(const QStringList &folders, const QStringList &extensions)
{
    VideoScanResult result;
    QSet<QString> unique;
    for (const VideoFile &video : collectVideos(folders, extensions)) {
        if (video.serial.has_value()) unique.insert(*video.serial);
        else result.filesWithoutSerial.append({QFileInfo(video.path).completeBaseName(), video.path});
    }
    result.serials = unique.values();
    return result;
}

QStringList findVideos(const QString &serialNumber, const QStringList &folders,
                       const QStringList &extensions)
{
    const QStringList candidates{serialNumber, serial::convertFanza(serialNumber),
                                 serial::convertSpecial(serialNumber)};
    QStringList found;
    for (const VideoFile &video : collectVideos(folders, extensions)) {
        const QString fileName = QFileInfo(video.path).fileName();
        const bool matches = std::any_of(candidates.cbegin(), candidates.cend(),
            [&fileName](const QString &candidate) {
                return fileName.contains(candidate, Qt::CaseInsensitive);
            });
        if (matches) found.append(video.path);
    }
    return found;
}

bool deleteImage(const QString &path)
{
    const QFileInfo info(path);
    return !info.exists() || QFile::remove(path);
}

bool saveAsJpeg(const QString &inputPath, const QString &outputPath, int quality)
{
    QImage image(inputPath);
    if (image.isNull()) return false;
    if (image.hasAlphaChannel()) {
        QImage flattened(image.size(), QImage::Format_RGB32);
        flattened.fill(Qt::white);
        QPainter painter(&flattened);
        painter.drawImage(0, 0, image);
        painter.end();
        image = flattened;
    } else {
        image = image.convertToFormat(QImage::Format_RGB32);
    }
    QString destination = outputPath;
    if (destination.isEmpty()) {
        const QFileInfo info(inputPath);
        destination = info.dir().filePath(info.completeBaseName() + QStringLiteral(".jpg"));
    }
    return image.save(destination, "JPG", qBound(0, quality, 100));
}

bool revealInFileManager(const QString &path)
{
    const QFileInfo info(path);
#ifdef Q_OS_WIN
    return QProcess::startDetached(QStringLiteral("explorer.exe"),
                                   {QStringLiteral("/select,"), QDir::toNativeSeparators(info.absoluteFilePath())});
#elif defined(Q_OS_MACOS)
    return QProcess::startDetached(QStringLiteral("open"),
                                   {QStringLiteral("-R"), info.absoluteFilePath()});
#else
    return QDesktopServices::openUrl(QUrl::fromLocalFile(info.absolutePath()));
#endif
}

bool playVideo(const QString &videoPath, const QString &configuredPlayer)
{
    const QFileInfo video(videoPath);
    if (!video.isFile()) return false;
    const QFileInfo player(configuredPlayer);
    if (!configuredPlayer.trimmed().isEmpty() && player.isFile()) {
        return QProcess::startDetached(player.absoluteFilePath(), {video.absoluteFilePath()},
                                       video.absolutePath());
    }
    return QDesktopServices::openUrl(QUrl::fromLocalFile(video.absoluteFilePath()));
}

} // namespace darkeye::utils
