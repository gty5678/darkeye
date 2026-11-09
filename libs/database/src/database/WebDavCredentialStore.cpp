#include "database/WebDavCredentialStore.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <cstring>

#ifdef Q_OS_WIN
#include <windows.h>
#include <wincred.h>
#elif defined(Q_OS_MACOS)
#include <Security/Security.h>
#endif

namespace darkeye
{
namespace
{

QString normalizedProfile(const QString &profile)
{
    const QString value = profile.trimmed();
    return value.isEmpty() ? QStringLiteral("default") : value;
}

void setError(QString *errorMessage, const QString &message)
{
    if (errorMessage) *errorMessage = message;
}

#ifdef Q_OS_MACOS
CFMutableDictionaryRef keychainQuery(const QString &service)
{
    auto query = CFDictionaryCreateMutable(nullptr, 0, &kCFTypeDictionaryKeyCallBacks,
                                          &kCFTypeDictionaryValueCallBacks);
    const QByteArray utf8 = service.toUtf8();
    auto name = CFStringCreateWithBytes(nullptr, reinterpret_cast<const UInt8 *>(utf8.constData()),
                                        utf8.size(), kCFStringEncodingUTF8, false);
    CFDictionarySetValue(query, kSecClass, kSecClassGenericPassword);
    CFDictionarySetValue(query, kSecAttrService, name);
    CFDictionarySetValue(query, kSecAttrAccount, CFSTR("credentials"));
    CFRelease(name);
    return query;
}

bool keychainResult(OSStatus status, QString *errorMessage)
{
    if (status == errSecSuccess) return true;
    setError(errorMessage, QStringLiteral("系统 Keychain 操作失败（%1）。").arg(status));
    return false;
}
#endif

} // namespace

QString WebDavCredentialStore::serviceName(const QString &profile)
{
    return QStringLiteral("darkeye/webdav/") + normalizedProfile(profile);
}

bool WebDavCredentialStore::save(const QString &profile, const WebDavCredentials &credentials,
                                 QString *errorMessage)
{
    setError(errorMessage, {});
    if (credentials.username.trimmed().isEmpty() || credentials.password.isEmpty())
    {
        setError(errorMessage, QStringLiteral("用户名和密码不能为空。"));
        return false;
    }
    const QByteArray payload = QJsonDocument(QJsonObject{{QStringLiteral("username"), credentials.username.trimmed()},
                                                         {QStringLiteral("password"), credentials.password}})
                                   .toJson(QJsonDocument::Compact);
#ifdef Q_OS_WIN
    // keyring writes Windows credential blobs as UTF-16LE.
    const std::wstring password = QString::fromUtf8(payload).toStdWString();
    const std::wstring target = serviceName(profile).toStdWString();
    CREDENTIALW credential{};
    credential.Type = CRED_TYPE_GENERIC;
    credential.TargetName = const_cast<wchar_t *>(target.c_str());
    credential.CredentialBlobSize = static_cast<DWORD>(password.size() * sizeof(wchar_t));
    credential.CredentialBlob = reinterpret_cast<LPBYTE>(const_cast<wchar_t *>(password.data()));
    credential.Persist = CRED_PERSIST_LOCAL_MACHINE;
    credential.UserName = const_cast<wchar_t *>(L"credentials");
    if (CredWriteW(&credential, 0)) return true;
    setError(errorMessage, QStringLiteral("写入系统凭据管理器失败（%1）。").arg(GetLastError()));
    return false;
#elif defined(Q_OS_MACOS)
    auto query = keychainQuery(serviceName(profile));
    auto data = CFDataCreate(nullptr, reinterpret_cast<const UInt8 *>(payload.constData()), payload.size());
    auto attributes = CFDictionaryCreateMutable(nullptr, 0, &kCFTypeDictionaryKeyCallBacks,
                                                &kCFTypeDictionaryValueCallBacks);
    CFDictionarySetValue(attributes, kSecValueData, data);
    OSStatus status = SecItemUpdate(query, attributes);
    if (status == errSecItemNotFound)
    {
        CFDictionarySetValue(query, kSecValueData, data);
        status = SecItemAdd(query, nullptr);
    }
    CFRelease(attributes);
    CFRelease(data);
    CFRelease(query);
    return keychainResult(status, errorMessage);
#else
    Q_UNUSED(profile);
    setError(errorMessage, QStringLiteral("当前平台不支持系统凭据存储。"));
    return false;
#endif
}

std::optional<WebDavCredentials> WebDavCredentialStore::load(const QString &profile,
                                                               QString *errorMessage)
{
    setError(errorMessage, {});
    QByteArray payload;
#ifdef Q_OS_WIN
    const std::wstring target = serviceName(profile).toStdWString();
    PCREDENTIALW credential = nullptr;
    if (!CredReadW(target.c_str(), CRED_TYPE_GENERIC, 0, &credential))
    {
        const DWORD code = GetLastError();
        if (code != ERROR_NOT_FOUND)
            setError(errorMessage, QStringLiteral("读取系统凭据管理器失败（%1）。").arg(code));
        return std::nullopt;
    }
    payload = QByteArray(reinterpret_cast<const char *>(credential->CredentialBlob),
                         static_cast<qsizetype>(credential->CredentialBlobSize));
    CredFree(credential);
    // Continue reading UTF-8 blobs saved by earlier C++ releases.
    if (!QJsonDocument::fromJson(payload).isObject() && payload.size() % 2 == 0)
    {
        std::wstring text(static_cast<size_t>(payload.size() / 2), L'\0');
        std::memcpy(text.data(), payload.constData(), static_cast<size_t>(payload.size()));
        payload = QString::fromStdWString(text).toUtf8();
    }
#elif defined(Q_OS_MACOS)
    auto query = keychainQuery(serviceName(profile));
    CFDictionarySetValue(query, kSecMatchLimit, kSecMatchLimitOne);
    CFDictionarySetValue(query, kSecReturnData, kCFBooleanTrue);
    CFTypeRef result = nullptr;
    const OSStatus status = SecItemCopyMatching(query, &result);
    CFRelease(query);
    if (status == errSecItemNotFound) return std::nullopt;
    if (!keychainResult(status, errorMessage)) return std::nullopt;
    const auto data = static_cast<CFDataRef>(result);
    payload = QByteArray(reinterpret_cast<const char *>(CFDataGetBytePtr(data)), CFDataGetLength(data));
    CFRelease(result);
#else
    Q_UNUSED(profile);
    setError(errorMessage, QStringLiteral("当前平台不支持系统凭据存储。"));
    return std::nullopt;
#endif
    const QJsonDocument document = QJsonDocument::fromJson(payload);
    if (!document.isObject()) return std::nullopt;
    const QJsonObject object = document.object();
    const QString username = object.value(QStringLiteral("username")).toString().trimmed();
    const QString password = object.value(QStringLiteral("password")).toString();
    if (username.isEmpty() || password.isEmpty()) return std::nullopt;
    return WebDavCredentials{username, password};
}

bool WebDavCredentialStore::has(const QString &profile)
{
    return load(profile).has_value();
}

bool WebDavCredentialStore::clear(const QString &profile, QString *errorMessage)
{
    setError(errorMessage, {});
#ifdef Q_OS_WIN
    const std::wstring target = serviceName(profile).toStdWString();
    if (CredDeleteW(target.c_str(), CRED_TYPE_GENERIC, 0)) return true;
    if (GetLastError() == ERROR_NOT_FOUND) return true;
    setError(errorMessage, QStringLiteral("清除系统凭据失败（%1）。").arg(GetLastError()));
    return false;
#elif defined(Q_OS_MACOS)
    auto query = keychainQuery(serviceName(profile));
    const OSStatus status = SecItemDelete(query);
    CFRelease(query);
    return status == errSecItemNotFound || keychainResult(status, errorMessage);
#else
    Q_UNUSED(profile);
    setError(errorMessage, QStringLiteral("当前平台不支持系统凭据存储。"));
    return false;
#endif
}

} // namespace darkeye
