#include "GuildRepository.hpp"

#include "DatabaseManager.hpp"
#include "Core/Logging.hpp"

#include <QSqlQuery>
#include <QSqlError>

namespace Acheron {
namespace Storage {

namespace {

template <typename F>
QVariant snowflakeOrNull(const F &field)
{
    return field.hasValue() ? QVariant(static_cast<qint64>(field.get())) : QVariant();
}

template <typename F>
QVariant enumOrNull(const F &field)
{
    return field.hasValue() ? QVariant(static_cast<int>(field.get())) : QVariant();
}

} // namespace

GuildRepository::GuildRepository(Core::Snowflake accountId)
    : BaseRepository(DatabaseManager::getCacheConnectionName(accountId))
{
}

void GuildRepository::saveGuild(const Discord::Guild &guild, QSqlDatabase &db)
{
    QSqlQuery q(db);
    q.prepare(R"(
		INSERT OR REPLACE INTO guilds
		(id, name, icon, owner_id, features, premium_tier, premium_subscription_count,
		 rules_channel_id, default_message_notifications, description, banner, afk_channel_id,
		 afk_timeout, system_channel_id, system_channel_flags, verification_level,
		 explicit_content_filter, mfa_level, premium_progress_bar_enabled, additional_emoji_slots,
		 additional_sticker_slots, splash, invites_disabled_until, dms_disabled_until,
		 lockdown_duration_hours)
		VALUES (:id, :name, :icon, :owner_id, :features, :premium_tier, :premium_subscription_count,
		        :rules_channel_id, :default_message_notifications, :description, :banner, :afk_channel_id,
		        :afk_timeout, :system_channel_id, :system_channel_flags, :verification_level,
		        :explicit_content_filter, :mfa_level, :premium_progress_bar_enabled, :additional_emoji_slots,
		        :additional_sticker_slots, :splash, :invites_disabled_until, :dms_disabled_until,
		        :lockdown_duration_hours)
    )");

    q.bindValue(":id", static_cast<qint64>(guild.id.get()));
    q.bindValue(":name", guild.name);
    q.bindValue(":icon", guild.icon);
    q.bindValue(":owner_id", static_cast<qint64>(guild.ownerId.get()));
    q.bindValue(":features", guild.features.hasValue() ? QStringList(guild.features.get()).join(',') : QVariant());
    q.bindValue(":premium_tier", enumOrNull(guild.premiumTier));
    bindOptional(q, ":premium_subscription_count", guild.premiumSubscriptionCount);
    q.bindValue(":rules_channel_id", snowflakeOrNull(guild.rulesChannelId));
    q.bindValue(":default_message_notifications", enumOrNull(guild.defaultMessageNotifications));
    bindOptional(q, ":description", guild.description);
    bindOptional(q, ":banner", guild.banner);
    bindOptional(q, ":splash", guild.splash);

    const Discord::GuildIncidentsData incidents = guild.incidentsData.valueOr({});
    q.bindValue(":invites_disabled_until", incidents.invitesDisabledUntil.hasValue()
                                                   ? QVariant(incidents.invitesDisabledUntil->toString(Qt::ISODateWithMs))
                                                   : QVariant());
    q.bindValue(":dms_disabled_until", incidents.dmsDisabledUntil.hasValue() ? QVariant(incidents.dmsDisabledUntil->toString(Qt::ISODateWithMs)) : QVariant());
    q.bindValue(":lockdown_duration_hours", incidents.lockdownDurationHours.hasValue() ? QVariant(incidents.lockdownDurationHours.get()) : QVariant());
    q.bindValue(":afk_channel_id", snowflakeOrNull(guild.afkChannelId));
    bindOptional(q, ":afk_timeout", guild.afkTimeout);
    q.bindValue(":system_channel_id", snowflakeOrNull(guild.systemChannelId));
    q.bindValue(":system_channel_flags", guild.systemChannelFlags.hasValue() ? QVariant(static_cast<int>(guild.systemChannelFlags->toInt())) : QVariant());
    q.bindValue(":verification_level", enumOrNull(guild.verificationLevel));
    q.bindValue(":explicit_content_filter", enumOrNull(guild.explicitContentFilter));
    q.bindValue(":mfa_level", enumOrNull(guild.mfaLevel));
    bindOptional(q, ":premium_progress_bar_enabled", guild.premiumProgressBarEnabled);
    const bool hasPremiumFeatures = guild.premiumFeatures.hasValue();
    q.bindValue(":additional_emoji_slots",
                hasPremiumFeatures && guild.premiumFeatures->additionalEmojiSlots.hasValue()
                        ? QVariant(guild.premiumFeatures->additionalEmojiSlots.get())
                        : QVariant());
    q.bindValue(":additional_sticker_slots",
                hasPremiumFeatures && guild.premiumFeatures->additionalStickerSlots.hasValue()
                        ? QVariant(guild.premiumFeatures->additionalStickerSlots.get())
                        : QVariant());

    execLogged(q, "GuildRepository: Save");
}

void GuildRepository::deleteGuild(Core::Snowflake guildId, QSqlDatabase &db)
{
    QSqlQuery q(db);
    q.prepare("DELETE FROM guilds WHERE id = :id");
    q.bindValue(":id", static_cast<qint64>(guildId));
    execLogged(q, "GuildRepository: Delete");
}

std::optional<Discord::Guild> GuildRepository::getGuild(Core::Snowflake guildId)
{
    auto db = getDb();
    QSqlQuery q(db);
    q.prepare(R"(
        SELECT id, name, icon, owner_id, features, premium_tier, premium_subscription_count,
               rules_channel_id, default_message_notifications, description, banner, afk_channel_id,
               afk_timeout, system_channel_id, system_channel_flags, verification_level,
               explicit_content_filter, mfa_level, premium_progress_bar_enabled, additional_emoji_slots,
               additional_sticker_slots, splash, invites_disabled_until, dms_disabled_until,
               lockdown_duration_hours
        FROM guilds WHERE id = :id
    )");
    q.bindValue(":id", static_cast<qint64>(guildId));

    if (!q.exec() || !q.next())
        return std::nullopt;

    Discord::Guild guild;
    guild.id = static_cast<Core::Snowflake>(q.value(0).toLongLong());
    guild.name = q.value(1).toString();
    guild.icon = q.value(2).toString();
    guild.ownerId = static_cast<Core::Snowflake>(q.value(3).toLongLong());
    if (!q.value(4).isNull())
        guild.features = q.value(4).toString().split(',', Qt::SkipEmptyParts);
    if (!q.value(5).isNull())
        guild.premiumTier = static_cast<Discord::PremiumTier>(q.value(5).toInt());
    if (!q.value(6).isNull())
        guild.premiumSubscriptionCount = q.value(6).toInt();
    if (q.value(7).isNull())
        guild.rulesChannelId = nullptr;
    else
        guild.rulesChannelId = static_cast<Core::Snowflake>(q.value(7).toLongLong());
    if (!q.value(8).isNull())
        guild.defaultMessageNotifications = static_cast<Discord::MessageNotificationLevel>(q.value(8).toInt());
    if (!q.value(9).isNull())
        guild.description = q.value(9).toString();
    if (!q.value(10).isNull())
        guild.banner = q.value(10).toString();
    if (!q.value(11).isNull())
        guild.afkChannelId = static_cast<Core::Snowflake>(q.value(11).toLongLong());
    if (!q.value(12).isNull())
        guild.afkTimeout = q.value(12).toInt();
    if (!q.value(13).isNull())
        guild.systemChannelId = static_cast<Core::Snowflake>(q.value(13).toLongLong());
    if (!q.value(14).isNull())
        guild.systemChannelFlags = Discord::SystemChannelFlags::fromInt(q.value(14).toInt());
    if (!q.value(15).isNull())
        guild.verificationLevel = static_cast<Discord::VerificationLevel>(q.value(15).toInt());
    if (!q.value(16).isNull())
        guild.explicitContentFilter = static_cast<Discord::ExplicitContentFilter>(q.value(16).toInt());
    if (!q.value(17).isNull())
        guild.mfaLevel = static_cast<Discord::MfaLevel>(q.value(17).toInt());
    if (!q.value(18).isNull())
        guild.premiumProgressBarEnabled = q.value(18).toBool();
    if (!q.value(19).isNull() || !q.value(20).isNull()) {
        Discord::GuildPremiumFeatures premiumFeatures;
        if (!q.value(19).isNull())
            premiumFeatures.additionalEmojiSlots = q.value(19).toInt();
        if (!q.value(20).isNull())
            premiumFeatures.additionalStickerSlots = q.value(20).toInt();
        guild.premiumFeatures = premiumFeatures;
    }
    if (!q.value(21).isNull())
        guild.splash = q.value(21).toString();
    if (!q.value(22).isNull() || !q.value(23).isNull() || !q.value(24).isNull()) {
        Discord::GuildIncidentsData incidents;
        if (!q.value(22).isNull())
            incidents.invitesDisabledUntil = QDateTime::fromString(q.value(22).toString(), Qt::ISODateWithMs);
        if (!q.value(23).isNull())
            incidents.dmsDisabledUntil = QDateTime::fromString(q.value(23).toString(), Qt::ISODateWithMs);
        if (!q.value(24).isNull())
            incidents.lockdownDurationHours = q.value(24).toInt();
        guild.incidentsData = incidents;
    }

    return guild;
}
} // namespace Storage
} // namespace Acheron
