#include "ui/pages/NfoImport.h"
#include "database/repositories/PersonRepository.h"
#include "database/repositories/ReferenceRepository.h"
#include "database/repositories/WorkRepository.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QXmlStreamReader>
#include <QImageReader>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QEventLoop>
#include <QTimer>
#include <QUrl>
#include <QSet>
#include <QRegularExpression>
#include <algorithm>
#include <utility>

namespace darkeye::nfo
{

namespace
{
// Preserve ElementTree's direct-child lookup and text before the first child.
struct Element final
{
    QString name, text, preview, defaultId;
    QList<Element> children;

    const Element *first(const QString &tag) const
    {
        for (const auto &child : children) if (child.name == tag) return &child;
        return nullptr;
    }
    QString value(const QString &tag) const
    {
        const auto *child = first(tag);
        return child ? child->text.trimmed() : QString{};
    }
};

Element readElement(QXmlStreamReader &xml)
{
    Element result;
    result.name = xml.namespaceUri().isEmpty() ? xml.name().toString()
        : QStringLiteral("{%1}%2").arg(xml.namespaceUri().toString(), xml.name().toString());
    result.preview = xml.attributes().value(QStringLiteral("preview")).toString().trimmed();
    result.defaultId = xml.attributes().value(QStringLiteral("default")).toString().trimmed();
    bool beforeChild = true;
    while (!xml.atEnd()) {
        xml.readNext();
        if (xml.isEndElement()) break;
        if (xml.isStartElement()) {
            beforeChild = false;
            result.children.append(readElement(xml));
        } else if (xml.isCharacters() && beforeChild) result.text += xml.text();
    }
    return result;
}

QString imageValue(const Element &node)
{
    const QString text = node.text.trimmed();
    return text.isEmpty() ? node.preview : text;
}
} // namespace

std::optional<ParsedNfo> parseNfo(const QString &path, bool mdcz, QString *error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        *error = QStringLiteral("无法读取文件：%1").arg(file.errorString());
        return std::nullopt;
    }
    QXmlStreamReader xml(&file);
    Element root;
    if (xml.readNextStartElement()) root = readElement(xml);
    // Consume the whole document to reject malformed XML after </movie> too.
    while (!xml.atEnd()) xml.readNext();
    if (xml.hasError()) {
        *error = QStringLiteral("XML 解析失败：%1").arg(xml.errorString());
        return std::nullopt;
    }
    if (root.name != QStringLiteral("movie")) {
        *error = QStringLiteral("根元素必须是 <movie>");
        return std::nullopt;
    }
    ParsedNfo result;
    result.serial = root.value(QStringLiteral("id"));
    if (result.serial.isEmpty()) result.serial = root.value(QStringLiteral("num"));
    if (mdcz && result.serial.isEmpty()) {
        QString fallback;
        for (const auto &node : root.children) {
            if (node.name != QStringLiteral("uniqueid")) continue;
            const QString value = node.text.trimmed();
            if (value.isEmpty()) continue;
            if (fallback.isEmpty()) fallback = value;
            if (node.defaultId.compare(QStringLiteral("true"), Qt::CaseInsensitive) == 0) {
                result.serial = value;
                break;
            }
        }
        if (result.serial.isEmpty()) result.serial = fallback;
    }
    if (result.serial.isEmpty()) {
        *error = mdcz ? QStringLiteral("NFO 中缺少番号（<id>/<num>/<uniqueid>）")
                      : QStringLiteral("NFO 中缺少番号（<id> 或 <num>）");
        return std::nullopt;
    }
    result.serial = result.serial.toUpper();
    result.title = root.value(QStringLiteral("title"));
    result.plot = root.value(QStringLiteral("plot"));
    result.director = root.value(QStringLiteral("director"));
    result.notes = root.value(QStringLiteral("source"));
    result.studio = root.value(QStringLiteral("studio"));
    result.releaseDate = root.value(QStringLiteral("premiered"));
    if (mdcz && result.releaseDate.isEmpty()) result.releaseDate = root.value(QStringLiteral("releasedate"));
    if (result.releaseDate.isEmpty()) result.releaseDate = root.value(QStringLiteral("release"));
    bool ok = false;
    const int runtime = root.value(QStringLiteral("runtime")).toInt(&ok);
    if (ok) result.runtime = runtime;
    QSet<QString> actorNames;
    for (const auto &node : root.children) {
        if (node.name == QStringLiteral("actor")) {
            const QString name = node.value(QStringLiteral("name"));
            if (!name.isEmpty() && (!mdcz || !actorNames.contains(name))) {
                result.cast.append({name, node.value(QStringLiteral("thumb"))});
                actorNames.insert(name);
            }
        } else if (node.name == QStringLiteral("genre") || node.name == QStringLiteral("tag")) {
            const QString value = node.text.trimmed();
            auto &names = node.name == QStringLiteral("genre") ? result.genres : result.tags;
            if (!value.isEmpty() && (!mdcz || !names.contains(value))) names.append(value);
        } else if (node.name == QStringLiteral("thumb")) {
            const QString value = imageValue(node);
            if (!value.isEmpty()) result.coverCandidates.append(value);
        }
    }
    if (mdcz) {
        if (const auto *set = root.first(QStringLiteral("set"))) {
            result.series = set->value(QStringLiteral("name"));
            if (result.series.isEmpty()) result.series = set->text.trimmed();
        }
        const auto *metadata = root.first(QStringLiteral("mdcz"));
        const auto *scene = metadata ? metadata->first(QStringLiteral("scene_images")) : nullptr;
        if (scene) for (const auto &image : scene->children) {
            if (image.name == QStringLiteral("image") && !image.text.trimmed().isEmpty())
                result.fanart.append({image.text.trimmed(), {}});
        }
    } else if (const auto *fanart = root.first(QStringLiteral("fanart"))) {
        for (const auto &thumb : fanart->children) {
            if (thumb.name != QStringLiteral("thumb")) continue;
            const QString value = imageValue(thumb);
            if (!value.isEmpty()) result.fanart.append({value, {}});
        }
    }
    return result;
}

