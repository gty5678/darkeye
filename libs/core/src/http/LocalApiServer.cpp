#include "http/LocalApiServer.h"

#include <QHttpHeaders>
#include <QHttpServer>
#include <QHttpServerRequest>
#include <QHttpServerResponder>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSqlQuery>
#include <QSqlError>
#include <QTcpServer>

namespace darkeye
{

namespace
{

constexpr qsizetype maximumRequestBytes = 1024 * 1024;

QHttpHeaders corsHeaders()
{
    QHttpHeaders headers;
    headers.append(QHttpHeaders::WellKnownHeader::AccessControlAllowOrigin, "*");
    headers.append(QHttpHeaders::WellKnownHeader::AccessControlAllowCredentials, "true");
    headers.append(QHttpHeaders::WellKnownHeader::AccessControlAllowMethods, "*");
    headers.append(QHttpHeaders::WellKnownHeader::AccessControlAllowHeaders, "*");
    return headers;
}

QHttpServerResponse jsonResponse(const QJsonObject &body,
                                 QHttpServerResponse::StatusCode status = QHttpServerResponse::StatusCode::Ok)
{
    QHttpServerResponse response(body, status);
    response.setHeaders(corsHeaders());
    return response;
}

QHttpServerResponse errorResponse(QHttpServerResponse::StatusCode status, const QString &detail)
{
    return jsonResponse({{QStringLiteral("detail"), detail}}, status);
}

std::optional<QJsonObject> objectBody(const QHttpServerRequest &request,
                                      QHttpServerResponse *error)
{
    const QByteArray body = request.body();
    if (body.size() > maximumRequestBytes)
    {
        *error = errorResponse(QHttpServerResponse::StatusCode::PayloadTooLarge,
                               QStringLiteral("Request body exceeds 1 MiB"));
        return std::nullopt;
    }
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(body, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject())
    {
        *error = errorResponse(QHttpServerResponse::StatusCode::BadRequest,
                               QStringLiteral("JSON object body is required"));
        return std::nullopt;
    }
    return document.object();
}

QString trimmedString(const QJsonObject &body, const QString &name)
{
    return body.value(name).isString() ? body.value(name).toString().trimmed() : QString();
}

} // namespace

class LocalApiServer::Private final
{
public:
    explicit Private(LocalApiServer *owner, QSqlDatabase database)
        : owner(owner), database(std::move(database)), server(owner), tcpServer(owner)
    {
        server.addAfterRequestHandler(owner,
                                      [](const QHttpServerRequest &, QHttpServerResponse &response)
                                      { response.setHeaders(corsHeaders()); });
        server.setMissingHandler(owner, [](const QHttpServerRequest &request,
                                           QHttpServerResponder &responder)
        {
            if (request.method() == QHttpServerRequest::Method::Options)
            {
                responder.write(corsHeaders(), QHttpServerResponder::StatusCode::NoContent);
                return;
            }
            responder.write(QJsonDocument(QJsonObject{{QStringLiteral("detail"), QStringLiteral("Not Found")}}),
                            corsHeaders(), QHttpServerResponder::StatusCode::NotFound);
        });

        server.route(QStringLiteral("/api/v1/health"), QHttpServerRequest::Method::Get, owner,
                     [] { return jsonResponse({{QStringLiteral("status"), QStringLiteral("ok")},
                                               {QStringLiteral("service"), QStringLiteral("DarkEye Server")}}); });
        server.route(QStringLiteral("/api/v1/check_existence"), QHttpServerRequest::Method::Post,
                     owner, [this](const QHttpServerRequest &request)
                     { return checkExistence(request); });
        server.route(QStringLiteral("/api/v1/minnano-actress-capture"), QHttpServerRequest::Method::Post,
                     owner, [this](const QHttpServerRequest &request)
                     { return minnanoCapture(request); });
        server.route(QStringLiteral("/api/v1/capture/one"), QHttpServerRequest::Method::Post,
                     owner, [this](const QHttpServerRequest &request)
                     { return captureOne(request); });
        server.route(QStringLiteral("/api/v1/crawler-backlog-warning"), QHttpServerRequest::Method::Post,
                     owner, [this](const QHttpServerRequest &request)
                     { return backlogWarning(request); });
        server.route(QStringLiteral("/api/v1/cloudflare-challenge-notify"), QHttpServerRequest::Method::Post,
                     owner, [this](const QHttpServerRequest &request)
                     { return cloudflareChallenge(request); });
    }

