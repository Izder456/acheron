#include "ApiError.hpp"

#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include "HttpClient.hpp"

namespace Acheron {
namespace Discord {

namespace {

QString firstNestedMessage(const QJsonObject &errors)
{
    const auto own = errors.constFind(QLatin1String("_errors"));
    if (own != errors.constEnd()) {
        const QJsonArray messages = own.value().toArray();
        if (!messages.isEmpty())
            return messages.first().toObject().value("message").toString();
    }
    for (auto it = errors.constBegin(); it != errors.constEnd(); ++it)
        if (it.value().isObject())
            return firstNestedMessage(it.value().toObject());
    return {};
}

} // namespace

ApiError ApiError::fromResponse(const HttpResponse &response)
{
    ApiError error;
    const QJsonObject body = QJsonDocument::fromJson(response.body).object();
    error.code = body.value("code").toInt();

    if (error.code == InvalidFormBody)
        error.message = firstNestedMessage(body.value("errors").toObject());
    if (error.message.isEmpty())
        error.message = body.value("message").toString();
    if (error.message.isEmpty())
        error.message = response.error;
    if (error.message.isEmpty())
        error.message = QCoreApplication::translate("ApiError", "An unexpected error occurred.");

    return error;
}

} // namespace Discord
} // namespace Acheron
