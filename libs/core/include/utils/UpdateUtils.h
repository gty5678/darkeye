#pragma once

#include <QByteArray>
#include <QString>
#include <optional>

namespace darkeye::utils {

struct UpdateCheckResult
{
    bool success = false;
    QString title;
    QString message;
    bool updateAvailable = false;
    QString latestVersion;
    QString releaseNotes;
    QString packageUrl;
};

[[nodiscard]] QString updateTarget();

[[nodiscard]] std::optional<QList<int>> parseVersion(const QString &version);
[[nodiscard]] bool isNewerVersion(const QString &remoteVersion,
                                  const QString &localVersion);
[[nodiscard]] UpdateCheckResult evaluateUpdateManifest(
    const QString &localVersion, const QByteArray &manifestJson);

} // namespace darkeye::utils
