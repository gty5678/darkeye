#include "services/LlamaRuntime.h"

#include <QCoreApplication>
#include <QFileInfo>
#include <QPointer>

namespace darkeye {

LlamaRuntime::LlamaRuntime(QObject *parent) : QObject(parent)
{
    connect(&m_process, &QProcess::stateChanged, this, [this](QProcess::ProcessState state) {
        emit runningChanged(state != QProcess::NotRunning);
    });
    connect(&m_process, &QProcess::started, this, [this] {
        setStatus(QStringLiteral("运行中（PID %1）").arg(processId()));
        appendLog(QStringLiteral("llama-server 已启动，PID=%1").arg(processId()));
    });
    connect(&m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError) {
        setStatus(QStringLiteral("失败：") + m_process.errorString());
        appendLog(m_status);
    });
    connect(&m_process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
            [this](int code, QProcess::ExitStatus) {
        appendLog(QStringLiteral("llama-server 已退出，exit_code=%1").arg(code));
        setStatus(QStringLiteral("已停止"));
    });
    connect(&m_process, &QProcess::readyReadStandardOutput, this, [this] {
        appendLog(QString::fromLocal8Bit(m_process.readAllStandardOutput()));
    });
    connect(&m_process, &QProcess::readyReadStandardError, this, [this] {
        appendLog(QString::fromLocal8Bit(m_process.readAllStandardError()));
    });
    if (auto *app = QCoreApplication::instance())
        connect(app, &QCoreApplication::aboutToQuit, this, &LlamaRuntime::stop);
}

LlamaRuntime::~LlamaRuntime() { stop(); }
bool LlamaRuntime::isRunning() const { return m_process.state() != QProcess::NotRunning; }
qint64 LlamaRuntime::processId() const { return m_process.processId(); }

QStringList LlamaRuntime::buildArguments(const LlamaCppSettings &settings)
{
    QStringList args{QStringLiteral("-m"), settings.modelPath.trimmed(),
                     QStringLiteral("--host"), settings.host.trimmed().isEmpty()
                         ? QStringLiteral("127.0.0.1") : settings.host.trimmed(),
                     QStringLiteral("--port"), QString::number(settings.port),
                     QStringLiteral("-c"), QString::number(settings.contextSize),
                     QStringLiteral("-t"), QString::number(settings.threads),
                     QStringLiteral("-tb"), QString::number(settings.threadsBatch),
                     QStringLiteral("-b"), QString::number(settings.batchSize),
                     QStringLiteral("-ub"), QString::number(settings.microBatchSize)};
    args << QStringLiteral("-ngl") << QString::number(
        settings.mode.trimmed().toLower() == QStringLiteral("gpu") ? settings.gpuLayers : 0);
    if (settings.mlock) args << QStringLiteral("--mlock");
    return args;
}

QString LlamaRuntime::start(const LlamaCppSettings &settings)
{
    // A pending auto-start must not launch a second process after a manual start.
    if (isRunning()) return {};
    const QString executable = settings.serverExecutable.trimmed();
    if (executable.isEmpty() || settings.modelPath.trimmed().isEmpty())
        return QStringLiteral("请先选择 llama-server 和 GGUF 模型。");
    m_process.setProgram(executable);
    m_process.setArguments(buildArguments(settings));
    m_process.setWorkingDirectory(QFileInfo(executable).absolutePath());
    setStatus(QStringLiteral("启动中"));
    appendLog(QStringLiteral("正在启动 llama-server …"));
    m_process.start();
    return {};
}

void LlamaRuntime::stop()
{
    if (!isRunning()) return;
    // Resolve a pending start before attempting to terminate it.
    if (m_process.state() == QProcess::Starting && !m_process.waitForStarted(2000)) return;
#ifdef Q_OS_WIN
    const qint64 pid = processId();
#endif
    m_process.terminate();
    if (!m_process.waitForFinished(3000)) {
#ifdef Q_OS_WIN
        if (pid > 0)
            QProcess::execute(QStringLiteral("taskkill"),
                              {QStringLiteral("/PID"), QString::number(pid),
                               QStringLiteral("/T"), QStringLiteral("/F")});
#endif
        if (isRunning()) m_process.kill();
        m_process.waitForFinished(1000);
    }
    if (!isRunning()) {
        setStatus(QStringLiteral("已停止"));
        appendLog(QStringLiteral("已停止 llama-server。"));
    }
}

void LlamaRuntime::setStatus(const QString &status)
{
    m_status = status;
    emit statusChanged(status);
}

void LlamaRuntime::appendLog(const QString &text)
{
    for (const QString &line : text.split(u'\n', Qt::SkipEmptyParts)) {
        if (!line.trimmed().isEmpty()) m_logs.append(line.trimmed());
    }
    constexpr qsizetype maxLines = 120;
    if (m_logs.size() > maxLines) m_logs = m_logs.sliced(m_logs.size() - maxLines);
    emit logAppended(text);
}

LlamaRuntime &get_llama_runtime()
{
    static QPointer<LlamaRuntime> runtime;
    if (!runtime) runtime = new LlamaRuntime(QCoreApplication::instance());
    return *runtime;
}

} // namespace darkeye
