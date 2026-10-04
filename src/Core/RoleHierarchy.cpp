#include "RoleHierarchy.hpp"

#include <QSet>

#include <algorithm>

namespace Acheron {
namespace Core {

RoleHierarchy::RoleHierarchy(Snowflake guildId, Snowflake ownerId, Snowflake selfId, const QList<Discord::Role> &roles,
                             const QList<Snowflake> &selfRoleIds)
    : guildId(guildId), ownerId(ownerId), selfId(selfId), roles(sorted(roles, guildId)), selfRoleIds(selfRoleIds)
{
}

bool RoleHierarchy::sortsBefore(const Discord::Role &a, const Discord::Role &b, Snowflake guildId)
{
    const bool aIsEveryone = a.id.get() == guildId;
    const bool bIsEveryone = b.id.get() == guildId;
    if (aIsEveryone != bIsEveryone)
        return bIsEveryone;
    if (a.position.get() != b.position.get())
        return a.position.get() > b.position.get();
    return a.id.get() < b.id.get();
}

QList<Discord::Role> RoleHierarchy::sorted(QList<Discord::Role> roles, Snowflake guildId)
{
    std::stable_sort(roles.begin(), roles.end(), [guildId](const Discord::Role &a, const Discord::Role &b) { return sortsBefore(a, b, guildId); });
    return roles;
}

std::optional<Discord::Role> RoleHierarchy::highestRole(const QList<Snowflake> &roleIds) const
{
    const QSet<Snowflake> held(roleIds.cbegin(), roleIds.cend());
    for (const Discord::Role &role : roles)
        if (role.id.get() != guildId && held.contains(role.id.get()))
            return role;
    return std::nullopt;
}

std::optional<Discord::Role> RoleHierarchy::selfHighestRole() const
{
    return highestRole(selfRoleIds);
}

bool RoleHierarchy::outranksRole(const Discord::Role &role) const
{
    if (isOwner())
        return true;
    const auto highest = selfHighestRole();
    return highest && sortsBefore(*highest, role, guildId);
}

bool RoleHierarchy::outranksMember(Snowflake userId, const QList<Snowflake> &roleIds) const
{
    if (userId == ownerId)
        return false;
    if (isOwner())
        return true;

    const auto highest = selfHighestRole();
    if (!highest)
        return false;
    const auto theirHighest = highestRole(roleIds);
    return !theirHighest || sortsBefore(*highest, *theirHighest, guildId);
}

} // namespace Core
} // namespace Acheron
