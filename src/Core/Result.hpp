#pragma once

#include <QString>
#include <optional>

namespace Acheron {
namespace Core {

template <typename T>
struct Result
{
    std::optional<T> value;
    QString error;
    int code = 0;

    bool success() const { return value.has_value(); }

    static Result<T> makeOk(const T &value)
    {
        Result<T> result{ value, "" };
        return result;
    }

    static Result<T> makeError(const QString &error, int code = 0)
    {
        Result<T> result{ {}, error, code };
        return result;
    }
};

template <>
struct Result<void>
{
    bool ok = false;
    QString error;
    int code = 0;

    bool success() const { return ok; }

    static Result<void> makeOk() { return { true, {} }; }

    static Result<void> makeError(const QString &error, int code = 0) { return { false, error, code }; }
};

} // namespace Core
} // namespace Acheron
