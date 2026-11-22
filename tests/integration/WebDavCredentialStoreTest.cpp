#include "database/WebDavCredentialStore.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QUuid>
#include <QtTest>
#ifdef Q_OS_WIN
#include <windows.h>
#include <wincred.h>
#endif

class WebDavCredentialStoreTest final : public QObject
{
    Q_OBJECT
private slots:
    void profileNames();
    void roundTrip();
    void windowsCompatibility();
    void cleanup();
private:
    const QString m_profile = QStringLiteral("test-") + QUuid::createUuid().toString(QUuid::WithoutBraces);
};

void WebDavCredentialStoreTest::profileNames()
{
    using Store = darkeye::WebDavCredentialStore;
    QCOMPARE(Store::serviceName({}), QStringLiteral("darkeye/webdav/default"));
    QCOMPARE(Store::serviceName(QStringLiteral("  ")), QStringLiteral("darkeye/webdav/default"));
    QCOMPARE(Store::serviceName(QStringLiteral("  中文  ")), QStringLiteral("darkeye/webdav/中文"));
    QString error;
    QVERIFY(!Store::save(m_profile, {QStringLiteral(" "), QStringLiteral("password")}, &error));
    QVERIFY(!error.isEmpty());
    QVERIFY(!Store::save(m_profile, {QStringLiteral("user"), {}}, &error));
    QVERIFY(!error.isEmpty());
}

void WebDavCredentialStoreTest::roundTrip()
{
#ifndef Q_OS_WIN
    if (!qEnvironmentVariableIsSet("DARKEYE_TEST_SYSTEM_KEYRING"))
        QSKIP("Set DARKEYE_TEST_SYSTEM_KEYRING=1 in a desktop session to test the system keyring.");
#endif
    using Store = darkeye::WebDavCredentialStore;
    QString error = QStringLiteral("stale error");
    QVERIFY2(Store::clear(m_profile, &error), qPrintable(error));
    QVERIFY(error.isEmpty());
    QVERIFY(!Store::load(m_profile, &error));
    QVERIFY(error.isEmpty());
    const QString password = QStringLiteral(" 密码🔒\n\"\\ ");
    QVERIFY2(Store::save(m_profile, {QStringLiteral("  用户  "), password}, &error), qPrintable(error));
    QVERIFY(error.isEmpty());
    const auto result = Store::load(m_profile, &error);
    QVERIFY2(result.has_value(), qPrintable(error));
    QCOMPARE(result->username, QStringLiteral("用户"));
    QCOMPARE(result->password, password);
    QVERIFY(Store::has(m_profile));
    QVERIFY(!Store::has(m_profile + QStringLiteral("-other")));
    QVERIFY2(Store::save(m_profile, {QStringLiteral("updated"), QStringLiteral("new password")}, &error), qPrintable(error));
    const auto updated = Store::load(m_profile, &error);
    QVERIFY(updated.has_value());
    QCOMPARE(updated->password, QStringLiteral("new password"));
    QVERIFY2(Store::clear(m_profile, &error), qPrintable(error));
    QVERIFY(!Store::has(m_profile));
    QVERIFY2(Store::clear(m_profile, &error), qPrintable(error));
}

void WebDavCredentialStoreTest::windowsCompatibility()
{
#ifdef Q_OS_WIN
    using Store = darkeye::WebDavCredentialStore;
    const std::wstring target = Store::serviceName(m_profile).toStdWString();
    const QByteArray json = QJsonDocument(QJsonObject{{QStringLiteral("username"), QStringLiteral("用户")},
        {QStringLiteral("password"), QStringLiteral("密码🔒")}}).toJson(QJsonDocument::Compact);
    const std::wstring utf16 = QString::fromUtf8(json).toStdWString();
    const QByteArray pythonBlob(reinterpret_cast<const char *>(utf16.data()), static_cast<qsizetype>(utf16.size() * sizeof(wchar_t)));
    // Seed Python keyring's encoding, the old C++ encoding, and corrupt data.
    for (const QByteArray &blob : {pythonBlob, json, QByteArray("bad json"), QByteArray("{}")})
    {
        CREDENTIALW credential{};
        credential.Type = CRED_TYPE_GENERIC;
        credential.TargetName = const_cast<wchar_t *>(target.c_str());
        credential.UserName = const_cast<wchar_t *>(L"credentials");
        credential.Persist = CRED_PERSIST_LOCAL_MACHINE;
        credential.CredentialBlobSize = static_cast<DWORD>(blob.size());
        credential.CredentialBlob = reinterpret_cast<LPBYTE>(const_cast<char *>(blob.constData()));
        QVERIFY(CredWriteW(&credential, 0));
        const auto loaded = Store::load(m_profile);
        if (blob == pythonBlob || blob == json)
        {
            QVERIFY(loaded.has_value());
            QCOMPARE(loaded->username, QStringLiteral("用户"));
            QCOMPARE(loaded->password, QStringLiteral("密码🔒"));
        }
        else QVERIFY(!loaded);
    }
    QVERIFY(Store::save(m_profile, {QStringLiteral("user"), QStringLiteral("密码🔒")}));
    PCREDENTIALW stored = nullptr;
    QVERIFY(CredReadW(target.c_str(), CRED_TYPE_GENERIC, 0, &stored));
    const QString decoded = QString::fromWCharArray(reinterpret_cast<const wchar_t *>(stored->CredentialBlob),
        stored->CredentialBlobSize / sizeof(wchar_t));
    CredFree(stored);
    QCOMPARE(QJsonDocument::fromJson(decoded.toUtf8()).object().value(QStringLiteral("password")).toString(), QStringLiteral("密码🔒"));
#else
    QSKIP("Windows credential blob compatibility.");
#endif
}

void WebDavCredentialStoreTest::cleanup()
{
#ifdef Q_OS_WIN
    darkeye::WebDavCredentialStore::clear(m_profile);
#else
    if (qEnvironmentVariableIsSet("DARKEYE_TEST_SYSTEM_KEYRING"))
        darkeye::WebDavCredentialStore::clear(m_profile);
#endif
}

QTEST_GUILESS_MAIN(WebDavCredentialStoreTest)
#include "WebDavCredentialStoreTest.moc"
