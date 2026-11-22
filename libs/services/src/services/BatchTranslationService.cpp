#include "services/BatchTranslationService.h"

#include "services/LlmTranslationService.h"

#include <QElapsedTimer>
#include <QHash>
#include <QList>
#include <QQueue>
#include <QSqlError>
#include <QSqlQuery>
#include <QTimer>
#include <optional>
#include <utility>

namespace darkeye
{

namespace
{

enum class TranslationField
{
    Title,
    Story,
};

struct TranslationRow final
{
    qint64 workId = 0;
    int pendingFields = 0;
    QString title;
    QString story;
    bool updated = false;
};

struct TranslationTask final
{
    int rowIndex = -1;
    TranslationField field = TranslationField::Title;
    QString source;
};

} // namespace

class BatchTranslationService::Private final : public QObject
{
public:
    explicit Private(BatchTranslationService *owner, QSqlDatabase database,
                     SettingsProvider settingsProvider)
        : QObject(owner), owner(owner), database(std::move(database)),
          settingsProvider(std::move(settingsProvider))
    {
    }

    bool start(BatchTranslationMode requestedMode)
    {
        if (busy)
            return false;

        mode = requestedMode;
        result = {};
        rows.clear();
        tasks.clear();
        translators.clear();
        activeTasks.clear();
        activeRequests.clear();
        completedRows = 0;
        busy = true;
        timer.start();

        QSqlQuery query(database);
        const QString where = mode == BatchTranslationMode::FillMissing
            ? QStringLiteral("WHERE IFNULL(is_deleted, 0)=0 AND ((jp_title IS NOT NULL AND "
                             "TRIM(jp_title)<>'' AND (cn_title IS NULL OR TRIM(cn_title)='')) "
                             "OR (jp_story IS NOT NULL AND TRIM(jp_story)<>'' AND "
                             "(cn_story IS NULL OR TRIM(cn_story)='')))")
            : QStringLiteral("WHERE IFNULL(is_deleted, 0)=0 AND ((jp_title IS NOT NULL AND "
                             "TRIM(jp_title)<>'') OR (jp_story IS NOT NULL AND "
                             "TRIM(jp_story)<>''))");
        if (!query.exec(QStringLiteral("SELECT work_id,jp_title,cn_title,jp_story,cn_story FROM work ") +
                        where))
        {
            result.errorMessage = query.lastError().text();
            QTimer::singleShot(0, this, [this] { complete(); });
            return true;
        }

        while (query.next())
        {
            const QString jpTitle = query.value(1).toString().trimmed();
            const QString cnTitle = query.value(2).toString().trimmed();
            const QString jpStory = query.value(3).toString().trimmed();
            const QString cnStory = query.value(4).toString().trimmed();
            TranslationRow row;
            row.workId = query.value(0).toLongLong();
            const int rowIndex = rows.size();
            if (!jpTitle.isEmpty() &&
                (mode == BatchTranslationMode::ForceOverwrite || cnTitle.isEmpty()))
            {
                ++row.pendingFields;
                tasks.enqueue({rowIndex, TranslationField::Title, jpTitle});
            }
            if (!jpStory.isEmpty() &&
                (mode == BatchTranslationMode::ForceOverwrite || cnStory.isEmpty()))
            {
                ++row.pendingFields;
                tasks.enqueue({rowIndex, TranslationField::Story, jpStory});
            }
            if (row.pendingFields > 0)
                rows.append(std::move(row));
        }
        result.total = rows.size();
        if (result.total == 0)
        {
            result.succeeded = true;
            QTimer::singleShot(0, this, [this] { complete(); });
            return true;
        }

        const int workers = mode == BatchTranslationMode::ForceOverwrite ? 4 : 1;
        for (int index = 0; index < workers; ++index)
        {
            auto *translator = new LlmTranslationService(settingsProvider, this);
            translators.append(translator);
            activeTasks.append(std::nullopt);
            connect(translator, &LlmTranslationService::translationFinished, this,
                    [this, index](quint64 requestId, const QString &translation, const QString &error)
                    { handleTranslation(index, requestId, translation, error); });
            schedule(index);
        }
        emit owner->progress(0, result.total, 0);
        return true;
    }

    void schedule(int worker)
    {
        if (!busy || activeTasks.at(worker).has_value())
            return;
        if (tasks.isEmpty())
        {
            if (allDone())
                complete();
            return;
        }
        const TranslationTask task = tasks.dequeue();
        const quint64 requestId = translators.at(worker)->translate(task.source);
        activeTasks[worker] = task;
        activeRequests[worker] = requestId;
    }

