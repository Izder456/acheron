#pragma once

#include <QByteArray>
#include <QJsonValue>
#include <QString>

#include "Core/OrderedJson.hpp"

namespace Acheron {
namespace Discord {

class ContextProperties
{
public:
    static ContextProperties empty();
    static ContextProperties location(const QString &location);

    ContextProperties &add(const QString &key, const QJsonValue &value);

    QByteArray toHeaderValue() const;

private:
    Core::OrderedJson json;
};

} // namespace Discord
} // namespace Acheron
