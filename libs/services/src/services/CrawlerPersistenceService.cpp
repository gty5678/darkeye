#include "services/CrawlerPersistenceService.h"

#include "database/SqliteConnection.h"
#include "database/repositories/PersonRepository.h"
#include "database/repositories/ReferenceRepository.h"
#include "database/repositories/WorkRepository.h"
#include "services/ImageFetchService.h"
#include "services/LlmTranslationService.h"
#include "settings/Paths.h"
#include "utils/TextUtils.h"

#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QUrl>
#include <algorithm>
#include <array>
#include <utility>

namespace darkeye
{

namespace
{

bool wants(const QSet<QString> &fields, const QString &field)
{
    return fields.isEmpty() || fields.contains(field);
}

QStringList strings(const QJsonObject &payload, const QString &key)
{
    QStringList result;
    for (const QJsonValue &value : payload.value(key).toArray())
    {
        const QString text = value.toString().trimmed();
        if (!text.isEmpty() && !result.contains(text))
            result.append(text);
    }
    return result;
}

std::optional<qint64> ensureReference(ReferenceRepository &repository, ReferenceKind kind,
                                      const QString &name, QString *errorMessage)
{
    const QString trimmed = name.trimmed();
    if (trimmed.isEmpty())
        return std::nullopt;
    std::optional<qint64> id = repository.findByName(kind, trimmed, errorMessage);
    if (!id.has_value() && (errorMessage == nullptr || errorMessage->isEmpty()))
        id = repository.create(kind, trimmed, errorMessage);
    return id;
}

QList<qint64> ensurePeople(PersonRepository &repository, PersonKind kind,
                           const QStringList &names, QString *errorMessage,
                           QList<qint64> *createdIds = nullptr)
{
    QList<qint64> ids;
    for (const QString &name : names)
    {
        std::optional<qint64> id = repository.findByName(kind, name, errorMessage);
        const bool missing = !id.has_value();
        if (missing && (errorMessage == nullptr || errorMessage->isEmpty()))
            id = repository.create(kind, {}, name, errorMessage);
        if (!id.has_value())
            return {};
        if (missing && createdIds != nullptr)
            createdIds->append(*id);
        ids.append(*id);
    }
    return ids;
}

QList<qint64> ensureTags(ReferenceRepository &repository, const QStringList &names,
                         QString *errorMessage)
{
    QList<TagRecord> records = repository.listTags(errorMessage);
    if (errorMessage != nullptr && !errorMessage->isEmpty())
        return {};
    QList<qint64> ids;
    for (const QString &name : names)
    {
        const auto found = std::find_if(records.cbegin(), records.cend(), [&name](const TagRecord &tag)
        { return tag.name == name || tag.aliases.contains(name); });
        std::optional<qint64> id;
        if (found != records.cend())
            id = found->id;
        else
            id = repository.createTag(name, std::nullopt, {}, {}, errorMessage);
        if (!id.has_value())
            return {};
        ids.append(*id);
    }
    return ids;
}

QList<qint64> existingTagIds(ReferenceRepository &repository, const QStringList &names,
                             QString *errorMessage)
{
    const QList<TagRecord> records = repository.listTags(errorMessage);
    if (errorMessage != nullptr && !errorMessage->isEmpty())
        return {};

    QList<qint64> ids;
    for (const QString &name : names)
    {
        const auto found = std::find_if(records.cbegin(), records.cend(), [&name](const TagRecord &tag)
        { return tag.name == name || tag.aliases.contains(name); });
        if (found != records.cend() && !ids.contains(found->id))
            ids.append(found->id);
    }
    return ids;
}

QString fanartJson(const QJsonObject &payload)
{
    QJsonArray entries;
    for (const QString &url : strings(payload, QStringLiteral("fanart_url_list")))
        entries.append(QJsonObject{{QStringLiteral("url"), url}, {QStringLiteral("file"), {}}});
    return entries.isEmpty()
        ? QString()
        : QString::fromUtf8(QJsonDocument(entries).toJson(QJsonDocument::Compact));
}

QMap<QString, bool> completeness(const WorkDetails &details)
{
    const Work &work = details.work;
    return {
        {QStringLiteral("cover"), !work.imageUrl.trimmed().isEmpty()},
        {QStringLiteral("actress"), !details.actresses.isEmpty()},
        {QStringLiteral("actor"), !details.actors.isEmpty()},
        {QStringLiteral("director"), !work.director.trimmed().isEmpty()},
        {QStringLiteral("release_date"), !work.releaseDate.trimmed().isEmpty()},
        {QStringLiteral("runtime"), work.runtime.value_or(0) > 0},
        {QStringLiteral("tag"), !details.tags.isEmpty()},
        {QStringLiteral("cn_title"), !work.chineseTitle.trimmed().isEmpty()},
        {QStringLiteral("jp_title"), !work.japaneseTitle.trimmed().isEmpty()},
        {QStringLiteral("cn_story"), !work.chineseStory.trimmed().isEmpty()},
        {QStringLiteral("jp_story"), !work.japaneseStory.trimmed().isEmpty()},
        {QStringLiteral("maker"), work.makerId.has_value()},
        {QStringLiteral("label"), work.labelId.has_value()},
        {QStringLiteral("series"), work.seriesId.has_value()},
        {QStringLiteral("fanart"), !work.fanartJson.trimmed().isEmpty()},
    };
}

} // namespace

CrawlerPersistenceService::CrawlerPersistenceService(QString databasePath, QString coverDirectory,
                                                     QUrl imageFetchEndpoint,
                                                     QObject *parent)
    : CrawlerPersistenceService(std::move(databasePath), std::move(coverDirectory),
                                std::move(imageFetchEndpoint),
                                [] { return settings::translation(); }, parent)
{
}

CrawlerPersistenceService::CrawlerPersistenceService(QString databasePath, QString coverDirectory,
                                                     QUrl imageFetchEndpoint,
                                                     TranslationSettings translationSettings,
                                                     QObject *parent)
    : CrawlerPersistenceService(std::move(databasePath), std::move(coverDirectory),
                                std::move(imageFetchEndpoint),
                                [settings = std::move(translationSettings)] { return settings; }, parent)
{
}

CrawlerPersistenceService::CrawlerPersistenceService(QString databasePath, QString coverDirectory,
                                                     QUrl imageFetchEndpoint,
                                                     SettingsProvider settingsProvider,
                                                     QObject *parent)
    : QObject(parent), m_databasePath(std::move(databasePath)),
      m_coverDirectory(std::move(coverDirectory)),
      m_imageFetch(new ImageFetchService(std::move(imageFetchEndpoint), this)),
      m_translation(new LlmTranslationService(std::move(settingsProvider), this))
{
    connect(m_translation, &LlmTranslationService::translationFinished, this,
            [this](quint64, const QString &translation, const QString &)
            {
                static constexpr std::array<std::pair<const char *, const char *>, 2> fields{{
                    {"jp_title", "cn_title"}, {"jp_story", "cn_story"}}};
                const auto [source, target] = fields.at(m_translationField);
                if (!translation.isEmpty())
                    m_pendingPayload.insert(QString::fromLatin1(target), translation);
                ++m_translationField;
                translateNextField();
            });
    connect(m_imageFetch, &ImageFetchService::requestFinished, this,
            [this](quint64, bool succeeded, const QString &, const QString &errorMessage)
            {
                if (m_activeSerial.isEmpty())
                    return;
                if (succeeded)
                {
                    const QString serial = m_activeSerial;
                    const qint64 workId = m_activeWorkId;
                    SqliteConnection connection;
                    QString updateError;
                    bool updated = connection.open(m_databasePath, false, &updateError);
                    if (updated)
                    {
                        WorkRepository works(connection.database());
                        const auto work = works.findById(workId, &updateError);
                        if (!work.has_value())
                            updated = false;
                        else
                        {
                            Work value = *work;
                            value.imageUrl = serial.toUpper() + QStringLiteral(".jpg");
                            updated = works.updateDetails(value, &updateError);
                        }
                        if (updated)
                        {
                            const auto details = works.findDetailsById(workId, &updateError);
                            if (details.has_value())
                                emit completenessChanged(serial, completeness(*details));
                        }
                    }
                    m_activeSerial.clear();
                    m_coverUrls.clear();
                    m_busy = false;
                    if (updated)
                        emit worksChanged();
                    emit finished(serial, updated, updateError);
                    return;
                }
                ++m_coverIndex;
                if (m_coverIndex < m_coverUrls.size())
                {
                    fetchNextCover();
                    return;
                }
                const QString serial = m_activeSerial;
                m_activeSerial.clear();
                m_coverUrls.clear();
                m_busy = false;
                // Metadata has already been committed.  Python treats an exhausted cover
                // sequence as a completed workflow rather than rolling it back.
                emit finished(serial, true, errorMessage);
            });
}

void CrawlerPersistenceService::persist(const QString &serialNumber, const QJsonObject &payload,
                                        const QSet<QString> &selectedFields)
{
    const QString serial = serialNumber.trimmed();
    if (serial.isEmpty() || m_busy)
        return;
    m_busy = true;
    m_pendingSerial = serial;
    m_pendingPayload = payload;
    m_pendingFields = selectedFields;
    m_translationField = 0;
    translateNextField();
}

void CrawlerPersistenceService::translateNextField()
{
    static constexpr std::array<std::pair<const char *, const char *>, 2> fields{{
        {"jp_title", "cn_title"}, {"jp_story", "cn_story"}}};
    while (m_translationField < static_cast<int>(fields.size()))
    {
        const auto [source, target] = fields.at(m_translationField);
        if (wants(m_pendingFields, QString::fromLatin1(target)))
        {
            const QString text = m_pendingPayload.value(QString::fromLatin1(source)).toString().trimmed();
            if (!text.isEmpty())
            {
                static_cast<void>(m_translation->translate(text));
                return;
            }
        }
        ++m_translationField;
    }
    persistTranslatedPayload();
}

void CrawlerPersistenceService::persistTranslatedPayload()
{
    const QString serial = m_pendingSerial;
    const QJsonObject payload = m_pendingPayload;
    const QSet<QString> selectedFields = m_pendingFields;
    QString errorMessage;
    qint64 workId = 0;
    QList<qint64> createdActressIds;
    if (!persistDatabase(serial, payload, selectedFields, &workId, &errorMessage,
                         &createdActressIds))
    {
        m_busy = false;
        emit finished(serial, false, errorMessage);
        return;
    }
    emit workPersisted(workId, serial);
    emit worksChanged();
    emit actressesChanged();
    emit actorsChanged();
    if (!createdActressIds.isEmpty())
        emit actressesCreated(createdActressIds);
    {
        SqliteConnection connection;
        QString completenessError;
        if (connection.open(m_databasePath, true, &completenessError))
        {
            WorkRepository works(connection.database());
            const auto details = works.findDetailsById(workId, &completenessError);
            if (details.has_value())
                emit completenessChanged(serial, completeness(*details));
        }
    }
    if (!wants(selectedFields, QStringLiteral("cover")))
    {
        m_busy = false;
        emit finished(serial, true, {});
        return;
    }
    m_coverUrls = strings(payload, QStringLiteral("cover_url_list"));
    if (m_coverUrls.isEmpty())
    {
        m_busy = false;
        emit finished(serial, true, {});
        return;
    }
    m_activeSerial = serial;
    m_activeWorkId = workId;
    m_coverIndex = 0;
    emit coverDownloadStarted(serial, m_coverUrls.size());
    fetchNextCover();
}

bool CrawlerPersistenceService::persistDatabase(const QString &serialNumber,
                                                const QJsonObject &payload,
                                                const QSet<QString> &selectedFields,
                                                qint64 *workId, QString *errorMessage,
                                                QList<qint64> *createdActressIds)
{
    SqliteConnection connection;
    if (!connection.open(m_databasePath, false, errorMessage))
        return false;
    WorkRepository works(connection.database());
    PersonRepository people(connection.database());
    ReferenceRepository references(connection.database());
    const auto existingId = works.findIdBySerial(serialNumber, errorMessage);
    if (errorMessage != nullptr && !errorMessage->isEmpty())
        return false;

    Work value;
    QList<qint64> actressIds;
    QList<qint64> actorIds;
    QList<qint64> tagIds;
    if (existingId.has_value())
    {
        const auto details = works.findDetailsById(*existingId, errorMessage);
        if (!details.has_value())
            return false;
        value = details->work;
        for (const WorkPersonReference &person : details->actresses) actressIds.append(person.id);
        for (const WorkPersonReference &person : details->actors) actorIds.append(person.id);
        for (const TagOption &tag : details->tags) tagIds.append(tag.id);
    }
    value.serialNumber = serialNumber;
    const auto text = [&payload](const QString &key) { return payload.value(key).toString(); };
    if (wants(selectedFields, QStringLiteral("director"))) value.director = text(QStringLiteral("director"));
    if (wants(selectedFields, QStringLiteral("release_date"))) value.releaseDate = text(QStringLiteral("release_date"));
    if (wants(selectedFields, QStringLiteral("runtime"))) value.runtime = payload.value(QStringLiteral("runtime")).toInt();
    if (wants(selectedFields, QStringLiteral("cn_title"))) value.chineseTitle = text(QStringLiteral("cn_title"));
    if (wants(selectedFields, QStringLiteral("jp_title"))) value.japaneseTitle = text(QStringLiteral("jp_title"));
    if (wants(selectedFields, QStringLiteral("cn_story"))) value.chineseStory = text(QStringLiteral("cn_story"));
    if (wants(selectedFields, QStringLiteral("jp_story"))) value.japaneseStory = text(QStringLiteral("jp_story"));
    if (wants(selectedFields, QStringLiteral("fanart"))) value.fanartJson = fanartJson(payload);
    if (wants(selectedFields, QStringLiteral("maker"))) value.makerId = ensureReference(references, ReferenceKind::Maker, text(QStringLiteral("maker")), errorMessage);
    if (wants(selectedFields, QStringLiteral("label"))) value.labelId = ensureReference(references, ReferenceKind::Label, text(QStringLiteral("label")), errorMessage);
    if (wants(selectedFields, QStringLiteral("series"))) value.seriesId = ensureReference(references, ReferenceKind::Series, text(QStringLiteral("series")), errorMessage);
    if (errorMessage != nullptr && !errorMessage->isEmpty()) return false;
    if (wants(selectedFields, QStringLiteral("actress")))
        actressIds = ensurePeople(people, PersonKind::Actress,
                                  strings(payload, QStringLiteral("actress_list")), errorMessage,
                                  createdActressIds);
    if (wants(selectedFields, QStringLiteral("actor"))) actorIds = ensurePeople(people, PersonKind::Actor, strings(payload, QStringLiteral("actor_list")), errorMessage);
    if (wants(selectedFields, QStringLiteral("tag")))
    {
        const QDir configDirectory(settings::Paths().configDirectory());
        // Preserve Python's title-derived tag rules. Mapped names are looked up
        // only: unlike crawler-provided genres, absent configured tags are not
        // created implicitly.
        const QString tagMapPath = configDirectory.filePath(QStringLiteral("tag_map.json"));
        const QList<qint64> mappedTagIds = existingTagIds(
            references, utils::tagNamesFromText(text(QStringLiteral("jp_title")), tagMapPath), errorMessage);
        if (errorMessage != nullptr && !errorMessage->isEmpty())
            return false;

        // Python filters crawler genres by exact name before resolving or
        // creating their tags. Keep title-derived mappings separate: they are
        // governed by tag_map.json rather than this exclusion list.
        const QSet<QString> excludedGenres = utils::loadExcludedGenres(
            configDirectory.filePath(QStringLiteral("exclude_genre.json")));
        QStringList crawlerTags;
        for (const QString &name : strings(payload, QStringLiteral("tag_list")))
            if (!excludedGenres.contains(name))
                crawlerTags.append(name);
        tagIds = ensureTags(references, crawlerTags, errorMessage);
        for (const qint64 tagId : mappedTagIds)
            if (!tagIds.contains(tagId))
                tagIds.append(tagId);
    }
    if (errorMessage != nullptr && !errorMessage->isEmpty()) return false;

    if (existingId.has_value())
    {
        if (!works.updateComplete(value, actressIds, actorIds, tagIds, errorMessage))
            return false;
        *workId = *existingId;
    }
    else
    {
        const auto created = works.insertComplete(value, actressIds, actorIds, tagIds, errorMessage);
        if (!created.has_value())
            return false;
        *workId = *created;
    }
    return true;
}

void CrawlerPersistenceService::fetchNextCover()
{
    const QString destination = QDir(m_coverDirectory).filePath(
        m_activeSerial.toUpper() + QStringLiteral(".jpg"));
    const quint64 requestId = m_imageFetch->fetchToJpeg(QUrl(m_coverUrls.at(m_coverIndex)), destination);
    Q_UNUSED(requestId);
}

} // namespace darkeye
