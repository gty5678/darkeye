#pragma once

#include "settings/Settings.h"
#include <QObject>
#include <QProcess>
#include <QStringList>

namespace darkeye {

// Application startup and settings pages observe and control the same process.
class LlamaRuntime final : public QObject
{
    Q_OBJECT
public:
    explicit LlamaRuntime(QObject *parent = nullptr);
    ~LlamaRuntime() override;
    bool isRunning() const;
    qint64 processId() const;
    QString status() const { return m_status; }
    QStringList logs() const { return m_logs; }
    static QStringList buildArguments(const LlamaCppSettings &settings);
    QString start(const LlamaCppSettings &settings);
    void stop();

signals:
    void statusChanged(const QString &status);
    void logAppended(const QString &text);
    void runningChanged(bool running);

private:
    void setStatus(const QString &status);
    void appendLog(const QString &text);
    QProcess m_process;
    QString m_status = QStringLiteral("未启动");
    QStringList m_logs;
};

LlamaRuntime &get_llama_runtime();

} // namespace darkeye