    QHttpServerResponse checkExistence(const QHttpServerRequest &request)
    {
        QHttpServerResponse error(QHttpServerResponse::StatusCode::BadRequest);
        const auto body = objectBody(request, &error);
        if (!body.has_value()) return error;
        const QJsonValue itemsValue = body->value(QStringLiteral("items"));
        if (!itemsValue.isArray())
            return errorResponse(QHttpServerResponse::StatusCode::UnprocessableEntity,
                                 QStringLiteral("items must be an array of strings"));
        const QJsonArray items = itemsValue.toArray();
        QJsonObject results;
        QStringList normalized;
        for (const QJsonValue &value : items)
        {
            if (!value.isString())
                return errorResponse(QHttpServerResponse::StatusCode::UnprocessableEntity,
                                     QStringLiteral("items must be an array of strings"));
            const QString original = value.toString();
            results.insert(original, false);
            normalized.append(original.trimmed().toUpper());
        }
        if (normalized.isEmpty()) return jsonResponse({{QStringLiteral("results"), results}});
        if (!database.isOpen())
            return errorResponse(QHttpServerResponse::StatusCode::InternalServerError,
                                 QStringLiteral("Database is unavailable"));

        QStringList placeholders;
        placeholders.fill(QStringLiteral("?"), normalized.size());
        QSqlQuery query(database);
        query.prepare(QStringLiteral("SELECT serial_number FROM work WHERE UPPER(serial_number) IN (%1)")
                          .arg(placeholders.join(QLatin1Char(','))));
        for (const QString &item : normalized) query.addBindValue(item);
        if (!query.exec())
            return errorResponse(QHttpServerResponse::StatusCode::InternalServerError, query.lastError().text());
        QSet<QString> existing;
        while (query.next()) existing.insert(query.value(0).toString().toUpper());
        for (const QJsonValue &value : items)
        {
            const QString original = value.toString();
            if (existing.contains(original.trimmed().toUpper())) results.insert(original, true);
        }
        return jsonResponse({{QStringLiteral("results"), results}});
    }

    QHttpServerResponse minnanoCapture(const QHttpServerRequest &request)
    {
        QHttpServerResponse error(QHttpServerResponse::StatusCode::BadRequest);
        const auto body = objectBody(request, &error);
        if (!body.has_value()) return error;
        emit owner->minnanoActressCaptureReceived(*body);
        return jsonResponse({{QStringLiteral("status"), QStringLiteral("success")},
                             {QStringLiteral("message"), QStringLiteral("Data received")}});
    }

    QHttpServerResponse captureOne(const QHttpServerRequest &request)
    {
        QHttpServerResponse error(QHttpServerResponse::StatusCode::BadRequest);
        const auto body = objectBody(request, &error);
        if (!body.has_value()) return error;
        const QJsonValue content = body->value(QStringLiteral("content"));
        if (!content.isString())
            return errorResponse(QHttpServerResponse::StatusCode::UnprocessableEntity,
                                 QStringLiteral("content is required"));
        emit owner->captureOneReceived(content.toString());
        return jsonResponse({{QStringLiteral("status"), QStringLiteral("success")},
                             {QStringLiteral("message"), QStringLiteral("Data received")}});
    }

    QHttpServerResponse backlogWarning(const QHttpServerRequest &request)
    {
        QHttpServerResponse error(QHttpServerResponse::StatusCode::BadRequest);
        const auto body = objectBody(request, &error);
        if (!body.has_value()) return error;
        const QJsonValue count = body->value(QStringLiteral("count"));
        if (!count.isDouble())
            return errorResponse(QHttpServerResponse::StatusCode::UnprocessableEntity,
                                 QStringLiteral("count is required"));
        const int threshold = body->value(QStringLiteral("threshold")).isDouble()
            ? body->value(QStringLiteral("threshold")).toInt() : 13;
        const QString browser = trimmedString(*body, QStringLiteral("browser")).isEmpty()
            ? QStringLiteral("firefox") : trimmedString(*body, QStringLiteral("browser"));
        if (count.toInt() < threshold)
            return jsonResponse({{QStringLiteral("status"), QStringLiteral("ignored")},
                                 {QStringLiteral("reason"), QStringLiteral("below_threshold")}});
        emit owner->crawlerBacklogWarning(count.toInt(), browser);
        return jsonResponse({{QStringLiteral("status"), QStringLiteral("success")},
                             {QStringLiteral("message"), QStringLiteral("notified")}});
    }

    QHttpServerResponse cloudflareChallenge(const QHttpServerRequest &request)
    {
        QHttpServerResponse error(QHttpServerResponse::StatusCode::BadRequest);
        const auto body = objectBody(request, &error);
        if (!body.has_value()) return error;
        QJsonObject payload;
        for (const QString &key : {QStringLiteral("site"), QStringLiteral("phase"),
                                   QStringLiteral("url"), QStringLiteral("serial"),
                                   QStringLiteral("merge_request_id")})
            payload.insert(key, trimmedString(*body, key));
        emit owner->cloudflareChallengeReceived(payload);
        return jsonResponse({{QStringLiteral("status"), QStringLiteral("success")},
                             {QStringLiteral("message"), QStringLiteral("notified")}});
    }

    LocalApiServer *owner;
    QSqlDatabase database;
    QHttpServer server;
    QTcpServer tcpServer;
};

LocalApiServer::LocalApiServer(QSqlDatabase publicDatabase, QObject *parent)
    : QObject(parent), d(std::make_unique<Private>(this, std::move(publicDatabase)))
{
}

LocalApiServer::~LocalApiServer() = default;

bool LocalApiServer::start(quint16 requestedPort, QString *errorMessage)
{
    if (isListening()) return true;
    if (!d->tcpServer.listen(QHostAddress::LocalHost, requestedPort))
    {
        if (errorMessage != nullptr) *errorMessage = d->tcpServer.errorString();
        return false;
    }
    if (!d->server.bind(&d->tcpServer))
    {
        if (errorMessage != nullptr) *errorMessage = QStringLiteral("Unable to bind HTTP server");
        d->tcpServer.close();
        return false;
    }
    return true;
}

void LocalApiServer::stop()
{
    d->tcpServer.close();
}

bool LocalApiServer::isListening() const noexcept { return d->tcpServer.isListening(); }
quint16 LocalApiServer::port() const noexcept { return d->tcpServer.serverPort(); }

} // namespace darkeye
