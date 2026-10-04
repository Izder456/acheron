#include "PermissionManager.hpp"
#include "PermissionComputer.hpp"
#include "Logging.hpp"

#include "Storage/RoleRepository.hpp"
#include "Storage/GuildRepository.hpp"
#include "Storage/ChannelRepository.hpp"
#include "Storage/MemberRepository.hpp"

namespace Acheron {
namespace Core {

PermissionManager::PermissionManager(Snowflake accountId, QObject *parent)
    : QObject(parent),
      accountId(accountId),
      roleRepo(accountId),
      guildRepo(accountId),
      channelRepo(accountId),
      memberRepo(accountId)
{
}

void PermissionManager::setSelfMfaEnabled(bool enabled)
{
    selfMfaEnabled = enabled;
}

Discord::Permissions PermissionManager::applyMfaRequirement(Discord::Permissions permissions, Snowflake userId, const Discord::Guild &guild) const
{
    if (userId != accountId || selfMfaEnabled || guild.mfaLevel.valueOr(Discord::MfaLevel::NONE) != Discord::MfaLevel::ELEVATED)
        return permissions;
    return permissions & ~Discord::MFA_ELEVATED_PERMISSIONS;
}

Discord::Permissions PermissionManager::getChannelPermissions(Snowflake userId, Snowflake channelId)
{
    auto cacheKey = qMakePair(userId, channelId);
    if (permissionCache.contains(cacheKey))
        return permissionCache.value(cacheKey);

    auto permissions = computeChannelPermissions(userId, channelId);

    permissionCache.insert(cacheKey, permissions);

    return permissions;
}

bool PermissionManager::hasChannelPermission(Snowflake userId, Snowflake channelId,
                                             Discord::Permissions permission)
{
    auto perms = getChannelPermissions(userId, channelId);
    return (perms & permission) == permission;
}

Discord::Permissions PermissionManager::getGuildPermissions(Snowflake userId, Snowflake guildId)
{
    if (userId != accountId)
        return computeGuildPermissions(userId, guildId).value_or(Discord::NO_PERMISSIONS);

    auto cacheKey = qMakePair(userId, guildId);
    auto cached = guildPermissionCache.constFind(cacheKey);
    if (cached != guildPermissionCache.constEnd())
        return cached.value();

    const auto permissions = computeGuildPermissions(userId, guildId);
    if (permissions)
        guildPermissionCache.insert(cacheKey, *permissions);
    return permissions.value_or(Discord::NO_PERMISSIONS);
}

bool PermissionManager::hasGuildPermission(Snowflake userId, Snowflake guildId, Discord::Permissions permission)
{
    return (getGuildPermissions(userId, guildId) & permission) == permission;
}

std::optional<Discord::Permissions> PermissionManager::computeGuildPermissions(Snowflake userId, Snowflake guildId)
{
    auto guildOpt = guildRepo.getGuild(guildId);
    if (!guildOpt)
        return std::nullopt;

    auto memberOpt = memberRepo.getMember(guildId, userId);
    if (!memberOpt && guildOpt->ownerId.get() != userId)
        return std::nullopt;

    QList<Snowflake> memberRoleIds;
    if (memberOpt && memberOpt->roles.hasValue())
        memberRoleIds = memberOpt->roles.get();

    auto permissions = PermissionComputer::computeBasePermissions(guildOpt->ownerId.get(), userId, guildId, memberRoleIds, roleRepo.getRolesForGuild(guildId));
    return applyMfaRequirement(permissions, userId, *guildOpt);
}

void PermissionManager::precomputeGuildPermissions(const Discord::Guild &guild,
                                                   const Discord::Member &member,
                                                   const QList<Discord::Role> &roles,
                                                   const QList<Discord::Channel> &channels,
                                                   Snowflake userId)
{
    QList<Snowflake> memberRoleIds;
    if (member.roles.hasValue())
        memberRoleIds = member.roles.get();

    auto guildPermissions = PermissionComputer::computeBasePermissions(guild.ownerId.get(), userId, guild.id.get(), memberRoleIds, roles);
    guildPermissionCache.insert(qMakePair(userId, guild.id.get()), applyMfaRequirement(guildPermissions, userId, guild));

    for (const auto &channel : channels) {
        QList<Discord::PermissionOverwrite> overwrites;
        if (channel.permissionOverwrites.hasValue())
            overwrites = channel.permissionOverwrites.get();

        auto permissions = PermissionComputer::computeChannelPermissions(
                guild.ownerId.get(), userId, guild.id.get(), false, memberRoleIds, roles,
                overwrites);

        auto cacheKey = qMakePair(userId, channel.id.get());
        permissionCache.insert(cacheKey, applyMfaRequirement(permissions, userId, guild));
    }
}

Discord::Permissions PermissionManager::computeChannelPermissions(Snowflake userId,
                                                                  Snowflake channelId)
{
    auto channelOpt = channelRepo.getChannel(channelId);
    if (!channelOpt) {
        qCWarning(LogCore) << "PermissionManager: Channel not found:" << channelId;
        return Discord::NO_PERMISSIONS;
    }

    const auto &channel = *channelOpt;

    bool isDM = !channel.guildId.hasValue();

    if (isDM) {
        return PermissionComputer::computeChannelPermissions(Snowflake::Invalid, userId,
                                                             Snowflake::Invalid, true, {}, {}, {});
    }

    Snowflake guildId = channel.guildId.get();

    auto guildOpt = guildRepo.getGuild(guildId);
    if (!guildOpt) {
        qCWarning(LogCore) << "PermissionManager: Guild not found:" << guildId;
        return Discord::NO_PERMISSIONS;
    }

    const auto &guild = *guildOpt;

    auto memberOpt = memberRepo.getMember(guildId, userId);
    if (!memberOpt) {
        qCWarning(LogCore) << "PermissionManager: Member not found:" << userId << "in" << guildId;
        return Discord::NO_PERMISSIONS;
    }

    const auto &member = *memberOpt;

    auto allRoles = roleRepo.getRolesForGuild(guildId);

    QList<Discord::PermissionOverwrite> overwrites;
    if (channel.permissionOverwrites.hasValue())
        overwrites = channel.permissionOverwrites.get();

    QList<Snowflake> memberRoleIds;
    if (member.roles.hasValue())
        memberRoleIds = member.roles.get();

    auto permissions = PermissionComputer::computeChannelPermissions(guild.ownerId.get(), userId, guildId, false, memberRoleIds, allRoles, overwrites);
    return applyMfaRequirement(permissions, userId, guild);
}

void PermissionManager::invalidateChannelCache(Snowflake channelId)
{
    auto it = permissionCache.begin();
    while (it != permissionCache.end()) {
        if (it.key().second == channelId)
            it = permissionCache.erase(it);
        else
            ++it;
    }

    qCDebug(LogCore) << "Invalidated permission cache for channel:" << channelId;
    emit channelPermissionsChanged(channelId);
}

void PermissionManager::invalidateUserGuildCache(Snowflake userId, Snowflake guildId)
{
    guildPermissionCache.remove(qMakePair(userId, guildId));

    auto channels = channelRepo.getChannelsForGuild(guildId);

    QSet<Snowflake> channelIds;
    channelIds.reserve(channels.size());
    for (const auto &channel : channels)
        channelIds.insert(channel.id.get());

    QList<Snowflake> invalidated;
    auto it = permissionCache.begin();
    while (it != permissionCache.end()) {
        if (it.key().first == userId && channelIds.contains(it.key().second)) {
            invalidated.append(it.key().second);
            it = permissionCache.erase(it);
        } else {
            ++it;
        }
    }

    for (Snowflake channelId : invalidated)
        emit channelPermissionsChanged(channelId);
    emit guildPermissionsChanged(guildId);

    qCDebug(LogCore) << "Invalidated" << invalidated.size() << "cached permissions for user" << userId << "in guild:" << guildId;
}

} // namespace Core
} // namespace Acheron
