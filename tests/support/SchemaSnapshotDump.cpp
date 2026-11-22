#include "database/SchemaManager.h"
#include "database/SchemaSnapshot.h"
#include "database/SqliteConnection.h"

#include <QCoreApplication>
#include <QDir>
#include <QSaveFile>
#include <QTemporaryDir>
#include <QTextStream>

int main(int argc, char *argv[])
{
    QCoreApplication application(argc, argv);
    const QString kindArgument = application.arguments().value(1).trimmed().toLower();
    const darkeye::DatabaseKind kind =
        kindArgument == QStringLiteral("private") ? darkeye::DatabaseKind::Private
                                                  : darkeye::DatabaseKind::Public;

    QTemporaryDir directory;
    darkeye::SqliteConnection connection;
    QString errorMessage;
    const QString databasePath = QDir(directory.path()).filePath(
        kind == darkeye::DatabaseKind::Public ? QStringLiteral("public.db")
                                              : QStringLiteral("private.db"));
    if (!directory.isValid()
        || !connection.open(databasePath, false, &errorMessage)
        || !darkeye::SchemaManager::initializeEmptyDatabase(connection, kind,
                                                            &errorMessage)) {
        QTextStream(stderr) << errorMessage << Qt::endl;
        return 1;
    }

    const QJsonDocument snapshot =
        darkeye::SchemaSnapshot::capture(connection.database(), &errorMessage);
    if (snapshot.isNull()) {
        QTextStream(stderr) << errorMessage << Qt::endl;
        return 1;
    }
    const QByteArray json = snapshot.toJson(QJsonDocument::Indented);
    const QString outputPath = application.arguments().value(2);
    if (!outputPath.isEmpty()) {
        QSaveFile output(outputPath);
        if (!output.open(QIODevice::WriteOnly) || output.write(json) != json.size()
            || !output.commit()) {
            QTextStream(stderr) << QStringLiteral("无法写入快照：%1").arg(outputPath)
                                << Qt::endl;
            return 1;
        }
    } else {
        QTextStream(stdout) << json;
    }
    return 0;
}
