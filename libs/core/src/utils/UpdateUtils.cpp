#include "utils/UpdateUtils.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QUrl>

namespace darkeye::utils {

QString updateTarget()
{
#if defined(Q_OS_WIN)
    const QString system = QStringLiteral("windows");
#elif defined(Q_OS_MACOS)
    const QString system = QStringLiteral("macos");
#else
    const QString system = QStringLiteral("unsupported");
#endif
#if defined(Q_PROCESSOR_ARM_64)
    return system + QStringLiteral("-arm64");
#elif defined(Q_PROCESSOR_X86_64)
    return system + QStringLiteral("-x64");
#else
    return system + QStringLiteral("-unsupported");
#endif
}

std::optional<QList<int>> parseVersion(const QString &version)
{
    QString normalized = version.trimmed();
    if (normalized.isEmpty()) return std::nullopt;
    if (normalized.startsWith(QLatin1Char('v'), Qt::CaseInsensitive)) normalized.remove(0, 1);
    QList<int> parsed;
    for (const QString &part : normalized.split(QLatin1Char('.'), Qt::SkipEmptyParts)) {
        bool ok = false;
        const int value = part.trimmed().toInt(&ok);
        if (!ok) return std::nullopt;
        parsed.append(value);
    }
    return parsed.isEmpty() ? std::nullopt : std::optional<QList<int>>(parsed);
}

bool isNewerVersion(const QString &remoteVersion, const QString &localVersion)
{
    const auto remote = parseVersion(remoteVersion);
    const auto local = parseVersion(localVersion);
    if (!remote || !local) return remoteVersion.trimmed() != localVersion.trimmed();
    const qsizetype count = qMin(remote->size(), local->size());
    for (qsizetype index = 0; index < count; ++index) {
        const int remotePart = remote->at(index);
        const int localPart = local->at(index);
        if (remotePart != localPart) return remotePart > localPart;
    }
    return remote->size() > local->size();
}

UpdateCheckResult evaluateUpdateManifest(const QString &localVersion,
                                         const QByteArray &manifestJson)
{
    QJsonParseError error;
    const QJsonDocument document = QJsonDocument::fromJson(manifestJson, &error);
    if (error.error != QJsonParseError::NoError || !document.isObject()) {
        return {false, QStringLiteral("更新检查失败"),
                QStringLiteral("无法解析 latest.json：%1").arg(error.errorString())};
    }
    const QJsonObject root = document.object();
    const QString latest = root.value(QStringLiteral("latestVersion")).toVariant().toString().trimmed();
    const QString notes = root.value(QStringLiteral("releaseNotes")).toVariant().toString().trimmed();
    const QJsonObject package = root.value(QStringLiteral("packages")).toObject()
        .value(updateTarget()).toObject();
    const QString packageUrl = package.value(QStringLiteral("url")).toString().trimmed();
    const QString hash = package.value(QStringLiteral("sha256")).toString().trimmed();
    if (latest.isEmpty()) {
        return {false, QStringLiteral("更新检查结果"),
                QStringLiteral("latest.json 缺少 latestVersion 字段。")};
    }
    const QUrl url(packageUrl);
    bool validHash = hash.size() == 64 && hash != QString(64, u'0');
    for (const QChar c : hash)
        validHash = validHash && ((c >= u'0' && c <= u'9') ||
            (c.toLower() >= u'a' && c.toLower() <= u'f'));
    if (package.isEmpty() || !url.isValid() || url.scheme() != QStringLiteral("https") ||
        url.host().isEmpty() || !validHash) {
        return {false, QStringLiteral("更新检查失败"),
            QStringLiteral("更新清单缺少 %1 的有效 HTTPS 更新包或 SHA-256。").arg(updateTarget())};
    }
    const bool available = isNewerVersion(latest, localVersion);
    UpdateCheckResult result;
    result.success = true;
    result.title = QStringLiteral("更新检查结果");
    result.updateAvailable = available;
    result.latestVersion = latest;
    result.releaseNotes = notes;
    result.packageUrl = packageUrl;
    if (!available) {
        result.message = QStringLiteral("当前已是最新版本：%1。").arg(localVersion);
    } else {
        result.message = QStringLiteral("检测到新版本：%1 -> %2").arg(localVersion, latest);
        if (!notes.isEmpty()) result.message += QStringLiteral("\n更新内容：%1").arg(notes);
    }
    return result;
}

} // namespace darkeye::utils
