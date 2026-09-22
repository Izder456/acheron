#include "ContextProperties.hpp"

namespace Acheron {
namespace Discord {

ContextProperties ContextProperties::empty()
{
    return {};
}

ContextProperties ContextProperties::location(const QString &location)
{
    return ContextProperties().add("location", location);
}

ContextProperties &ContextProperties::add(const QString &key, const QJsonValue &value)
{
    json.insert(key, value);
    return *this;
}

QByteArray ContextProperties::toHeaderValue() const
{
    return json.toBytes().toBase64();
}

} // namespace Discord
} // namespace Acheron
