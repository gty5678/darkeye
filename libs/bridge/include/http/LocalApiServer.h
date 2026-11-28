#pragma once

#include <QJsonObject>
#include <QObject>
#include <QSet>
#include <QStringList>

#include <functional>
#include <memory>
#include <optional>

namespace darkeye
{

class LocalApiServer final : public QObject
{
    Q_OBJECT

public:
    using SerialLookup = std::function<std::optional<QSet<QString>>(
        const QStringList &normalizedSerials, QString *errorMessage)>;

    explicit LocalApiServer(SerialLookup serialLookup, QObject *parent = nullptr);
    ~LocalApiServer() override;

    bool start(quint16 port = 56789, QString *errorMessage = nullptr);
    void stop();
    [[nodiscard]] bool isListening() const noexcept;
    [[nodiscard]] quint16 port() const noexcept;

signals:
    /// Browser extension payloads are deliberately forwarded rather than interpreted here.
    void minnanoActressCaptureReceived(const QJsonObject &payload);
    void captureOneReceived(const QString &serialNumber);
    void crawlerBacklogWarning(int count, const QString &browser);
    void cloudflareChallengeReceived(const QString &site, const QString &serial);

private:
    class Private;
    std::unique_ptr<Private> d;
};

} // namespace darkeye