    void handleTranslation(int worker, quint64 requestId, const QString &translation,
                           const QString &error)
    {
        if (!busy || !activeTasks.at(worker).has_value() ||
            activeRequests.value(worker) != requestId)
            return;
        const TranslationTask task = *activeTasks.at(worker);
        activeTasks[worker] = std::nullopt;
        TranslationRow &row = rows[task.rowIndex];
        if (error.isEmpty() && !translation.trimmed().isEmpty())
        {
            if (task.field == TranslationField::Title)
                row.title = translation;
            else
                row.story = translation;
        }
        else
        {
            ++result.failedFields;
        }
        --row.pendingFields;
        if (row.pendingFields == 0)
            persistRow(row);
        emit owner->progress(completedRows, result.total, timer.elapsed());
        schedule(worker);
    }

    void persistRow(TranslationRow &row)
    {
        QSqlQuery update(database);
        if (!row.title.isEmpty() && !row.story.isEmpty())
        {
            update.prepare(QStringLiteral("UPDATE work SET cn_title=?,cn_story=? WHERE work_id=?"));
            update.addBindValue(row.title);
            update.addBindValue(row.story);
        }
        else if (!row.title.isEmpty())
        {
            update.prepare(QStringLiteral("UPDATE work SET cn_title=? WHERE work_id=?"));
            update.addBindValue(row.title);
        }
        else if (!row.story.isEmpty())
        {
            update.prepare(QStringLiteral("UPDATE work SET cn_story=? WHERE work_id=?"));
            update.addBindValue(row.story);
        }
        if (!row.title.isEmpty() || !row.story.isEmpty())
        {
            update.addBindValue(row.workId);
            if (update.exec() && update.numRowsAffected() == 1)
            {
                row.updated = true;
                ++result.updatedWorks;
                if (!row.title.isEmpty()) ++result.translatedTitles;
                if (!row.story.isEmpty()) ++result.translatedStories;
            }
            else
            {
                if (result.errorMessage.isEmpty()) result.errorMessage = update.lastError().text();
                if (!row.title.isEmpty()) ++result.failedFields;
                if (!row.story.isEmpty()) ++result.failedFields;
            }
        }
        ++completedRows;
    }

    [[nodiscard]] bool allDone() const
    {
        if (!tasks.isEmpty() || completedRows != result.total)
            return false;
        for (const auto &active : activeTasks)
            if (active.has_value())
                return false;
        return true;
    }

    void complete()
    {
        if (!busy)
            return;
        busy = false;
        result.elapsedMilliseconds = timer.isValid() ? timer.elapsed() : 0;
        result.succeeded = result.errorMessage.isEmpty();
        emit owner->finished(result);
    }

    BatchTranslationService *owner;
    QSqlDatabase database;
    SettingsProvider settingsProvider;
    BatchTranslationMode mode = BatchTranslationMode::FillMissing;
    BatchTranslationResult result;
    QElapsedTimer timer;
    QList<TranslationRow> rows;
    QQueue<TranslationTask> tasks;
    QList<LlmTranslationService *> translators;
    QList<std::optional<TranslationTask>> activeTasks;
    QHash<int, quint64> activeRequests;
    int completedRows = 0;
    bool busy = false;
};

QString BatchTranslationResult::summary(BatchTranslationMode mode) const
{
    if (!succeeded)
        return errorMessage;
    if (mode == BatchTranslationMode::ForceOverwrite)
    {
        return QStringLiteral("共 %1 条待处理，已覆盖写入 %2 条作品；覆盖 cn_title %3 项，"
                              "cn_story %4 项；并发 worker=4；耗时 %5s")
            .arg(total)
            .arg(updatedWorks)
            .arg(translatedTitles)
            .arg(translatedStories)
            .arg(elapsedMilliseconds / 1000.0, 0, 'f', 1);
    }
    return QStringLiteral("共 %1 条待处理，已写入 %2 条作品；补充 cn_title %3 项，cn_story %4 项")
        .arg(total)
        .arg(updatedWorks)
        .arg(translatedTitles)
        .arg(translatedStories);
}

BatchTranslationService::BatchTranslationService(QSqlDatabase database, QObject *parent)
    : BatchTranslationService(std::move(database), [] { return settings::translation(); }, parent)
{
}

BatchTranslationService::BatchTranslationService(QSqlDatabase database, SettingsProvider settingsProvider,
                                                 QObject *parent)
    : QObject(parent), d(new Private(this, std::move(database), std::move(settingsProvider)))
{
    qRegisterMetaType<BatchTranslationResult>();
}

BatchTranslationService::BatchTranslationService(QSqlDatabase database, TranslationSettings settings,
                                                 QObject *parent)
    : BatchTranslationService(std::move(database),
                              [settings = std::move(settings)] { return settings; }, parent)
{
}

bool BatchTranslationService::start(BatchTranslationMode mode)
{
    return d->start(mode);
}

bool BatchTranslationService::isBusy() const noexcept
{
    return d->busy;
}

} // namespace darkeye
