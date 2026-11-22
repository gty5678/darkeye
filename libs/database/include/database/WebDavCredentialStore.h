#pragma once

#include <QString>

#include <optional>

namespace darkeye
{

struct WebDavCredentials final
{
    QString username;
    QString password;
};

class WebDavCredentialStore final
{
public:
    [[nodiscard]] static QString serviceName(const QString &profile);
    static bool save(const QString &profile, const WebDavCredentials &credentials,
                     QString *errorMessage = nullptr);
    [[nodiscard]] static std::optional<WebDavCredentials> load(const QString &profile,
                                                                 QString *errorMessage = nullptr);
    [[nodiscard]] static bool has(const QString &profile);
    static bool clear(const QString &profile, QString *errorMessage = nullptr);
};

} // namespace darkeye
