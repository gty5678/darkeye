# WebDAV 系统凭据

C++ 版本使用系统后端保存凭据，沿用 Python `keyring` 版本的数据约定：

- service：`darkeye/webdav/<profile>`，profile 去除首尾空白，空值使用 `default`。
- account / username 属性：固定为 `credentials`。
- secret：包含 `username` 和 `password` 的 JSON；用户名去除首尾空白，密码保持原样。

| 平台 | 后端 | 构建及运行要求 |
| --- | --- | --- |
| Windows | Credential Manager | 链接系统 Advapi32；写入 UTF-16LE，同时读取旧 C++ UTF-8 数据 |
| macOS | Keychain，generic password | 链接系统 Security、CoreFoundation；系统可能要求授予钥匙串访问权限 |


macOS 使用与 Python 默认 Keychain 后端相同的查询字段，可读取该后端保存的凭据。Python 配置的第三方后端不在此兼容范围内。

测试目标为 `darkeye_webdav_credential_tests`。使用随机测试 profile 并在测试后清除，不使用真实 WebDAV 凭据。Windows 默认执行系统存取及 Python/旧 C++ 编码兼容测试；macOS 在桌面会话中设置 `DARKEYE_TEST_SYSTEM_KEYRING=1` 后执行系统存取测试。
