#pragma once

#include <QFlags>
#include <QSettings>

namespace Acheron {
namespace UI {
namespace ChannelIndent {

enum class Scope {
    Accounts = 1 << 0,
    Folders = 1 << 1,
    Servers = 1 << 2,
    Categories = 1 << 3,
    Channels = 1 << 4,
};
Q_DECLARE_FLAGS(Scopes, Scope)
Q_DECLARE_OPERATORS_FOR_FLAGS(Scopes)

constexpr int StepWidth = 16;

inline bool enabled()
{
    return QSettings().value("ui/channelListIndent", false).toBool();
}

inline void setEnabled(bool enabled)
{
    QSettings().setValue("ui/channelListIndent", enabled);
}

inline Scopes scopes()
{
    const Scopes everythingBelowAccounts = Scope::Folders | Scope::Servers | Scope::Categories | Scope::Channels;
    return Scopes(QFlag(QSettings().value("ui/channelListIndentInside", int(everythingBelowAccounts)).toInt()));
}

inline void setScopes(Scopes scopes)
{
    QSettings().setValue("ui/channelListIndentInside", int(scopes));
}

inline Scopes activeScopes()
{
    return enabled() ? scopes() : Scopes();
}

} // namespace ChannelIndent
} // namespace UI
} // namespace Acheron
