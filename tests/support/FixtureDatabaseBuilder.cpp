#include "database/SchemaManager.h"
#include "database/SqlScriptRunner.h"
#include "database/SqliteConnection.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QSqlError>
#include <QSqlQuery>
#include <QTextStream>

namespace {

bool prepareTarget(const QString &path)
{
    QFile::remove(path);
    QFile::remove(path + QStringLiteral("-wal"));
    QFile::remove(path + QStringLiteral("-shm"));
    return true;
}

bool readScript(const QString &path, QString *script, QString *errorMessage)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        *errorMessage = file.errorString();
        return false;
    }
    *script = QString::fromUtf8(file.readAll());
    return true;
}

bool finalize(darkeye::SqliteConnection &connection, QString *errorMessage)
{
    QSqlQuery query(connection.database());
    if (!query.exec(QStringLiteral("PRAGMA wal_checkpoint(TRUNCATE)"))
        || !query.exec(QStringLiteral("PRAGMA journal_mode=DELETE"))) {
        *errorMessage = query.lastError().text();
        return false;
    }
    connection.close();
    return true;
}

bool buildCurrent(const QString &path, darkeye::DatabaseKind kind,
                  const QString &seedPath, QString *errorMessage)
{
    prepareTarget(path);
    darkeye::SqliteConnection connection;
    if (!connection.open(path, false, errorMessage)) {
        return false;
    }
    // The public golden fixture supplies its own reference data and IDs.
    const bool initialized = kind == darkeye::DatabaseKind::Public && !seedPath.isEmpty()
        ? darkeye::SqlScriptRunner::executeResource(connection.database(),
              QStringLiteral(":/sql/initPublicTable.sql"), errorMessage)
        : darkeye::SchemaManager::initializeEmptyDatabase(connection, kind, errorMessage);
    if (!initialized) {
        return false;
    }
    if (!seedPath.isEmpty()) {
        QString script;
        if (!readScript(seedPath, &script, errorMessage)
            || !darkeye::SqlScriptRunner::execute(connection.database(), script,
                                                  errorMessage)) {
            return false;
        }
    }
    return finalize(connection, errorMessage);
}

bool buildLegacy(const QString &path, darkeye::DatabaseKind kind,
                 QString *errorMessage)
{
    prepareTarget(path);
    darkeye::SqliteConnection connection;
    if (!connection.open(path, false, errorMessage)) {
        return false;
    }
    const QString resource =
        kind == darkeye::DatabaseKind::Public
            ? QStringLiteral(":/sql/fixtures/initPublicTable-v1.0.sql")
            : QStringLiteral(":/sql/fixtures/initPrivateTable-v1.0.sql");
    if (!darkeye::SqlScriptRunner::executeResource(connection.database(), resource,
                                                   errorMessage)) {
        return false;
    }
    return finalize(connection, errorMessage);
}

bool buildUnknown(const QString &path, QString *errorMessage)
{
    prepareTarget(path);
    darkeye::SqliteConnection connection;
    if (!connection.open(path, false, errorMessage)
        || !darkeye::SqlScriptRunner::execute(
            connection.database(),
            QStringLiteral("CREATE TABLE unexpected(id INTEGER PRIMARY KEY);"
                           "PRAGMA user_version=99;"),
            errorMessage)) {
        return false;
    }
    return finalize(connection, errorMessage);
}

bool buildForeignKeyCorruption(const QString &path, QString *errorMessage)
{
    prepareTarget(path);
    darkeye::SqliteConnection connection;
    if (!connection.open(path, false, errorMessage)
        || !darkeye::SchemaManager::initializeEmptyDatabase(
            connection, darkeye::DatabaseKind::Public, errorMessage)) {
        return false;
    }
    QSqlQuery query(connection.database());
    if (!query.exec(QStringLiteral("PRAGMA foreign_keys=OFF"))
        || !query.exec(QStringLiteral(
            "INSERT INTO work_actress_relation(work_id, actress_id) VALUES(999, 999)"))) {
        *errorMessage = query.lastError().text();
        return false;
    }
    return finalize(connection, errorMessage);
}

} // namespace

int main(int argc, char *argv[])
{
    QCoreApplication application(argc, argv);
    const QString outputRoot = application.arguments().value(1);
    if (outputRoot.isEmpty()) {
        QTextStream(stderr) << "usage: fixture-builder <output-directory>" << Qt::endl;
        return 2;
    }
    QDir output(outputRoot);
    if (!output.mkpath(QStringLiteral("."))) {
        QTextStream(stderr) << "cannot create output directory" << Qt::endl;
        return 2;
    }

    const QString seedRoot =
        QDir(QStringLiteral(DARKEYE_SOURCE_DIR)).filePath(
            QStringLiteral("tests/fixtures/seeds"));
    QString errorMessage;
    const auto build = [&](bool result, const QString &name) {
        if (!result) {
            QTextStream(stderr) << name << ": " << errorMessage << Qt::endl;
            return false;
        }
        return true;
    };

    if (!build(buildCurrent(output.filePath(QStringLiteral("public-empty-v2.db")),
                            darkeye::DatabaseKind::Public, {}, &errorMessage),
               QStringLiteral("public-empty-v2"))
        || !build(buildCurrent(output.filePath(QStringLiteral("private-empty-v1.1.db")),
                               darkeye::DatabaseKind::Private, {}, &errorMessage),
                  QStringLiteral("private-empty-v1.1"))
        || !build(buildCurrent(
                      output.filePath(QStringLiteral("public-typical-v2.db")),
                      darkeye::DatabaseKind::Public,
                      QDir(seedRoot).filePath(QStringLiteral("public-typical.sql")),
                      &errorMessage),
                  QStringLiteral("public-typical-v2"))
        || !build(buildCurrent(
                      output.filePath(QStringLiteral("private-typical-v1.1.db")),
                      darkeye::DatabaseKind::Private,
                      QDir(seedRoot).filePath(QStringLiteral("private-typical.sql")),
                      &errorMessage),
                  QStringLiteral("private-typical-v1.1"))
        || !build(buildLegacy(output.filePath(QStringLiteral("public-legacy-v1.0.db")),
                              darkeye::DatabaseKind::Public, &errorMessage),
                  QStringLiteral("public-legacy-v1.0"))
        || !build(buildLegacy(output.filePath(QStringLiteral("private-legacy-v1.0.db")),
                              darkeye::DatabaseKind::Private, &errorMessage),
                  QStringLiteral("private-legacy-v1.0"))
        || !build(buildUnknown(output.filePath(QStringLiteral("unknown-v99.db")),
                               &errorMessage),
                  QStringLiteral("unknown-v99"))
        || !build(buildForeignKeyCorruption(
                      output.filePath(QStringLiteral("public-corrupt-foreign-key.db")),
                      &errorMessage),
                  QStringLiteral("public-corrupt-foreign-key"))) {
        return 1;
    }
    return 0;
}
