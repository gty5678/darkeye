#pragma once

#include <QObject>

class QProcess;

namespace darkeye
{

/// Owns the optional external Collector process for the lifetime of Darkeye.
class ManagedCollector final : public QObject
{
    Q_OBJECT

public:
    explicit ManagedCollector(QObject *parent = nullptr);
    ~ManagedCollector() override;

    bool start(const QString &executable, QString *errorMessage = nullptr);
    void stop(int gracefulTimeoutMilliseconds = 3000);
    [[nodiscard]] bool isRunning() const noexcept;

signals:
    void started(qint64 processId);
    void stopped();
    void failed(const QString &errorMessage);

private:
    QProcess *m_process = nullptr;
};

} // namespace darkeye
