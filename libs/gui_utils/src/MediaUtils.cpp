#include "utils/MediaUtils.h"

#include "domain/SerialNumber.h"

#include <QDesktopServices>
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QProcess>
#include <QSet>
#include <QUrl>

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
