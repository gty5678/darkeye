#include "services/ImageFetchService.h"

#include <QDir>
#include <QFileInfo>
#include <QHash>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPainter>
#include <QPointer>
#include <QSaveFile>
#include <QTimer>
#include <utility>

namespace darkeye
{

namespace
{

constexpr qsizetype maximumResponseBytes = 64 * 1024 * 1024;

bool writeJpeg(const QByteArray &bytes, const QString &destinationPath, QString *errorMessage)
{
    QImage image;
    if (!image.loadFromData(bytes))
    {
        if (errorMessage != nullptr)
            *errorMessage = QStringLiteral("下载内容不是可读取的图片");
        return false;
    }
    if (image.hasAlphaChannel())
    {
        QImage flattened(image.size(), QImage::Format_RGB32);
        flattened.fill(Qt::white);
        QPainter painter(&flattened);
        painter.drawImage(0, 0, image);
        image = flattened;
    }
    if (!QDir().mkpath(QFileInfo(destinationPath).absolutePath()))
    {
        if (errorMessage != nullptr)
            *errorMessage = QStringLiteral("无法创建图片目录");
        return false;
    }
    QSaveFile output(destinationPath);
    if (!output.open(QIODevice::WriteOnly) || !image.save(&output, "JPEG", 92) || !output.commit())
    {
        if (errorMessage != nullptr)
            *errorMessage = output.errorString().isEmpty() ? QStringLiteral("图片写入失败")
                                                           : output.errorString();
        return false;
    }
    return true;
}

} // namespace

class ImageFetchService::Private final : public QObject
{
public:
    struct Pending final
    {
        QPointer<QNetworkReply> reply;
        QPointer<QTimer> timer;
        QByteArray response;
        QString destinationPath;
        bool tooLarge = false;
        bool timedOut = false;
        bool cancelled = false;
    };

    explicit Private(ImageFetchService *owner, QUrl endpoint)
        : QObject(owner), owner(owner), endpoint(std::move(endpoint)), network(this)
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
            if (state.response.size() > maximumResponseBytes)
                state.tooLarge = true;
        }
        QString errorMessage;
        bool succeeded = false;
        if (state.cancelled)
            errorMessage = QStringLiteral("图片下载已取消");
        else if (state.timedOut)
            errorMessage = QStringLiteral("图片下载超时");
        else if (state.tooLarge)
            errorMessage = QStringLiteral("图片服务响应超过 64 MiB 限制");
        else if (reply == nullptr)
            errorMessage = QStringLiteral("图片下载连接已失效");
        else if (reply->error() != QNetworkReply::NoError)
        {
            const QJsonDocument errorDocument = QJsonDocument::fromJson(state.response);
            const QJsonValue detail = errorDocument.object().value(QStringLiteral("detail"));
            if (detail.isString())
                errorMessage = detail.toString();
            else if (!detail.isUndefined() && !detail.isNull())
            {
                const QJsonDocument detailDocument = detail.isArray()
                                                         ? QJsonDocument(detail.toArray())
                                                         : QJsonDocument(detail.toObject());
                errorMessage = QString::fromUtf8(detailDocument.toJson(QJsonDocument::Compact));
            }
            if (errorMessage.isEmpty())
                errorMessage = reply->errorString();
        }
        else
        {
            QJsonParseError parseError;
            const QJsonDocument document = QJsonDocument::fromJson(state.response, &parseError);
            if (parseError.error != QJsonParseError::NoError || !document.isObject())
                errorMessage = QStringLiteral("图片服务返回的 JSON 无效");
            else
            {
                const QJsonObject object = document.object();
                if (!object.value(QStringLiteral("success")).toBool())
                    errorMessage = object.value(QStringLiteral("message"))
                                       .toString(object.value(QStringLiteral("detail"))
                                                     .toString(QStringLiteral("下载失败")));
                else
                {
                    const QByteArray encoded =
                        object.value(QStringLiteral("image")).toString().trimmed().toLatin1();
                    const QByteArray raw =
                        QByteArray::fromBase64(encoded, QByteArray::AbortOnBase64DecodingErrors);
                    if (encoded.isEmpty())
                        errorMessage = QStringLiteral("无图片数据");
                    else if (raw.isEmpty())
                        errorMessage = QStringLiteral("图片 Base64 解码失败");
                    else
                        succeeded = writeJpeg(raw, state.destinationPath, &errorMessage);
                }
            }
        }
        if (reply != nullptr)
            reply->deleteLater();
        emit owner->requestFinished(requestId, succeeded, state.destinationPath, errorMessage);
    }

    ImageFetchService *owner;
    QUrl endpoint;
    QNetworkAccessManager network;
    QHash<quint64, Pending> pending;
    quint64 nextRequestId = 1;
    int timeoutMilliseconds = 50'000;
};