bool isRemoteImage(const QString &source) { const QUrl url(source); return url.isValid() && (url.scheme() == QStringLiteral("http") || url.scheme() == QStringLiteral("https")); }
bool saveImageAsJpeg(const QString &source, const QString &destination)
{
    QImageReader reader(source); const QImage image = reader.read();
    return !image.isNull() && QDir().mkpath(QFileInfo(destination).absolutePath()) && image.save(destination, "JPEG", 90);
}
bool isBlockedImage(const QString &source)
{
    const QString lower = source.trimmed().toLower();
    return lower.startsWith(QStringLiteral("https://www.javsee.in"))
        || lower.startsWith(QStringLiteral("http://www.javsee.in"));
}
bool downloadImageAsJpeg(const QString &source, const QString &destination)
{
    if (isBlockedImage(source)) return false;
    QNetworkAccessManager manager; QNetworkReply *reply = manager.get(QNetworkRequest(QUrl(source))); QEventLoop loop; QTimer timeout; timeout.setSingleShot(true);
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit); QObject::connect(&timeout, &QTimer::timeout, reply, &QNetworkReply::abort); timeout.start(30000); loop.exec();
    const QByteArray body = reply->readAll(); const bool ok = reply->error() == QNetworkReply::NoError; reply->deleteLater(); const QImage image = QImage::fromData(body);
    return ok && !image.isNull() && QDir().mkpath(QFileInfo(destination).absolutePath()) && image.save(destination, "JPEG", 90);
}
QString resolveLocalImage(const QString &source)
{
    if (isRemoteImage(source)) return {}; if (QFileInfo(source).isFile()) return QFileInfo(source).absoluteFilePath();
    return {};
}
QString pickCover(const ParsedNfo &nfo)
{
    QStringList large, neutral, small, remote;
    for (const QString &candidate : nfo.coverCandidates) { if (isRemoteImage(candidate)) { remote.append(candidate); continue; } const QString local = resolveLocalImage(candidate); if (local.isEmpty()) continue; const QString lower = local.toLower().replace(u'\\', u'/'); if (lower.contains(QStringLiteral("bigpic")) || lower.contains(QStringLiteral("largepic")) || lower.contains(QStringLiteral("/large/"))) large.append(local); else if (lower.contains(QStringLiteral("smallpic")) || lower.contains(QStringLiteral("small_pic")) || lower.contains(QStringLiteral("/small/"))) small.append(local); else neutral.append(local); }
    if (!large.isEmpty()) { std::sort(large.begin(), large.end(), [](const QString &left, const QString &right) { return (left.contains(QStringLiteral("bigpic"), Qt::CaseInsensitive) ? 0 : 1) < (right.contains(QStringLiteral("bigpic"), Qt::CaseInsensitive) ? 0 : 1); }); return large.first(); }
    if (!neutral.isEmpty()) return neutral.first(); if (!small.isEmpty()) return small.first(); if (!remote.isEmpty()) return remote.first(); for (const auto &cast : nfo.cast) if (!cast.thumb.isEmpty()) return cast.thumb; return {};
}
QSet<QString> maleActorNames(const settings::Paths &paths)
{
    QFile file(QDir(paths.configDirectory()).filePath(QStringLiteral("actors_cn_jp_export.json"))); if (!file.open(QIODevice::ReadOnly)) return {};
    QSet<QString> names; for (const QJsonValue &value : QJsonDocument::fromJson(file.readAll()).array()) if (!value.toString().trimmed().isEmpty()) names.insert(value.toString().trimmed()); return names;
}
QString fanartJson(const QList<ParsedNfo::Fanart> &items) { QJsonArray array; for (const auto &item : items) if (!item.url.isEmpty() || !item.file.isEmpty()) array.append(QJsonObject{{QStringLiteral("url"), item.url}, {QStringLiteral("file"), item.file}}); return array.isEmpty() ? QString{} : QString::fromUtf8(QJsonDocument(array).toJson(QJsonDocument::Compact)); }

