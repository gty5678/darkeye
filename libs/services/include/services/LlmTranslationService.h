#pragma once

#include "settings/Settings.h"

#include <QObject>
#include <QString>
#include <functional>

namespace darkeye
{

class LlmTranslationService final : public QObject
{
    Q_OBJECT

public:
    using SettingsProvider = std::function<TranslationSettings()>;

    // Read current application settings at each translate() call.
    explicit LlmTranslationService(QObject *parent = nullptr);
    explicit LlmTranslationService(SettingsProvider settingsProvider, QObject *parent = nullptr);
    explicit LlmTranslationService(TranslationSettings settings, QObject *parent = nullptr);

    [[nodiscard]] quint64 translate(const QString &source,
                                    const QString &destination = QStringLiteral("zh-CN"));
    [[nodiscard]] quint64 translate(const QString &source, const QString &destination,
                                    const QString &translationVariant);

signals:
    void translationFinished(quint64 requestId, const QString &translation,
                             const QString &errorMessage);

private:
    class Private;
    Private *d = nullptr;
};

} // namespace darkeye
