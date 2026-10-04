#pragma once

#include <QList>

#include <optional>

#include "Discord/Entities.hpp"
#include "Snowflake.hpp"

namespace Acheron {
namespace Core {

class RoleHierarchy
{
public:
    RoleHierarchy(Snowflake guildId, Snowflake ownerId, Snowflake selfId, const QList<Discord::Role> &roles, const QList<Snowflake> &selfRoleIds);

    static bool sortsBefore(const Discord::Role &a, const Discord::Role &b, Snowflake guildId);
    static QList<Discord::Role> sorted(QList<Discord::Role> roles, Snowflake guildId);

    [[nodiscard]] const QList<Discord::Role> &sortedRoles() const { return roles; }
    [[nodiscard]] Snowflake owner() const { return ownerId; }
    [[nodiscard]] bool isOwner() const { return selfId == ownerId; }
    [[nodiscard]] const QList<Snowflake> &selfRoles() const { return selfRoleIds; }
    [[nodiscard]] bool selfHolds(Snowflake roleId) const { return roleId == guildId || selfRoleIds.contains(roleId); }

    [[nodiscard]] std::optional<Discord::Role> selfHighestRole() const;

    [[nodiscard]] bool outranksRole(const Discord::Role &role) const;
    [[nodiscard]] bool outranksMember(Snowflake userId, const QList<Snowflake> &roleIds) const;

private:
    [[nodiscard]] std::optional<Discord::Role> highestRole(const QList<Snowflake> &roleIds) const;

    Snowflake guildId;
    Snowflake ownerId;
    Snowflake selfId;
    QList<Discord::Role> roles;
    QList<Snowflake> selfRoleIds;
};

} // namespace Core
} // namespace Acheron
