#include "OrderedJson.hpp"

#include <QJsonArray>
#include <QJsonObject>
#include <QLocale>

#include <cmath>

namespace Acheron {
namespace Core {

namespace {

QByteArray quoted(const QString &text)
{
    QString escaped;
    escaped.reserve(text.size() + 2);
    escaped += '"';
    for (QChar c : text) {
        switch (c.unicode()) {
        case '"':
            escaped += "\\\"";
            break;
        case '\\':
            escaped += "\\\\";
            break;
        case '\b':
            escaped += "\\b";
            break;
        case '\f':
            escaped += "\\f";
            break;
        case '\n':
            escaped += "\\n";
            break;
        case '\r':
            escaped += "\\r";
            break;
        case '\t':
            escaped += "\\t";
            break;
        default:
            if (c.unicode() < 0x20)
                escaped += QString("\\u%1").arg(int(c.unicode()), 4, 16, QChar('0'));
            else
                escaped += c;
        }
    }
    escaped += '"';
    return escaped.toUtf8();
}

QByteArray number(double value)
{
    if (!std::isfinite(value))
        return "null";
    if (value == std::trunc(value) && std::fabs(value) < 9007199254740992.0)
        return QByteArray::number(static_cast<qint64>(value));
    return QByteArray::number(value, 'g', QLocale::FloatingPointShortest);
}

QByteArray member(const QString &key, const QByteArray &json)
{
    return quoted(key) + ':' + json;
}

QByteArray join(const QList<QByteArray> &parts, char open, char close)
{
    QByteArray out;
    out += open;
    for (int i = 0; i < parts.size(); i++) {
        if (i > 0)
            out += ',';
        out += parts[i];
    }
    out += close;
    return out;
}

} // namespace

OrderedJson::Array &OrderedJson::Array::append(const QJsonValue &value)
{
    elements.append(serialize(value));
    return *this;
}

OrderedJson::Array &OrderedJson::Array::append(const OrderedJson &object)
{
    elements.append(object.toBytes());
    return *this;
}

OrderedJson::Array &OrderedJson::Array::append(const Array &array)
{
    elements.append(array.toBytes());
    return *this;
}

QByteArray OrderedJson::Array::toBytes() const
{
    return join(elements, '[', ']');
}

OrderedJson &OrderedJson::insert(const QString &key, const QJsonValue &value)
{
    return insertRaw(key, serialize(value));
}

OrderedJson &OrderedJson::insert(const QString &key, const OrderedJson &object)
{
    return insertRaw(key, object.toBytes());
}

OrderedJson &OrderedJson::insert(const QString &key, const Array &array)
{
    return insertRaw(key, array.toBytes());
}

OrderedJson &OrderedJson::insertRaw(const QString &key, const QByteArray &json)
{
    members.append(member(key, json));
    return *this;
}

QByteArray OrderedJson::toBytes() const
{
    return join(members, '{', '}');
}

QByteArray OrderedJson::serialize(const QJsonValue &value)
{
    switch (value.type()) {
    case QJsonValue::Bool:
        return value.toBool() ? "true" : "false";
    case QJsonValue::Double:
        return number(value.toDouble());
    case QJsonValue::String:
        return quoted(value.toString());
    case QJsonValue::Array: {
        QList<QByteArray> elements;
        for (const QJsonValue &element : value.toArray())
            elements.append(serialize(element));
        return join(elements, '[', ']');
    }
    case QJsonValue::Object: {
        QList<QByteArray> members;
        const QJsonObject object = value.toObject();
        for (auto it = object.constBegin(); it != object.constEnd(); ++it)
            members.append(member(it.key(), serialize(it.value())));
        return join(members, '{', '}');
    }
    case QJsonValue::Null:
    case QJsonValue::Undefined:
        break;
    }
    return "null";
}

} // namespace Core
} // namespace Acheron
