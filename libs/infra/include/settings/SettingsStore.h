#pragma once

#include <QString>
#include <QVariant>

namespace darkeye
{

/// 面向单个设置文件的轻量级 INI 键值存储。
///
/// 该对象仅持有文件路径。每次操作都会创建独立的 QSettings 实例，
/// 因此调用方可安全地创建生命周期很短的 SettingsStore 对象。
class SettingsStore final
{
public:
    /// 创建一个读写指定 INI 文件的存储对象。
    explicit SettingsStore(QString settingsFile);

    /// 返回 key 对应的值；若 key 不存在则返回 defaultValue。
    [[nodiscard]] QVariant value(const QString &key,
                                 const QVariant &defaultValue = {}) const;
    /// 返回底层设置文件当前是否存在。
    [[nodiscard]] bool exists() const;
    /// 写入单个值，并在返回前同步到磁盘。
    void setValue(const QString &key, const QVariant &value);
    /// 批量写入所有值，并同步到磁盘。
    void setValues(const QVariantMap &values);
    /// 移除 key，并将变更同步到磁盘。
    void remove(const QString &key);

    /// 返回构造时传入的底层 INI 文件路径。
    [[nodiscard]] const QString &fileName() const noexcept;

private:
    QString m_fileName;
};

} // namespace darkeye
