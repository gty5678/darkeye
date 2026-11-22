#include "database/SqliteConnection.h"

#include <QFileInfo>
#include <QSqlError>
#include <QSqlQuery>
#include <QUuid>

namespace darkeye {

SqliteConnection::SqliteConnection()
    : m_connectionName(QStringLiteral("darkeye-%1")
                           .arg(QUuid::createUuid().toString(QUuid::WithoutBraces)))
{
}

SqliteConnection::~SqliteConnection()
{
    close();
}

bool SqliteConnection::open(const QString &databasePath, bool readOnly,
                            QString *errorMessage)
{
    close();

    if (readOnly && !QFileInfo::exists(databasePath)) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("只读数据库不存在：%1").arg(databasePath);
        }
        return false;
    }

    m_database = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), m_connectionName);
    m_databasePath = QFileInfo(databasePath).absoluteFilePath();
    m_database.setDatabaseName(m_databasePath);
    m_database.setConnectOptions(readOnly
                                     ? QStringLiteral("QSQLITE_OPEN_READONLY;QSQLITE_BUSY_TIMEOUT=5000")
                                     : QStringLiteral("QSQLITE_BUSY_TIMEOUT=5000"));

    if (!m_database.open()) {
        if (errorMessage != nullptr) {
            *errorMessage = m_database.lastError().text();
        }
        close();
        return false;
    }

    m_readOnly = readOnly;
    if (!executePragma(QStringLiteral("PRAGMA foreign_keys=ON"), errorMessage)
        || !executePragma(QStringLiteral("PRAGMA busy_timeout=5000"), errorMessage)) {
        close();
        return false;
    }

    if (!readOnly && !executePragma(QStringLiteral("PRAGMA journal_mode=WAL"), errorMessage)) {
        close();
        return false;
    }

    return true;
}

void SqliteConnection::close()
{
    if (m_database.isValid()) {
        m_database.close();
        m_database = QSqlDatabase();
        QSqlDatabase::removeDatabase(m_connectionName);
    }
    m_readOnly = false;
    m_databasePath.clear();
}

bool SqliteConnection::isOpen() const
{
    return m_database.isValid() && m_database.isOpen();
}

bool SqliteConnection::isReadOnly() const noexcept
{
    return m_readOnly;
}

const QString &SqliteConnection::databasePath() const noexcept
{
    return m_databasePath;
}

QSqlDatabase SqliteConnection::database() const
{
    return m_database;
}

QString SqliteConnection::schemaVersion(QString *errorMessage) const
{
    if (!isOpen()) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("数据库尚未打开");
        }
        return {};
    }

    QSqlQuery userVersionQuery(m_database);
    if (!userVersionQuery.exec(QStringLiteral("PRAGMA user_version"))
        || !userVersionQuery.next()) {
        if (errorMessage != nullptr) {
            *errorMessage = userVersionQuery.lastError().text();
        }
        return {};
    }

    const int userVersion = userVersionQuery.value(0).toInt();
    if (userVersion != 0) {
        return QString::number(userVersion);
    }

    QSqlQuery legacyVersionQuery(m_database);
    if (!legacyVersionQuery.exec(QStringLiteral(
            "SELECT version FROM db_version ORDER BY applied_at DESC LIMIT 1"))) {
        if (errorMessage != nullptr) {
            *errorMessage = legacyVersionQuery.lastError().text();
        }
        return {};
    }
    return legacyVersionQuery.next() ? legacyVersionQuery.value(0).toString() : QString();
}

bool SqliteConnection::executePragma(const QString &statement, QString *errorMessage)
{
    QSqlQuery query(m_database);
    if (query.exec(statement)) {
        return true;
    }
    if (errorMessage != nullptr) {
        *errorMessage = query.lastError().text();
    }
    return false;
}

} // namespace darkeye