namespace
{
QString safeFanartStem(QString stem)
{
    static const QRegularExpression forbidden(QStringLiteral("[<>:\"/\\\\|?*\\x00-\\x1f]"));
    stem.replace(forbidden, QStringLiteral("_"));
    while (stem.startsWith(u' ') || stem.startsWith(u'.')) stem.remove(0, 1);
    while (stem.endsWith(u' ') || stem.endsWith(u'.')) stem.chop(1);
    return stem.isEmpty() ? QStringLiteral("fanart") : stem;
}

QList<ParsedNfo::Fanart> migrateMdczFanart(const QList<ParsedNfo::Fanart> &scenes,
                                         const QString &path, const settings::Paths &paths)
{
    struct Pair { QString url, name, source; };
    QList<Pair> pairs, unresolved;
    QList<ParsedNfo::Fanart> items;
    QSet<QString> seenUrls, seenNames, usedSources;
    const QDir sourceDir(QDir(QFileInfo(path).absolutePath()).filePath(QStringLiteral("extrafanart")));
    for (const auto &scene : scenes) {
        const QString basename = QFileInfo(QUrl(scene.url).path(QUrl::FullyDecoded)).fileName();
        const QString name = basename.isEmpty() || basename == QStringLiteral(".") || basename == QStringLiteral("..")
            ? QString{} : safeFanartStem(QFileInfo(basename).completeBaseName()) + QStringLiteral(".jpg");
        const QFileInfo source(sourceDir.filePath(name));
        if (!name.isEmpty() && source.isFile()) {
            if (!seenNames.contains(name)) {
                items.append({scene.url, name});
                pairs.append({scene.url, name, source.absoluteFilePath()});
                seenNames.insert(name);
                seenUrls.insert(scene.url);
                usedSources.insert(source.absoluteFilePath());
            }
        } else unresolved.append({scene.url, name, {}});
    }
    QFileInfoList freeFiles;
    const QSet<QString> extensions{QStringLiteral("jpg"), QStringLiteral("jpeg"), QStringLiteral("png"), QStringLiteral("webp")};
    for (const auto &file : sourceDir.entryInfoList(QDir::Files, QDir::NoSort))
        if (extensions.contains(file.suffix().toLower()) && !usedSources.contains(file.absoluteFilePath())) freeFiles.append(file);
    std::sort(freeFiles.begin(), freeFiles.end(), [](const QFileInfo &a, const QFileInfo &b) {
        return a.fileName().toLower() < b.fileName().toLower();
    });
    int index = 0;
    for (const auto &scene : unresolved) {
        if (seenUrls.contains(scene.url)) continue;
        QString name;
        if (index < freeFiles.size()) {
            const auto source = freeFiles.at(index++);
            name = scene.name.isEmpty() ? safeFanartStem(source.completeBaseName()) + QStringLiteral(".jpg") : scene.name;
            if (seenNames.contains(name)) name = safeFanartStem(source.completeBaseName()) + QStringLiteral("_%1.jpg").arg(index);
            pairs.append({scene.url, name, source.absoluteFilePath()});
            seenNames.insert(name);
        }
        items.append({scene.url, name});
        seenUrls.insert(scene.url);
    }
    // Python puts migrated entries first, followed by unresolved remote entries.
    QList<ParsedNfo::Fanart> migrated;
    QSet<QString> migratedUrls;
    for (const auto &pair : pairs) {
        const bool saved = saveImageAsJpeg(pair.source, QDir(paths.fanartDirectory()).filePath(pair.name));
        migrated.append({pair.url, saved ? pair.name : QString{}});
        migratedUrls.insert(pair.url);
        if (saved) QFile::remove(pair.source);
    }
    for (const auto &item : items) if (!migratedUrls.contains(item.url)) migrated.append(item);
    return migrated;
}
} // namespace

