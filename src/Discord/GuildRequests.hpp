#pragma once

#include <QDateTime>
#include <QRegularExpression>
#include <QStringList>

#include <optional>

#include "Entities.hpp"

namespace Acheron {
namespace Discord {

struct GuildEdit : Core::JsonUtils::JsonObject
{
    Field<QString, true, true> splash;
    Field<QString, true, true> banner;
    Field<Core::Snowflake, true, true> afkChannelId;
    Field<int, true> afkTimeout;
    Field<Core::Snowflake, true, true> systemChannelId;
    Field<MessageNotificationLevel, true> defaultMessageNotifications;
    Field<SystemChannelFlags, true> systemChannelFlags;
    Field<bool, true> premiumProgressBarEnabled;

    Core::OrderedJson toJson() const
    {
        Core::OrderedJson obj;
        insert(obj, "splash", splash);
        insert(obj, "banner", banner);
        insert(obj, "afk_channel_id", afkChannelId);
        insert(obj, "afk_timeout", afkTimeout);
        insert(obj, "system_channel_id", systemChannelId);
        insert(obj, "default_message_notifications", defaultMessageNotifications);
        insert(obj, "system_channel_flags", systemChannelFlags);
        insert(obj, "premium_progress_bar_enabled", premiumProgressBarEnabled);
        return obj;
    }
};

struct GuildProfileEdit
{
    struct Trait
    {
        QString label;
        QJsonValue emojiId;
        QJsonValue emojiName;
        QJsonValue emojiAnimated;
    };

    static constexpr int TraitSlots = 5;

    QString name;
    QString description;
    QJsonValue icon;
    QJsonValue customBanner;
    QJsonValue visibility;
    QJsonValue brandColorPrimary;
    QList<Trait> traits;
    QJsonArray gameApplicationIds;

    static GuildProfileEdit fromProfile(const QJsonObject &profile)
    {
        GuildProfileEdit edit;
        edit.name = profile.value("name").toString();
        edit.description = profile.value("description").toString();
        edit.icon = profile.value("icon_hash");
        edit.customBanner = profile.value("custom_banner_hash");
        edit.visibility = profile.value("visibility");
        const QJsonValue brandColor = profile.value("brand_color_primary");
        edit.brandColorPrimary = brandColor.toString().isEmpty() ? QJsonValue(QJsonValue::Null) : brandColor;

        for (int i = 0; i < TraitSlots; i++)
            edit.traits.append(Trait{});
        for (const QJsonValue &value : profile.value("traits").toArray()) {
            const QJsonObject trait = value.toObject();
            const int position = trait.value("position").toInt(-1);
            if (position < 0 || position >= TraitSlots)
                continue;
            edit.traits[position] = { trait.value("label").toString(), trait.value("emoji_id"), trait.value("emoji_name"), trait.value("emoji_animated") };
        }
        edit.gameApplicationIds = profile.value("game_application_ids").toArray();
        return edit;
    }

    Core::OrderedJson toJson() const
    {
        Core::OrderedJson obj;
        obj.insert("name", name);
        obj.insert("description", description);
        obj.insert("icon", icon.isUndefined() ? QJsonValue(QJsonValue::Null) : icon);
        obj.insert("custom_banner", customBanner.isUndefined() ? QJsonValue(QJsonValue::Null) : customBanner);
        if (!visibility.isNull() && !visibility.isUndefined())
            obj.insert("visibility", visibility);
        obj.insert("brand_color_primary", brandColorPrimary.isUndefined() ? QJsonValue(QJsonValue::Null) : brandColorPrimary);

        Core::OrderedJson::Array traitArray;
        for (int i = 0; i < traits.size(); i++) {
            const Trait &trait = traits[i];
            if (trait.label.isEmpty())
                continue;
            Core::OrderedJson entry;
            auto insertIfSet = [&entry](const QString &key, const QJsonValue &value) {
                if (!value.isNull() && !value.isUndefined())
                    entry.insert(key, value);
            };
            entry.insert("label", trait.label);
            entry.insert("position", i);
            insertIfSet("emoji_id", trait.emojiId);
            insertIfSet("emoji_name", trait.emojiName);
            insertIfSet("emoji_animated", trait.emojiAnimated);
            traitArray.append(entry);
        }
        obj.insert("traits", traitArray);
        obj.insert("game_application_ids", gameApplicationIds);
        return obj;
    }
};

struct RoleEdit : Core::JsonUtils::JsonObject
{
    Field<QString> name;
    Field<Permissions> permissions;
    Field<int> color;
    Field<RoleColors, true, true> colors;
    Field<bool> hoist;
    Field<bool> mentionable;
    Field<QString, true, true> icon;
    Field<QString, false, true> unicodeEmoji;