ImageFetchService::ImageFetchService(QUrl endpoint, QObject *parent)
    : QObject(parent), d(new Private(this, std::move(endpoint)))
{
}

quint64 ImageFetchService::fetchToJpeg(const QUrl &sourceUrl, const QString &destinationPath)
{
    const quint64 requestId = d->nextRequestId++;
    if (!sourceUrl.isValid() || (sourceUrl.scheme() != QStringLiteral("http") &&
                                 sourceUrl.scheme() != QStringLiteral("https")))
    {
        QTimer::singleShot(0, this,
                           [this, requestId, destinationPath]()
                           {
                               emit requestFinished(requestId, false, destinationPath,
                                                    QStringLiteral("网址需要以 http(s) 开头"));
                           });
        return requestId;
    }
    if (!d->endpoint.isValid() || (d->endpoint.scheme() != QStringLiteral("http") &&
                                   d->endpoint.scheme() != QStringLiteral("https")))
    {
        QTimer::singleShot(0, this,
                           [this, requestId, destinationPath]()
                           {
                               emit requestFinished(requestId, false, destinationPath,
                                                    QStringLiteral("图片服务地址无效"));
                           });
        return requestId;
    }

    QNetworkRequest request(d->endpoint);
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    const QByteArray body =
        QJsonDocument(QJsonObject{{QStringLiteral("url"), sourceUrl.toString()}})
            .toJson(QJsonDocument::Compact);
    QNetworkReply *reply = d->network.post(request, body);
    auto *timer = new QTimer(reply);
    timer->setSingleShot(true);
    Private::Pending state;
    state.reply = reply;
    state.timer = timer;
    state.destinationPath = QDir::cleanPath(destinationPath);
    d->pending.insert(requestId, state);
    connect(reply, &QIODevice::readyRead, this,
            [this, requestId]()
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
            [this, requestId]()
            {
                if (!d->pending.contains(requestId))
                    return;
                d->pending[requestId].timedOut = true;
                if (d->pending[requestId].reply != nullptr)
                    d->pending[requestId].reply->abort();
            });
    timer->start(d->timeoutMilliseconds);
    emit requestStarted(requestId);
    return requestId;
}

bool ImageFetchService::cancel(quint64 requestId)
{
    if (!d->pending.contains(requestId))
        return false;
    d->pending[requestId].cancelled = true;
    if (d->pending[requestId].reply != nullptr)
        d->pending[requestId].reply->abort();
    return true;
}

void ImageFetchService::cancelAll()
{
    const QList<quint64> requestIds = d->pending.keys();
    for (const quint64 requestId : requestIds)
        cancel(requestId);
}

void ImageFetchService::setTimeoutMilliseconds(int timeoutMilliseconds)
{
    d->timeoutMilliseconds = qMax(1, timeoutMilliseconds);
}

int ImageFetchService::timeoutMilliseconds() const noexcept
{
    return d->timeoutMilliseconds;
}

bool ImageFetchService::isBusy() const noexcept
{
    return !d->pending.isEmpty();
}

QString ImageFetchService::suggestedJpegFileName(const QUrl &sourceUrl)
{
    QString base = QFileInfo(sourceUrl.path(QUrl::FullyDecoded)).fileName();
    if (base.isEmpty() || base == QStringLiteral(".") || base == QStringLiteral(".."))
        return {};
    QString stem = QFileInfo(base).completeBaseName();
    for (const QChar character : QStringLiteral("\\/:*?\"<>|"))
        stem.replace(character, QChar('_'));
    for (int index = 0; index < stem.size(); ++index)
    {
        if (stem.at(index).unicode() < 0x20)
            stem[index] = QChar('_');
    }
    while (stem.endsWith(QChar(' ')) || stem.endsWith(QChar('.')))
        stem.chop(1);
    while (stem.startsWith(QChar(' ')))
        stem.remove(0, 1);
    return stem.isEmpty() ? QStringLiteral("fanart.jpg") : stem + QStringLiteral(".jpg");
}

} // namespace darkeye
