#include "database/DatabaseManager.h"

#include "database/DatabaseBackupService.h"
#include "database/SchemaManager.h"

#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>

namespace darkeye {

bool DatabaseManager::initialize(const settings::Paths &paths, QString *errorMessage)
{
    if (!prepareDatabase(m_publicConnection, paths.publicDatabase(),
                         paths.publicBackupDirectory(), DatabaseKind::Public,
                         errorMessage)) {
        return false;
    }
    if (!prepareDatabase(m_privateConnection, paths.privateDatabase(),
                         paths.privateBackupDirectory(), DatabaseKind::Private,
                         errorMessage)) {
        m_publicConnection.close();
        return false;
    }
    return true;
}

bool DatabaseManager::initialize(QString *errorMessage)
{
    return initialize(settings::Paths{}, errorMessage);
}

SqliteConnection &DatabaseManager::publicConnection() noexcept
{
    return m_publicConnection;
}

SqliteConnection &DatabaseManager::privateConnection() noexcept
{
    return m_privateConnection;
}

bool DatabaseManager::prepareDatabase(SqliteConnection &connection,
                                      const QString &databasePath,
                                      const QString &backupDirectory,
                                      DatabaseKind kind,
                                      QString *errorMessage)
{
    const bool existedBeforeOpen = QFileInfo::exists(databasePath);
    if (!QDir().mkpath(QFileInfo(databasePath).absolutePath())) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("无法创建数据库目录：%1")
                                .arg(QFileInfo(databasePath).absolutePath());
        }
        return false;
    }
    if (!connection.open(databasePath, false, errorMessage)) {
        return false;
    }

    const QStringList userTables = connection.database().tables(QSql::Tables).filter(
        QRegularExpression(QStringLiteral("^(?!sqlite_).+")));
    if (existedBeforeOpen && !userTables.isEmpty()) {
        const QString currentVersion = connection.schemaVersion(errorMessage).trimmed();
        if (currentVersion != SchemaManager::requiredVersion(kind)) {
            const QString baseName = QFileInfo(databasePath).completeBaseName();
            const QString stamp = QDateTime::currentDateTime().toString(
                QStringLiteral("yyyyMMdd-HHmmsszzz"));
            const QString backupPath = QDir(backupDirectory).filePath(
                QStringLiteral("%1-pre-migration-%2.db").arg(baseName, stamp));
            if (!DatabaseBackupService::createConsistentBackup(connection, backupPath,
                                                               errorMessage)) {
                connection.close();
                return false;
            }
            qInfo() << "Created pre-migration database backup" << backupPath;
        }
    }

    if (!SchemaManager::migrateToCurrent(connection, kind, errorMessage)) {
        connection.close();
        return false;
    }
    return true;
}

} // namespace darkeye
