#include "crawler/ManagedCollector.h"

#include <QFileInfo>
#include <QProcess>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace darkeye
{

ManagedCollector::ManagedCollector(QObject *parent) : QObject(parent), m_process(new QProcess(this))
{
    connect(m_process, &QProcess::errorOccurred, this,
            [this](QProcess::ProcessError) { emit failed(m_process->errorString()); });
    connect(m_process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
            [this](int, QProcess::ExitStatus) { emit stopped(); });
}

ManagedCollector::~ManagedCollector()
{
    stop();
}

bool ManagedCollector::start(const QString &executable, QString *errorMessage)
{
    if (isRunning()) return true;
    const QFileInfo file(executable.trimmed());
    if (file.filePath().isEmpty() || !file.exists() || !file.isFile())
    {
        if (errorMessage != nullptr)
            *errorMessage = QStringLiteral("Collector executable does not exist: %1").arg(file.filePath());
        return false;
    }
    m_process->setProgram(file.absoluteFilePath());
    m_process->setArguments({});
    m_process->setWorkingDirectory(file.absolutePath());
#ifdef Q_OS_WIN
    m_process->setCreateProcessArgumentsModifier(
        [](QProcess::CreateProcessArguments *arguments) {
            arguments->flags |= CREATE_NEW_CONSOLE;
            auto *startupInfo = reinterpret_cast<STARTUPINFOW *>(arguments->startupInfo);
            startupInfo->dwFlags &= ~STARTF_USESTDHANDLES;
            startupInfo->dwFlags |= STARTF_USESHOWWINDOW;
            startupInfo->wShowWindow = SW_SHOWMINNOACTIVE;
        });
#endif
    m_process->start();
    if (!m_process->waitForStarted(5000))
    {
        if (errorMessage != nullptr) *errorMessage = m_process->errorString();
        return false;
    }
    emit started(m_process->processId());
    return true;
}

void ManagedCollector::stop(int gracefulTimeoutMilliseconds)
{
    if (!isRunning()) return;
    m_process->terminate();
    if (m_process->waitForFinished(qMax(1, gracefulTimeoutMilliseconds))) return;
#ifdef Q_OS_WIN
    QProcess::execute(QStringLiteral("taskkill"),
                      {QStringLiteral("/PID"), QString::number(m_process->processId()),
                       QStringLiteral("/T"), QStringLiteral("/F")});
#else
    m_process->kill();
#endif
    m_process->waitForFinished(1000);
}

bool ManagedCollector::isRunning() const noexcept
{
    return m_process->state() != QProcess::NotRunning;
}

} // namespace darkeye
