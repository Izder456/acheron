#pragma once

#include <QObject>
#include <QCache>
#include <QPair>
#include <optional>

#include "Snowflake.hpp"
#include "Discord/Enums.hpp"
#include "Discord/Entities.hpp"

#include "Storage/GuildRepository.hpp"
#include "Storage/ChannelRepository.hpp"
#include "Storage/RoleRepository.hpp"
#include "Storage/MemberRepository.hpp"

namespace Acheron {
namespace Core {

class PermissionManager : public QObject
{
    Q_OBJECT
public:
    explicit PermissionManager(Snowflake accountId, QObject *parent = nullptr);

    Discord::Permissions getChannelPermissions(Snowflake userId, Snowflake channelId);
    bool hasChannelPermission(Snowflake userId, Snowflake channelId,
                              Discord::Permissions permission);

    Discord::Permissions getGuildPermissions(Snowflake userId, Snowflake guildId);
    bool hasGuildPermission(Snowflake userId, Snowflake guildId, Discord::Permissions permission);

    void precomputeGuildPermissions(const Discord::Guild &guild, const Discord::Member &member,
                                    const QList<Discord::Role> &roles,
                                    const QList<Discord::Channel> &channels, Snowflake userId);

    void setSelfMfaEnabled(bool enabled);

    void invalidateChannelCache(Snowflake channelId);
    void invalidateUserGuildCache(Snowflake userId, Snowflake guildId);

signals:
    void channelPermissionsChanged(Snowflake channelId);
    void guildPermissionsChanged(Snowflake guildId);

private:
    Discord::Permissions computeChannelPermissions(Snowflake userId, Snowflake channelId);
    std::optional<Discord::Permissions> computeGuildPermissions(Snowflake userId, Snowflake guildId);
    [[nodiscard]] Discord::Permissions applyMfaRequirement(Discord::Permissions permissions, Snowflake userId, const Discord::Guild &guild) const;

    Snowflake accountId;
    bool selfMfaEnabled = true;

    Storage::RoleRepository roleRepo;
    Storage::GuildRepository guildRepo;
    Storage::ChannelRepository channelRepo;
    Storage::MemberRepository memberRepo;

    QHash<QPair<Snowflake /* userId */, Snowflake /* channelId */>, Discord::Permissions>
            permissionCache;
    QHash<QPair<Snowflake, Snowflake>, Discord::Permissions> guildPermissionCache;
};

} // namespace Core
} // namespace Acheron