    static RoleEdit fromRole(const Role &role)
    {
        RoleEdit edit;
        edit.name = role.name.get();
        edit.permissions = role.permissions.get();
        edit.color = role.color.valueOr(0);
        if (role.colors.hasValue())
            edit.colors = role.colors.get();
        edit.hoist = role.hoist.valueOr(false);
        edit.mentionable = role.mentionable.valueOr(false);
        if (role.unicodeEmoji.hasValue())
            edit.unicodeEmoji = role.unicodeEmoji.get();
        else
            edit.unicodeEmoji = nullptr;
        return edit;
    }

    Core::OrderedJson toJson() const
    {
        Core::OrderedJson obj;
        insert(obj, "name", name);
        obj.insert("permissions", QString::number(static_cast<quint64>(permissions.get())));
        insert(obj, "color", color);
        if (colors.hasValue())
            insert(obj, "colors", colors);
        insert(obj, "hoist", hoist);
        insert(obj, "mentionable", mentionable);
        insert(obj, "icon", icon);
        insert(obj, "unicode_emoji", unicodeEmoji);
        return obj;
    }
};

struct MemberSearchQuery
{
    enum class Sort {
        JoinedNewest = 1,
        JoinedOldest = 2,
        UserIdDescending = 3,
        UserIdAscending = 4,
    };

    struct Cursor
    {
        qint64 guildJoinedAtMs = 0;
        Core::Snowflake userId;
    };

    static constexpr int MinimumTextLength = 2;

    QString text;
    bool onlyTimedOut = false;
    std::optional<Sort> sort;
    int limit = 250;
    std::optional<Cursor> after;

    [[nodiscard]] bool searchesText() const { return text.size() >= MinimumTextLength; }

    Core::OrderedJson toJson() const
    {
        Core::OrderedJson andQuery;
        if (searchesText()) {
            QStringList usernames;
            QStringList userIds;
            static const QRegularExpression snowflakePattern(QStringLiteral("^\\d{17,19}$"));
            for (const QString &term : text.split(QLatin1Char(',')))
                (snowflakePattern.match(term.trimmed()).hasMatch() ? userIds : usernames).append(term.trimmed());
            if (!usernames.isEmpty())
                andQuery.insert("usernames", Core::OrderedJson().insert("or_query", QJsonArray::fromStringList(usernames)));
            if (!userIds.isEmpty())
                andQuery.insert("user_id", Core::OrderedJson().insert("or_query", QJsonArray::fromStringList(userIds)));
        }

        Core::OrderedJson orQuery;
        if (onlyTimedOut) {
            Core::OrderedJson range;
            range.insert("gte", QDateTime::currentMSecsSinceEpoch());
            Core::OrderedJson safetySignals;
            safetySignals.insert("communication_disabled_until", Core::OrderedJson().insert("range", range));
            orQuery.insert("safety_signals", safetySignals);
        }

        Core::OrderedJson obj;
        obj.insert("or_query", orQuery);
        obj.insert("and_query", andQuery);
        if (sort)
            obj.insert("sort", static_cast<int>(*sort));
        obj.insert("limit", limit);
        if (after) {
            Core::OrderedJson cursor;
            cursor.insert("guild_joined_at", after->guildJoinedAtMs);
            cursor.insert("user_id", QString::number(quint64(after->userId)));
            obj.insert("after", cursor);
        }
        return obj;
    }
};

} // namespace Discord
} // namespace Acheron
