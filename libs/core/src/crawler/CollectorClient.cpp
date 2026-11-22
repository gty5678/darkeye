#include "crawler/CollectorClient.h"

#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPointer>
#include <QTimer>
#include <QUrlQuery>
#include <utility>

namespace darkeye
{

namespace
{

constexpr qsizetype maximumResponseBytes = 8 * 1024 * 1024;

QString jsonValueText(const QJsonValue &value)
{
    if (value.isString())
        return value.toString();
    if (value.isArray())
        return QString::fromUtf8(QJsonDocument(value.toArray()).toJson(QJsonDocument::Compact));
    if (value.isObject())
        return QString::fromUtf8(QJsonDocument(value.toObject()).toJson(QJsonDocument::Compact));
    return {};
}

QUrl itemUrl(const QUrl &baseUrl, const QString &item)
{
    QString encodedBase = baseUrl.toString(QUrl::FullyEncoded);
    while (encodedBase.endsWith(QChar('/')))
        encodedBase.chop(1);
    return QUrl(encodedBase + QChar('/') +
                QString::fromLatin1(QUrl::toPercentEncoding(item.trimmed())));
}

} // namespace

class CollectorClient::Private final : public QObject
{
public:
    struct Pending final
    {
        CollectorRequestKind kind = CollectorRequestKind::Work;
        QPointer<QNetworkReply> reply;
        QPointer<QTimer> timer;
        QByteArray response;
        bool tooLarge = false;
        bool timedOut = false;
        bool cancelled = false;
    };

    Private(CollectorClient *owner, QUrl workUrl, QUrl actressUrl, QUrl topUrl)
        : QObject(owner), owner(owner), workUrl(std::move(workUrl)),
          actressUrl(std::move(actressUrl)), topUrl(std::move(topUrl)), network(this)
    {
    }

    void finish(quint64 requestId)
    {
        if (!pending.contains(requestId))
            return;
        Pending state = pending.take(requestId);
        if (state.timer != nullptr)
            state.timer->stop();
        QNetworkReply *reply = state.reply;
        if (reply != nullptr)
        {
            state.response += reply->readAll();
            state.tooLarge = state.tooLarge || state.response.size() > maximumResponseBytes;
        }

        QJsonObject payload;
        QString errorMessage;
        int httpStatus = 0;
        bool succeeded = false;
        if (reply != nullptr)
            httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (state.cancelled)
            errorMessage = QStringLiteral("Collector 请求已取消");
        else if (state.timedOut)
            errorMessage = QStringLiteral("Collector 请求超时");
        else if (state.tooLarge)
            errorMessage = QStringLiteral("Collector 响应超过 8 MiB 限制");
        else if (reply == nullptr)
            errorMessage = QStringLiteral("Collector 连接已失效");
        else
        {
            QJsonParseError parseError;
            const QJsonDocument document = QJsonDocument::fromJson(state.response, &parseError);
            if (document.isObject())
                payload = document.object();
            if (reply->error() != QNetworkReply::NoError)
            {
                errorMessage = jsonValueText(payload.value(QStringLiteral("detail")));
                if (errorMessage.isEmpty())
                    errorMessage = reply->errorString();
            }
            else if (parseError.error != QJsonParseError::NoError || !document.isObject())
                errorMessage = QStringLiteral("Collector 返回的 JSON 无效");
            else
                succeeded = true;
        }
        if (reply != nullptr)
            reply->deleteLater();
        emit owner->requestFinished(requestId, state.kind, succeeded, payload, errorMessage,
                                    httpStatus);
    }

    CollectorClient *owner;
    QUrl workUrl;
    QUrl actressUrl;
    QUrl topUrl;
    QNetworkAccessManager network;
    QHash<quint64, Pending> pending;
    quint64 nextRequestId = 1;
    int timeoutMilliseconds = 130'000;
};

CollectorClient::CollectorClient(QUrl workApiBaseUrl, QUrl actressApiBaseUrl,
                                 QUrl topActressesApiUrl, QObject *parent)
    : QObject(parent), d(new Private(this, std::move(workApiBaseUrl), std::move(actressApiBaseUrl),
                                     std::move(topActressesApiUrl)))
{
    qRegisterMetaType<CollectorRequestKind>();
}

quint64 CollectorClient::fetchWork(const QString &serialNumber)
{
    return startGet(CollectorRequestKind::Work, itemUrl(d->workUrl, serialNumber));
}

quint64 CollectorClient::fetchActress(const QString &japaneseName, const QString &minnanoUrl)
{
    QUrl url = itemUrl(d->actressUrl, japaneseName);
    if (!minnanoUrl.trimmed().isEmpty())
    {
        QUrlQuery query(url);
        query.addQueryItem(QStringLiteral("minnano_url"), minnanoUrl.trimmed());
        url.setQuery(query);
    }
    return startGet(CollectorRequestKind::Actress, url);
}

quint64 CollectorClient::fetchTopActresses()
{
    return startGet(CollectorRequestKind::TopActresses, d->topUrl);
}

quint64 CollectorClient::startGet(CollectorRequestKind kind, const QUrl &url)
{
    const quint64 requestId = d->nextRequestId++;
    if (!url.isValid() ||
        (url.scheme() != QStringLiteral("http") && url.scheme() != QStringLiteral("https")))
    {
        QTimer::singleShot(0, this,
                           [this, requestId, kind]
                           {
                               emit requestFinished(requestId, kind, false, {},
                                                    QStringLiteral("Collector 地址无效"), 0);
                           });
        return requestId;
    }

    QNetworkReply *reply = d->network.get(QNetworkRequest(url));
    auto *timer = new QTimer(reply);
    timer->setSingleShot(true);
    Private::Pending state;
    state.kind = kind;
    state.reply = reply;
    state.timer = timer;
    d->pending.insert(requestId, state);
    connect(reply, &QIODevice::readyRead, this,
            [this, requestId]
            {
                if (!d->pending.contains(requestId))
                    return;
                Private::Pending &pending = d->pending[requestId];
                if (pending.reply == nullptr)
                    return;
                pending.response += pending.reply->readAll();
                if (pending.response.size() > maximumResponseBytes)
                {
                    pending.tooLarge = true;
                    pending.reply->abort();
                }
            });
    connect(reply, &QNetworkReply::finished, this, [this, requestId]() { d->finish(requestId); });
    connect(timer, &QTimer::timeout, this,
            [this, requestId]
            {
                if (!d->pending.contains(requestId))
                    return;
                d->pending[requestId].timedOut = true;
                if (d->pending[requestId].reply != nullptr)
                    d->pending[requestId].reply->abort();
            });
    timer->start(d->timeoutMilliseconds);
    emit requestStarted(requestId, kind);
    return requestId;
}

bool CollectorClient::cancel(quint64 requestId)
{
    if (!d->pending.contains(requestId))
        return false;
    d->pending[requestId].cancelled = true;
    if (d->pending[requestId].reply != nullptr)
        d->pending[requestId].reply->abort();
    return true;
}

void CollectorClient::cancelAll()
{
    const QList<quint64> requestIds = d->pending.keys();
    for (const quint64 requestId : requestIds)
        cancel(requestId);
}

void CollectorClient::setTimeoutMilliseconds(int timeoutMilliseconds)
{
    d->timeoutMilliseconds = qMax(1, timeoutMilliseconds);
}

int CollectorClient::timeoutMilliseconds() const noexcept
{
    return d->timeoutMilliseconds;
}

bool CollectorClient::isBusy() const noexcept
{
    return !d->pending.isEmpty();
}

} // namespace darkeye
