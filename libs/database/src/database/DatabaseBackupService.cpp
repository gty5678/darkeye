#include "database/DatabaseBackupService.h"

#include <QDir>
#include <QFileInfo>
#include <QSqlError>
#include <QSqlQuery>

namespace darkeye {

bool DatabaseBackupService::createConsistentBackup(const SqliteConnection &source,
                                                   const QString &destinationPath,
                                                   QString *errorMessage)
{
    if (!source.isOpen()) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("源数据库尚未打开");
        }
        return false;
    }

    QFileInfo destinationInfo(destinationPath);
    if (destinationInfo.exists()) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("拒绝覆盖已有备份：%1").arg(destinationPath);
        }
        return false;
    }
    if (!QDir().mkpath(destinationInfo.absolutePath())) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("无法创建备份目录：%1")
                                .arg(destinationInfo.absolutePath());
        }
        return false;
    }

    QString escapedPath = destinationInfo.absoluteFilePath();
    escapedPath.replace(QLatin1Char('\''), QStringLiteral("''"));
    QSqlQuery backupQuery(source.database());
    if (!backupQuery.exec(QStringLiteral("VACUUM main INTO '%1'").arg(escapedPath))) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("数据库备份失败：%1")
                                .arg(backupQuery.lastError().text());
        }
        return false;
    }

    destinationInfo.refresh();
    if (!destinationInfo.exists() || destinationInfo.size() <= 0) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("数据库备份未生成有效文件：%1")
                                .arg(destinationPath);
        }
        return false;
    }
    return true;
}

} // namespace darkeye
