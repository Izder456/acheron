#pragma once

#include <QList>
#include <QString>

#include <functional>

#include "Core/Snowflake.hpp"

class QMenu;

namespace Acheron {
namespace Core {
class ClientInstance;
}
namespace UI {

enum class GuildSettingsSection {
    Profile,
    Engagement,
    BoostPerks,
    Emoji,
    Stickers,
    Members,
    Roles,
    Invites,
    AuditLog,
    Bans,
    DeleteServer,
};

enum class GuildSettingsGroup {
    Server,
    Expression,
    People,
    Moderation,
    Danger,
};

namespace GuildSettingsAccess {

[[nodiscard]] bool canOpen(Core::ClientInstance *instance, Core::Snowflake guildId);
[[nodiscard]] QList<GuildSettingsSection> visibleSections(Core::ClientInstance *instance, Core::Snowflake guildId);
[[nodiscard]] bool canEdit(Core::ClientInstance *instance, Core::Snowflake guildId, GuildSettingsSection section);
[[nodiscard]] bool selfHasMfa(Core::ClientInstance *instance);

[[nodiscard]] QString title(GuildSettingsSection section);
[[nodiscard]] GuildSettingsGroup group(GuildSettingsSection section);
[[nodiscard]] QString groupTitle(GuildSettingsGroup group);

using SectionsProvider = std::function<QList<GuildSettingsSection>(Core::Snowflake accountId, Core::Snowflake guildId)>;

void addMenu(QMenu *menu, const SectionsProvider &provider, Core::Snowflake accountId, Core::Snowflake guildId,
             const std::function<void(GuildSettingsSection)> &open);

} // namespace GuildSettingsAccess

inline size_t qHash(GuildSettingsSection key, size_t seed = 0) noexcept
{
    return ::qHash(static_cast<int>(key), seed);
}

} // namespace UI
} // namespace Acheron
