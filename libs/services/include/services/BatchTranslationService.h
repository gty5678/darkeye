#pragma once

#include "settings/Settings.h"

#include <QObject>
#include <QSqlDatabase>
#include <functional>

namespace darkeye
{

enum class BatchTranslationMode
{
    FillMissing,
    ForceOverwrite,
};

struct BatchTranslationResult final
{
    bool succeeded = false;
    int total = 0;
    int updatedWorks = 0;
    int translatedTitles = 0;
    int translatedStories = 0;
    int failedFields = 0;
    qint64 elapsedMilliseconds = 0;
    QString errorMessage;

    [[nodiscard]] QString summary(BatchTranslationMode mode) const;
};

class LlmTranslationService;

class BatchTranslationService final : public QObject
{
    Q_OBJECT

public:
    using SettingsProvider = std::function<TranslationSettings()>;

    explicit BatchTranslationService(QSqlDatabase database, QObject *parent = nullptr);
    explicit BatchTranslationService(QSqlDatabase database, SettingsProvider settingsProvider,
                                     QObject *parent = nullptr);
    explicit BatchTranslationService(QSqlDatabase database, TranslationSettings settings,
                                     QObject *parent = nullptr);

    bool start(BatchTranslationMode mode);
    [[nodiscard]] bool isBusy() const noexcept;

signals:
    void progress(int done, int total, qint64 elapsedMilliseconds);
    void finished(const darkeye::BatchTranslationResult &result);

private:
    class Private;
    Private *d = nullptr;
};

} // namespace darkeye

Q_DECLARE_METATYPE(darkeye::BatchTranslationResult)
