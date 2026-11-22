#include "settings/SettingsStore.h"

#include <QSettings>
#include <QFileInfo>

namespace darkeye
{

SettingsStore::SettingsStore(QString settingsFile) : m_fileName(std::move(settingsFile))
{
}

QVariant SettingsStore::value(const QString &key, const QVariant &defaultValue) const
{
    // 不缓存 QSettings，使下一次读取能够看到外部修改。
    QSettings settings(m_fileName, QSettings::IniFormat);
    return settings.value(key, defaultValue);
}

bool SettingsStore::exists() const
{
    return QFileInfo::exists(m_fileName);
}

void SettingsStore::setValue(const QString &key, const QVariant &value)
{
    QSettings settings(m_fileName, QSettings::IniFormat);
    settings.setValue(key, value);
    // 成功调用后立即持久化，无需等待 QSettings 析构。
    settings.sync();
}

void SettingsStore::setValues(const QVariantMap &values)
{
    QSettings settings(m_fileName, QSettings::IniFormat);
    for (auto it = values.cbegin(); it != values.cend(); ++it)
    {
        settings.setValue(it.key(), it.value());
    }
    // 批量完成后只同步一次，避免每个键都触发磁盘写入。
    settings.sync();
}

void SettingsStore::remove(const QString &key)
{
    QSettings settings(m_fileName, QSettings::IniFormat);
    settings.remove(key);
    // 与上方写入操作保持一致，立即同步删除结果。
    settings.sync();
}

const QString &SettingsStore::fileName() const noexcept
{
    return m_fileName;
}

} // namespace darkeye
