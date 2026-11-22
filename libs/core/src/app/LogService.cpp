#include "app/LogService.h"

#include <QDateTime>
#include <QFile>
#include <QMutex>
#include <QTextStream>

namespace {

QFile logFile;
QMutex logMutex;
QtMessageHandler previousHandler = nullptr;

QString levelName(QtMsgType type)
{
    switch (type) {
    case QtDebugMsg:
        return QStringLiteral("DEBUG");
    case QtInfoMsg:
        return QStringLiteral("INFO");
    case QtWarningMsg:
        return QStringLiteral("WARNING");
    case QtCriticalMsg:
        return QStringLiteral("CRITICAL");
    case QtFatalMsg:
        return QStringLiteral("FATAL");
    }
    return QStringLiteral("UNKNOWN");
}

void writeMessage(QtMsgType type, const QMessageLogContext &context, const QString &message)
{
    const QMutexLocker locker(&logMutex);
    if (logFile.isOpen()) {
        QTextStream stream(&logFile);
        stream << QDateTime::currentDateTime().toString(Qt::ISODateWithMs) << " ["
               << levelName(type) << "] " << message;
        if (context.file != nullptr) {
            stream << " (" << context.file << ':' << context.line << ')';
        }
        stream << Qt::endl;
    }

    if (previousHandler != nullptr) {
        previousHandler(type, context, message);
    }
}

} // namespace

namespace darkeye {

bool LogService::initialize(const QString &filePath)
{
    const QMutexLocker locker(&logMutex);
    if (logFile.isOpen()) {
        return true;
    }

    logFile.setFileName(filePath);
    if (!logFile.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
        return false;
    }
    previousHandler = qInstallMessageHandler(writeMessage);
    return true;
}

void LogService::shutdown()
{
    qInstallMessageHandler(previousHandler);
    previousHandler = nullptr;

    const QMutexLocker locker(&logMutex);
    logFile.close();
}

} // namespace darkeye

