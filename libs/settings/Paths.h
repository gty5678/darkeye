#pragma once

#include <QString>
#include <QStringList>

namespace darkeye::settings
{

/// 安装资源与可写数据的统一路径入口。
/// portable.ini 明确指定便携数据目录；无便携配置时使用系统用户数据目录。
class Paths final
{
public:
    /// 使用当前可执行文件目录检测运行模式。
    Paths();
    /// 启动时解析路径参数，供所有默认构造的 Paths 共用；相对参数以启动工作目录解析。
    static bool configureFromArguments(const QStringList &arguments, QString *errorMessage = nullptr);
    /// 指定程序目录；可选的数据目录覆盖用于测试和独立工具。
    explicit Paths(QString applicationDirectory, QString dataDirectoryOverride = {},
                   QString configDirectoryOverride = {}, QString wikiDirectoryOverride = {});
    [[nodiscard]] bool isPortable() const;

    /// 返回该对象使用的程序根目录。
    [[nodiscard]] const QString &applicationDirectory() const noexcept;
    /// 返回资源目录和运行数据根目录。
    [[nodiscard]] QString resourcesDirectory() const;
    [[nodiscard]] QString configDirectory() const;
    [[nodiscard]] QString dataDirectory() const;
    [[nodiscard]] QString cacheDirectory() const;
    [[nodiscard]] QString temporaryDirectory() const;
    [[nodiscard]] QString wikiDirectory() const;
    /// 随程序分发的只读知识库，仅用于首次初始化。
    [[nodiscard]] QString bundledWikiDirectory() const;
    /// 返回固定配置文件及各类 JSON、日志文件路径。
    [[nodiscard]] QString settingsFile() const;
    [[nodiscard]] QString publicDatabase() const;
    [[nodiscard]] QString privateDatabase() const;
    [[nodiscard]] QString publicBackupDirectory() const;
    [[nodiscard]] QString workCoverDirectory() const;
    [[nodiscard]] QString fanartDirectory() const;
    [[nodiscard]] QString actressImageDirectory() const;
    [[nodiscard]] QString actorImageDirectory() const;
    [[nodiscard]] QString privateBackupDirectory() const;
    /// 返回不支持自定义位置的运行配置文件路径。
    [[nodiscard]] QString shortcutsFile() const;
    [[nodiscard]] QString crawlerNavButtonsFile() const;
    [[nodiscard]] QString actressNavButtonsFile() const;
    [[nodiscard]] QString addWorkWorkspaceLayoutFile() const;
    [[nodiscard]] QString logFile() const;

    /// 创建运行所需的数据目录；失败时将错误原因写入 errorMessage。
    bool ensureRuntimeDirectories(QString *errorMessage = nullptr) const;

private:
    /// 查找程序目录或 macOS .app 外的 portable.ini。
    [[nodiscard]] QString portableConfigurationFile() const;
    [[nodiscard]] QString resourceRootDirectory() const;
    /// 将旧版相对程序目录的配置迁移到版本 2 的数据目录规则。
    bool migratePathSettings(QString *errorMessage) const;
    /// 从 settings.ini 读取路径；版本 2 的相对路径以数据目录为根。
    [[nodiscard]] QString configuredPath(const QString &key,
                                         const QString &defaultRelativePath) const;

    QString m_applicationDirectory;
    QString m_dataDirectoryOverride;
    QString m_configDirectoryOverride;
    QString m_wikiDirectoryOverride;
};

} // namespace darkeye::settings
