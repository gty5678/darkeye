#include "utils/TextUtils.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <algorithm>

namespace darkeye::utils {
namespace {
QJsonDocument readJson(const QString &filePath, QString *errorMessage)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        if (errorMessage) *errorMessage = file.errorString();
        return {};
    }
    QJsonParseError error;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &error);
    if (error.error != QJsonParseError::NoError && errorMessage)
        *errorMessage = error.errorString();
    return error.error == QJsonParseError::NoError ? document : QJsonDocument();
}
}

QStringList loadSensitiveWords(const QString &filePath, QString *errorMessage)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        if (errorMessage) *errorMessage = file.errorString();
        return {};
    }
    QStringList words;
    while (!file.atEnd()) {
        const QString word = QString::fromUtf8(file.readLine()).trimmed();
        if (!word.isEmpty()) words.append(word);
    }
    return words;
}

QVariantMap loadTagMap(const QString &filePath, QString *errorMessage)
{
    const QJsonDocument document = readJson(filePath, errorMessage);
    return document.isObject() ? document.object().toVariantMap() : QVariantMap();
}

QStringList tagNamesFromText(const QString &text, const QString &filePath, QString *errorMessage)
{
    const QVariantMap tagMap = loadTagMap(filePath, errorMessage);
    QStringList result;
    for (auto it = tagMap.cbegin(); it != tagMap.cend(); ++it)
    {
        const QStringList keywords = it.key().split(u'|');
        const bool matches = keywords.size() > 1
            ? std::all_of(keywords.cbegin(), keywords.cend(), [&text](const QString &keyword)
                          { return text.contains(keyword); })
            : text.contains(it.key());
        if (!matches)
            continue;

        // QString can convert to a list of characters; a mapped tag name is
        // one value, while a JSON array explicitly represents multiple tags.
        const QVariantList values = it.value().metaType().id() == QMetaType::QString
            ? QVariantList{it.value()} : it.value().toList();
        if (values.isEmpty())
        {
            const QString name = it.value().toString().trimmed();
            if (!name.isEmpty() && !result.contains(name))
                result.append(name);
            continue;
        }
        for (const QVariant &value : values)
        {
            const QString name = value.toString().trimmed();
            if (!name.isEmpty() && !result.contains(name))
                result.append(name);
        }
    }
    return result;
}

QSet<QString> loadExcludedGenres(const QString &filePath, QString *errorMessage)
{
    const QJsonDocument document = readJson(filePath, errorMessage);
    if (!document.isObject()) return {};
    QSet<QString> result;
    for (const QJsonValue &value : document.object().value(QStringLiteral("exclude_genre")).toArray()) {
        if (!value.isNull() && !value.isUndefined()) result.insert(value.toVariant().toString());
    }
    return result;
}

} // namespace darkeye::utils
