#include "services/LlmTranslationService.h"

#include <QHash>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>
#include <utility>

namespace darkeye
{

namespace
{

constexpr auto systemPrompt =
    "你是专业的日文到中文翻译引擎。只输出译文，不添加任何解释、前后缀、引号或注释。"
    "保留番号、系列名、人名、专有名词。若输入为空，返回空字符串。将淫荡的用语翻译到位";

constexpr auto actressNameSystemPrompt =
    "你是专业的日文姓名翻译引擎。"
    "输入是女优或艺人的日文名字，输出仅允许为一个中文名字。"
    "优先使用常见汉字译名；若无通行译名，使用自然、简洁的中文音译。"
    "不要输出解释、括号、前后缀、引号、注释或额外句子。"
    "若输入为空，返回空字符串。";

QString cleanedTranslation(QString value)
{
    value = value.trimmed();
    if (value.size() >= 2 && ((value.startsWith(u'\"') && value.endsWith(u'\"')) ||
                              (value.startsWith(u'\'') && value.endsWith(u'\''))))
        value = value.mid(1, value.size() - 2).trimmed();
    return value;
}

QString endpointFor(const TranslationSettings &settings)
{
    QString baseUrl = settings.baseUrl.trimmed();
    if (baseUrl.isEmpty())
        baseUrl = QStringLiteral("http://%1:%2/v1").arg(settings.llama.host).arg(settings.llama.port);
    while (baseUrl.endsWith(u'/'))
        baseUrl.chop(1);
    return baseUrl + QStringLiteral("/chat/completions");
}

} // namespace

class LlmTranslationService::Private final : public QObject
{
public:
    struct Pending final
    {
        QString source;
        QString destination;
        QString translationVariant;
        TranslationSettings settings;
        int attempt = 0;
    };

    explicit Private(LlmTranslationService *owner, SettingsProvider settingsProvider)
        : QObject(owner), owner(owner), settingsProvider(std::move(settingsProvider)), network(this)
    {
    }

    void complete(quint64 requestId, const QString &translation, const QString &error)
    {
        if (!pending.contains(requestId))
            return;
        pending.remove(requestId);
        emit owner->translationFinished(requestId, translation, error);
    }

    void send(quint64 requestId)
    {
        if (!pending.contains(requestId))
            return;
        const Pending state = pending.value(requestId);
        const TranslationSettings &settings = state.settings;
        const QUrl endpoint(endpointFor(settings));
        if (!endpoint.isValid() || (endpoint.scheme() != QStringLiteral("http") &&
                                    endpoint.scheme() != QStringLiteral("https")))
        {
            complete(requestId, {}, QStringLiteral("LLM Base URL 无效"));
            return;
        }
        if (settings.model.trimmed().isEmpty())
        {
            complete(requestId, {}, QStringLiteral("未配置 LLM 模型"));
            return;
        }

        QNetworkRequest request(endpoint);
        request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
        request.setRawHeader("Authorization", QByteArray("Bearer ") +
                                                (settings.apiKey.trimmed().isEmpty()
                                                     ? QByteArray("local")
                                                     : settings.apiKey.trimmed().toUtf8()));
        const bool actressName = state.translationVariant == QStringLiteral("actress_name");
        const QString userPrompt = actressName
            ? QStringLiteral("以下是日文艺人名，请翻译成%1，只输出中文名字：\n\n%2")
                  .arg(state.destination, state.source)
            : QStringLiteral("将以下文本翻译为 %1，只输出译文：\n\n%2")
                  .arg(state.destination, state.source);
        const QJsonArray messages{
            QJsonObject{{QStringLiteral("role"), QStringLiteral("system")},
                        {QStringLiteral("content"), QString::fromUtf8(
                             actressName ? actressNameSystemPrompt : systemPrompt)}},
            QJsonObject{{QStringLiteral("role"), QStringLiteral("user")},
                        {QStringLiteral("content"),
                         userPrompt}}};
        const QByteArray body = QJsonDocument(QJsonObject{
            {QStringLiteral("model"), settings.model.trimmed()},
            {QStringLiteral("temperature"), 0.1},
            {QStringLiteral("messages"), messages}}).toJson(QJsonDocument::Compact);
        QNetworkReply *reply = network.post(request, body);
        auto *timer = new QTimer(reply);
        timer->setSingleShot(true);
        connect(timer, &QTimer::timeout, reply, [reply]()
                { reply->setProperty("llmTimedOut", true); reply->abort(); });
        connect(reply, &QNetworkReply::finished, this, [this, requestId, reply]()
                {
                    const QByteArray response = reply->readAll();
                    QString error;
                    QString translation;
                    if (reply->property("llmTimedOut").toBool())
                        error = QStringLiteral("LLM 翻译超时");
                    else if (reply->error() != QNetworkReply::NoError)
                    {
                        const QJsonDocument document = QJsonDocument::fromJson(response);
                        error = document.object().value(QStringLiteral("error")).toObject()
                                    .value(QStringLiteral("message")).toString();
                        if (error.isEmpty()) error = reply->errorString();
                    }
                    else
                    {
                        QJsonParseError parseError;
                        const QJsonDocument document = QJsonDocument::fromJson(response, &parseError);
                        if (parseError.error != QJsonParseError::NoError || !document.isObject())
                            error = QStringLiteral("LLM 返回的 JSON 无效");
                        else
                            translation = cleanedTranslation(document.object()
                                .value(QStringLiteral("choices")).toArray().at(0).toObject()
                                .value(QStringLiteral("message")).toObject()
                                .value(QStringLiteral("content")).toString());
                    }
                    reply->deleteLater();
                    if (error.isEmpty()) { complete(requestId, translation, {}); return; }
                    if (!pending.contains(requestId)) return;
                    Pending &state = pending[requestId];
                    if (state.attempt >= state.settings.retries) { complete(requestId, {}, error); return; }
                    const int delayMilliseconds = 600 * (1 << qMin(state.attempt, 5));
                    ++state.attempt;
                    QTimer::singleShot(delayMilliseconds, this, [this, requestId]() { send(requestId); });
                });
        timer->start(qMax(1, settings.timeoutSeconds) * 1000);
    }

    LlmTranslationService *owner;
    SettingsProvider settingsProvider;
    QNetworkAccessManager network;
    QHash<quint64, Pending> pending;
    quint64 nextRequestId = 1;
};

LlmTranslationService::LlmTranslationService(QObject *parent)
    : LlmTranslationService([] { return settings::translation(); }, parent)
{
}

LlmTranslationService::LlmTranslationService(SettingsProvider settingsProvider, QObject *parent)
    : QObject(parent), d(new Private(this, std::move(settingsProvider)))
{
}

LlmTranslationService::LlmTranslationService(TranslationSettings settings, QObject *parent)
    : LlmTranslationService([settings = std::move(settings)] { return settings; }, parent)
{
}

quint64 LlmTranslationService::translate(const QString &source, const QString &destination)
{
    return translate(source, destination, QStringLiteral("default"));
}

quint64 LlmTranslationService::translate(const QString &source, const QString &destination,
                                       const QString &translationVariant)
{
    const quint64 requestId = d->nextRequestId++;
    if (source.trimmed().isEmpty())
    {
        QTimer::singleShot(0, this, [this, requestId]() { emit translationFinished(requestId, {}, {}); });
        return requestId;
    }
    d->pending.insert(requestId, Private::Pending{source.trimmed(), destination,
                                                translationVariant.trimmed(), d->settingsProvider(), 0});
    d->send(requestId);
    return requestId;
}

} // namespace darkeye