bool importNfo(QSqlDatabase database, const QString &path, bool mdcz, const settings::Paths &paths, QString *message)
{
    const auto parsed = parseNfo(path, mdcz, message); if (!parsed) return false;
    WorkRepository works(database); if (works.findIdBySerial(parsed->serial)) { *message = QStringLiteral("番号「%1」已在库中，已跳过导入。").arg(parsed->serial); return false; }
    ReferenceRepository references(database); PersonRepository people(database);
    const auto resolveReference = [&references](ReferenceKind kind, const QString &name) { if (name.isEmpty()) return std::optional<qint64>{}; const auto existing = references.findByName(kind, name); return existing ? existing : references.create(kind, name); };
    Work work; work.serialNumber = parsed->serial; work.japaneseTitle = parsed->title; work.japaneseStory = parsed->plot; work.director = parsed->director.isEmpty() ? QStringLiteral("----") : parsed->director; work.releaseDate = parsed->releaseDate; work.notes = parsed->notes; work.runtime = parsed->runtime;
    const QStringList studioParts = parsed->studio.split(u'/', Qt::SkipEmptyParts); if (!studioParts.isEmpty()) work.makerId = resolveReference(ReferenceKind::Maker, studioParts.first().trimmed()); if (studioParts.size() > 1) work.labelId = resolveReference(ReferenceKind::Label, studioParts.at(1).trimmed()); work.seriesId = resolveReference(ReferenceKind::Series, mdcz ? parsed->series : (parsed->tags.isEmpty() ? QString{} : parsed->tags.first()));
    QList<qint64> actressIds, actorIds, tagIds; const QSet<QString> maleNames = mdcz ? QSet<QString>{} : maleActorNames(paths);
    for (const auto &cast : parsed->cast) {
        auto id = people.findByName(PersonKind::Actress, cast.name);
        if (id) { if (!actressIds.contains(*id)) actressIds.append(*id); continue; }
        id = people.findByName(PersonKind::Actor, cast.name);
        if (id) { if (!actorIds.contains(*id)) actorIds.append(*id); continue; }
        const PersonKind kind = !mdcz && maleNames.contains(cast.name) ? PersonKind::Actor : PersonKind::Actress;
        people.create(kind, cast.name, cast.name);
        id = people.findByName(kind, cast.name);
        if (mdcz && !id) {
            if (people.create(PersonKind::Actor, cast.name, cast.name)) {
                id = people.findByName(PersonKind::Actor, cast.name);
                if (id && !actorIds.contains(*id)) actorIds.append(*id);
            }
        } else if (id) {
            auto &ids = kind == PersonKind::Actress ? actressIds : actorIds;
            if (!ids.contains(*id)) ids.append(*id);
        }
    }
    const QList<TagOption> existingTags = works.tagOptions(); QStringList tagNames = parsed->genres; if (mdcz) tagNames.append(parsed->tags);
    for (const QString &name : std::as_const(tagNames)) { if (name.isEmpty()) continue; std::optional<qint64> tagId; for (const TagOption &tag : existingTags) if (tag.name == name) { tagId = tag.id; break; } if (!tagId) tagId = references.createTag(name, 11, QStringLiteral("#cccccc"), {}); if (tagId && !tagIds.contains(*tagId)) tagIds.append(*tagId); }
    const QString cover = mdcz ? QDir(QFileInfo(path).absolutePath()).filePath(QStringLiteral("fanart.jpg")) : pickCover(*parsed); const QString localCover = isRemoteImage(cover) ? QString{} : resolveLocalImage(cover); if (!cover.isEmpty() && (isRemoteImage(cover) || !localCover.isEmpty())) { const QString destination = QDir(paths.workCoverDirectory()).filePath(parsed->serial + QStringLiteral(".jpg")); if ((isRemoteImage(cover) ? downloadImageAsJpeg(cover, destination) : saveImageAsJpeg(localCover, destination))) work.imageUrl = QFileInfo(destination).fileName(); }
    QList<ParsedNfo::Fanart> fanart = parsed->fanart;
    if (mdcz) fanart = migrateMdczFanart(fanart, path, paths);
    work.fanartJson = fanartJson(fanart); QString error; if (!works.insertComplete(work, actressIds, actorIds, tagIds, &error)) { *message = error.isEmpty() ? QStringLiteral("写入数据库失败") : error; return false; } *message = QStringLiteral("已从 NFO 导入作品：%1").arg(parsed->serial); return true;
}

} // namespace darkeye::nfo
