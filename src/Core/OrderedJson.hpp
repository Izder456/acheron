#pragma once

#include <QByteArray>
#include <QJsonValue>
#include <QList>
#include <QString>

namespace Acheron {
namespace Core {

class OrderedJson
{
public:
    class Array
    {
    public:
        Array &append(const QJsonValue &value);
        Array &append(const OrderedJson &object);
        Array &append(const Array &array);

        QByteArray toBytes() const;

    private:
        QList<QByteArray> elements;
    };

    OrderedJson &insert(const QString &key, const QJsonValue &value);
    OrderedJson &insert(const QString &key, const OrderedJson &object);
    OrderedJson &insert(const QString &key, const Array &array);

    QByteArray toBytes() const;

private:
    static QByteArray serialize(const QJsonValue &value);
    OrderedJson &insertRaw(const QString &key, const QByteArray &json);

    QList<QByteArray> members;
};

} // namespace Core
} // namespace Acheron
