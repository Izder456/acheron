#include "AuditLogFormatter.hpp"

#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLocale>
#include <QSet>

#include <algorithm>
#include <cmath>
#include <functional>

#include "Core/ClientInstance.hpp"

namespace Acheron {
namespace UI {

using Action = Discord::AuditLogAction;

enum class AuditLogFormatter::Target {
    Unknown,
    Guild,
    Channel,
    ChannelOverwrite,
    User,
    Role,
    Invite,
    Webhook,
    Emoji,
    Integration,
    StageInstance,
    Sticker,
    ScheduledEvent,
    ScheduledEventException,
    Thread,
    ApplicationCommand,
    AutoModerationRule,
    Soundboard,
    OnboardingPrompt,
    Onboarding,
    GuildHome,
    HomeSettings,
    VoiceChannelStatus,
    MemberVerification,
    GuildProfile,
};

struct AuditLogFormatter::Change
{
    QString key;
    QJsonValue oldValue;
    QJsonValue newValue;
    QString subtarget;
    QStringList items;
};

struct AuditLogFormatter::Entry
{
    Discord::AuditLogEntry raw;
    Action action;
    Target target = Target::Unknown;
    Category category = Category::Other;
    Core::Snowflake id;
    Core::Snowflake userId;
    Core::Snowflake targetId;
    QDateTime time;
    QList<Change> changes;
};

namespace {

constexpr qint64 MergeWindowSecs = 30 * 60;
constexpr int MaxMerges = 50;

QString tr(const char *text)
{
    return QCoreApplication::translate("AuditLogFormatter", text);
}

QString bold(const QString &text)
{
    return QStringLiteral("<b>%1</b>").arg(text.toHtmlEscaped());
}

bool isNull(const QJsonValue &value)
{
    return value.isNull() || value.isUndefined();
}

bool truthy(const QJsonValue &value)
{
    switch (value.type()) {
    case QJsonValue::Bool:
        return value.toBool();
    case QJsonValue::Double:
        return value.toDouble() != 0;
    case QJsonValue::String:
        return !value.toString().isEmpty();
    case QJsonValue::Array:
    case QJsonValue::Object:
        return true;
    default:
        return false;
    }
}

QString text(const QJsonValue &value)
{
    switch (value.type()) {
    case QJsonValue::Bool:
        return value.toBool() ? QStringLiteral("true") : QStringLiteral("false");
    case QJsonValue::Double: {
        const double number = value.toDouble();
        return number == std::floor(number) ? QString::number(qint64(number)) : QString::number(number);
    }
    case QJsonValue::String:
        return value.toString();
    case QJsonValue::Array: {
        QStringList parts;
        for (const QJsonValue &item : value.toArray())
            parts.append(text(item));
        return parts.join(QStringLiteral(", "));
    }
    case QJsonValue::Object:
        return QString::fromUtf8(QJsonDocument(value.toObject()).toJson(QJsonDocument::Compact));
    default:
        return {};
    }
}

qint64 number(const QJsonValue &value)
{
    return value.isString() ? value.toString().toLongLong() : qint64(value.toDouble());
}

quint64 bitsOf(const QJsonValue &value)
{
    return value.isString() ? value.toString().toULongLong() : 0;
}

Core::Snowflake snowflakeOf(const QJsonValue &value)
{
    if (value.isString())
        return Core::Snowflake(value.toString().toULongLong());
    if (value.isDouble())
        return Core::Snowflake(quint64(value.toDouble()));
    return {};
}

QString countText(qint64 count, const char *one, const char *other)
{
    return count == 1 ? tr(one) : tr(other).arg(QLocale::system().toString(count));
}

QString colorText(const QJsonValue &value)
{
    return QStringLiteral("#%1").arg(quint32(number(value)) & 0xffffff, 6, 16, QLatin1Char('0')).toUpper();
}

QString swatch(const QString &color)
{
    return QStringLiteral("<span style=\"color:%1\">&#9632;</span>").arg(color.toHtmlEscaped());
}

QString calendar(const QDateTime &time)
{
    const QDateTime local = time.toLocalTime();
    const qint64 days = local.date().daysTo(QDate::currentDate());
    const QLocale locale = QLocale::system();
    const QString clock = locale.toString(local.time(), QStringLiteral("h:mm AP"));
    const QString weekday = locale.dayName(local.date().dayOfWeek());
    if (days == 0)
        return tr("Today at %1").arg(clock);
    if (days == 1)
        return tr("Yesterday at %1").arg(clock);
    if (days > 1 && days < 7)
        return tr("Last %1 at %2").arg(weekday, clock);
    if (days == -1)
        return tr("Tomorrow at %1").arg(clock);
    if (days < -1 && days > -7)
        return tr("%1 at %2").arg(weekday, clock);
    return locale.toString(local.date(), QStringLiteral("MM/dd/yyyy"));
}

QString longDateTime(const QJsonValue &value)
{
    const QDateTime time = QDateTime::fromString(value.toString(), Qt::ISODateWithMs);
    if (!time.isValid())
        return text(value);
    return QLocale::system().toString(time.toLocalTime(), QStringLiteral("dddd, MMMM d, yyyy h:mm AP"));
}

struct PermissionName
{
    int bit;
    const char *name;
};

constexpr PermissionName PermissionNames[] = {
    { 0, QT_TRANSLATE_NOOP("AuditLogFormatter", "Create Invite") },
    { 1, QT_TRANSLATE_NOOP("AuditLogFormatter", "Kick Members") },
    { 2, QT_TRANSLATE_NOOP("AuditLogFormatter", "Ban Members") },
    { 3, QT_TRANSLATE_NOOP("AuditLogFormatter", "Administrator") },
    { 4, QT_TRANSLATE_NOOP("AuditLogFormatter", "Manage Channels") },
    { 5, QT_TRANSLATE_NOOP("AuditLogFormatter", "Manage Server") },
    { 26, QT_TRANSLATE_NOOP("AuditLogFormatter", "Change Nickname") },
    { 27, QT_TRANSLATE_NOOP("AuditLogFormatter", "Manage Nicknames") },
    { 28, QT_TRANSLATE_NOOP("AuditLogFormatter", "Manage Roles") },
    { 29, QT_TRANSLATE_NOOP("AuditLogFormatter", "Manage Webhooks") },
    { 30, QT_TRANSLATE_NOOP("AuditLogFormatter", "Manage Expressions") },
    { 43, QT_TRANSLATE_NOOP("AuditLogFormatter", "Create Expressions") },
    { 7, QT_TRANSLATE_NOOP("AuditLogFormatter", "View Audit Log") },
    { 10, QT_TRANSLATE_NOOP("AuditLogFormatter", "View Channels") },
    { 19, QT_TRANSLATE_NOOP("AuditLogFormatter", "View Server Insights") },
    { 41, QT_TRANSLATE_NOOP("AuditLogFormatter", "View Server Subscription Insights") },
    { 40, QT_TRANSLATE_NOOP("AuditLogFormatter", "Timeout Members") },
    { 39, QT_TRANSLATE_NOOP("AuditLogFormatter", "Use Activities") },
    { 50, QT_TRANSLATE_NOOP("AuditLogFormatter", "Use External Apps") },
    { 11, QT_TRANSLATE_NOOP("AuditLogFormatter", "Send Messages") },
    { 12, QT_TRANSLATE_NOOP("AuditLogFormatter", "Send TTS Messages") },
    { 13, QT_TRANSLATE_NOOP("AuditLogFormatter", "Manage Messages") },
    { 14, QT_TRANSLATE_NOOP("AuditLogFormatter", "Embed Links") },
    { 15, QT_TRANSLATE_NOOP("AuditLogFormatter", "Attach Files") },
    { 16, QT_TRANSLATE_NOOP("AuditLogFormatter", "Read Message History") },
    { 17, QT_TRANSLATE_NOOP("AuditLogFormatter", "Mention @everyone, @here, and All Roles") },
    { 18, QT_TRANSLATE_NOOP("AuditLogFormatter", "Use External Emoji") },
    { 6, QT_TRANSLATE_NOOP("AuditLogFormatter", "Add Reactions") },
    { 31, QT_TRANSLATE_NOOP("AuditLogFormatter", "Use Application Commands") },
    { 34, QT_TRANSLATE_NOOP("AuditLogFormatter", "Manage Threads") },
    { 35, QT_TRANSLATE_NOOP("AuditLogFormatter", "Create Public Threads") },
    { 36, QT_TRANSLATE_NOOP("AuditLogFormatter", "Create Private Threads") },
    { 37, QT_TRANSLATE_NOOP("AuditLogFormatter", "Use External Stickers") },
    { 38, QT_TRANSLATE_NOOP("AuditLogFormatter", "Send Messages in Threads") },
    { 46, QT_TRANSLATE_NOOP("AuditLogFormatter", "Send Voice Messages") },
    { 49, QT_TRANSLATE_NOOP("AuditLogFormatter", "Create Polls") },
    { 51, QT_TRANSLATE_NOOP("AuditLogFormatter", "Pin Messages") },
    { 52, QT_TRANSLATE_NOOP("AuditLogFormatter", "Bypass Slowmode") },
    { 53, QT_TRANSLATE_NOOP("AuditLogFormatter", "Manage Official Messages") },
    { 20, QT_TRANSLATE_NOOP("AuditLogFormatter", "Connect") },
    { 21, QT_TRANSLATE_NOOP("AuditLogFormatter", "Speak") },
    { 22, QT_TRANSLATE_NOOP("AuditLogFormatter", "Mute Members") },
    { 23, QT_TRANSLATE_NOOP("AuditLogFormatter", "Deafen Members") },
    { 24, QT_TRANSLATE_NOOP("AuditLogFormatter", "Move Members") },
    { 25, QT_TRANSLATE_NOOP("AuditLogFormatter", "Use Voice Activity") },
    { 8, QT_TRANSLATE_NOOP("AuditLogFormatter", "Priority Speaker") },
    { 9, QT_TRANSLATE_NOOP("AuditLogFormatter", "Video") },
    { 42, QT_TRANSLATE_NOOP("AuditLogFormatter", "Use Soundboard") },
    { 45, QT_TRANSLATE_NOOP("AuditLogFormatter", "Use External Sounds") },
    { 48, QT_TRANSLATE_NOOP("AuditLogFormatter", "Set Voice Channel Status") },
    { 32, QT_TRANSLATE_NOOP("AuditLogFormatter", "Request to Speak") },
    { 33, QT_TRANSLATE_NOOP("AuditLogFormatter", "Manage Events") },
    { 44, QT_TRANSLATE_NOOP("AuditLogFormatter", "Create Events") },
};

QStringList permissionNames(quint64 bits, bool onChannel)
{
    QStringList names;
    for (const PermissionName &permission : PermissionNames) {
        if (!(bits & (quint64(1) << permission.bit)))
            continue;
        if (onChannel && permission.bit == 4)
            names.append(tr("Manage Channel"));
        else if (onChannel && permission.bit == 10)
            names.append(tr("View Channel"));
        else
            names.append(tr(permission.name));
    }
    return names;
}

QString channelTypeName(qint64 type)
{
    switch (type) {
    case 0:
        return tr("Text Channel");
    case 1:
        return tr("Direct Message");
    case 2:
        return tr("Voice Channel");
    case 3:
        return tr("Group DM");
    case 4:
        return tr("Category");
    case 5:
        return tr("Announcement Channel");
    case 6:
        return tr("Store Channel");
    case 13:
        return tr("Stage Channel");
    case 15:
        return tr("Forum Channel");
    case 16:
        return tr("Media Channel");
    }
    return QString::number(type);
}

bool isTextLikeChannelType(qint64 type)
{
    switch (type) {
    case 0:
    case 1:
    case 3:
    case 5:
    case 6:
    case 10:
    case 11:
    case 12:
    case 14:
    case 15:
    case 16:
        return true;
    }
    return false;
}

QString inviteMaxAgeText(qint64 seconds)
{
    switch (seconds) {
    case 0:
        return tr("Never");
    case 1800:
        return tr("30 minutes");
    case 3600:
        return tr("1 hour");
    case 21600:
        return tr("6 hours");
    case 43200:
        return tr("12 hours");
    case 86400:
        return tr("1 day");
    case 604800:
        return tr("7 days");
    case 1209600:
        return tr("14 days");
    case 2592000:
        return tr("30 days");
    case 5184000:
        return tr("60 days");
    }
    return QString::number(seconds);
}

QString localeName(const QString &code)
{
    static const QHash<QString, QString> names = {
        { "en-US", "English, US" },
        { "en-GB", "English, UK" },
        { "zh-CN", "中文" },
        { "zh-TW", "繁體中文" },
        { "cs", "Čeština" },
        { "da", "Dansk" },
        { "nl", "Nederlands" },
        { "fr", "Français" },
        { "de", "Deutsch" },
        { "el", "Ελληνικά" },
        { "hu", "Magyar" },
        { "it", "Italiano" },
        { "ja", "日本語" },
        { "ko", "한국어" },
        { "pl", "Polski" },
        { "pt-BR", "Português do Brasil" },
        { "ru", "Русский" },
        { "es-419", "Español, LATAM" },
        { "es-ES", "Español" },
        { "sv-SE", "Svenska" },
        { "tr", "Türkçe" },
        { "bg", "български" },
        { "uk", "Українська" },
        { "fi", "Suomi" },
        { "no", "Norsk" },
        { "hr", "Hrvatski" },
        { "ro", "Română" },
        { "lt", "Lietuviškai" },
        { "th", "ไทย" },
        { "vi", "Tiếng Việt" },
        { "hi", "हिंदी" },
    };
    const auto found = names.constFind(code);
    return found != names.constEnd() ? found.value() : code;
}

QString autoModTriggerName(qint64 type)
{
    switch (type) {
    case 1:
        return tr("Block Custom Words");
    case 3:
        return tr("Block Suspected Spam Content");
    case 4:
        return tr("Block Commonly Flagged Words");
    case 5:
        return tr("Block Mention Spam");
    case 6:
        return tr("Block Words in Member Profile Names");
    }
    return tr("Unknown");
}

QString autoModEventName(qint64 type)
{
    switch (type) {
    case 1:
        return tr("Message Send");
    case 2:
        return tr("Member join or update");
    }
    return tr("Unknown");
}

QString autoModActionName(qint64 type)
{
    switch (type) {
    case 1:
        return tr("Block message");
    case 2:
        return tr("Send alert");
    case 3:
        return tr("Timeout user");
    case 4:
        return tr("Block member interactions");
    }
    return tr("Unknown");
}

QString quotedList(const QJsonValue &value)
{
    QStringList quoted;
    for (const QJsonValue &item : value.toArray())
        quoted.append(QStringLiteral("'%1'").arg(item.toString()));
    return quoted.join(QStringLiteral(", "));
}

QString integrationPlatform(const QString &type)
{
    if (type == QLatin1String("twitch"))
        return QStringLiteral("Twitch");
    if (type == QLatin1String("youtube"))
        return QStringLiteral("YouTube");
    if (type == QLatin1String("discord"))
        return QStringLiteral("Discord");
    return tr("Unknown Integration");
}

QString tagText(const QJsonObject &tag)
{
    const QString emoji = tag.value("emoji_name").toString();
    const QString name = tag.value("name").toString();
    return emoji.isEmpty() ? name : emoji + QLatin1Char(' ') + name;
}

} // namespace

AuditLogFormatter::AuditLogFormatter(Core::ClientInstance *instance, Core::Snowflake guildId)
    : instance(instance), guildId(guildId)
{
}

AuditLogFormatter::Target AuditLogFormatter::targetOf(Action action)
{
    switch (action) {
    case Action::GUILD_UPDATE:
    case Action::CREATOR_MONETIZATION_REQUEST_CREATED:
    case Action::CREATOR_MONETIZATION_TERMS_ACCEPTED:
    case Action::HARMFUL_LINKS_BLOCKED_MESSAGE:
    case Action::GUILD_MIGRATE_PIN_PERMISSION:
    case Action::GUILD_MIGRATE_BYPASS_SLOWMODE_PERMISSION:
        return Target::Guild;
    case Action::CHANNEL_CREATE:
    case Action::CHANNEL_UPDATE:
    case Action::CHANNEL_DELETE:
    case Action::MESSAGE_BULK_DELETE:
        return Target::Channel;
    case Action::CHANNEL_OVERWRITE_CREATE:
    case Action::CHANNEL_OVERWRITE_UPDATE:
    case Action::CHANNEL_OVERWRITE_DELETE:
        return Target::ChannelOverwrite;
    case Action::MEMBER_KICK:
    case Action::MEMBER_PRUNE:
    case Action::MEMBER_BAN_ADD:
    case Action::MEMBER_BAN_REMOVE:
    case Action::MEMBER_UPDATE:
    case Action::MEMBER_ROLE_UPDATE:
    case Action::MEMBER_MOVE:
    case Action::MEMBER_DISCONNECT:
    case Action::BOT_ADD:
    case Action::MESSAGE_DELETE:
    case Action::MESSAGE_PIN:
    case Action::MESSAGE_UNPIN:
    case Action::AUTO_MODERATION_BLOCK_MESSAGE:
    case Action::AUTO_MODERATION_FLAG_TO_CHANNEL:
    case Action::AUTO_MODERATION_USER_COMMUNICATION_DISABLED:
    case Action::AUTO_MODERATION_QUARANTINE_USER:
        return Target::User;
    case Action::ROLE_CREATE:
    case Action::ROLE_UPDATE:
    case Action::ROLE_DELETE:
        return Target::Role;
    case Action::INVITE_CREATE:
    case Action::INVITE_UPDATE:
    case Action::INVITE_DELETE:
        return Target::Invite;
    case Action::WEBHOOK_CREATE:
    case Action::WEBHOOK_UPDATE:
    case Action::WEBHOOK_DELETE:
        return Target::Webhook;
    case Action::EMOJI_CREATE:
    case Action::EMOJI_UPDATE:
    case Action::EMOJI_DELETE:
        return Target::Emoji;
    case Action::INTEGRATION_CREATE:
    case Action::INTEGRATION_UPDATE:
    case Action::INTEGRATION_DELETE:
        return Target::Integration;
    case Action::STAGE_INSTANCE_CREATE:
    case Action::STAGE_INSTANCE_UPDATE:
    case Action::STAGE_INSTANCE_DELETE:
        return Target::StageInstance;
    case Action::STICKER_CREATE:
    case Action::STICKER_UPDATE:
    case Action::STICKER_DELETE:
        return Target::Sticker;
    case Action::GUILD_SCHEDULED_EVENT_CREATE:
    case Action::GUILD_SCHEDULED_EVENT_UPDATE:
    case Action::GUILD_SCHEDULED_EVENT_DELETE:
        return Target::ScheduledEvent;
    case Action::GUILD_SCHEDULED_EVENT_EXCEPTION_CREATE:
    case Action::GUILD_SCHEDULED_EVENT_EXCEPTION_UPDATE:
    case Action::GUILD_SCHEDULED_EVENT_EXCEPTION_DELETE:
        return Target::ScheduledEventException;
    case Action::THREAD_CREATE:
    case Action::THREAD_UPDATE:
    case Action::THREAD_DELETE:
        return Target::Thread;
    case Action::APPLICATION_COMMAND_PERMISSION_UPDATE:
        return Target::ApplicationCommand;
    case Action::SOUNDBOARD_SOUND_CREATE:
    case Action::SOUNDBOARD_SOUND_UPDATE:
    case Action::SOUNDBOARD_SOUND_DELETE:
        return Target::Soundboard;
    case Action::AUTO_MODERATION_RULE_CREATE:
    case Action::AUTO_MODERATION_RULE_UPDATE:
    case Action::AUTO_MODERATION_RULE_DELETE:
        return Target::AutoModerationRule;
    case Action::ONBOARDING_PROMPT_CREATE:
    case Action::ONBOARDING_PROMPT_UPDATE:
    case Action::ONBOARDING_PROMPT_DELETE:
        return Target::OnboardingPrompt;
    case Action::ONBOARDING_CREATE:
    case Action::ONBOARDING_UPDATE:
        return Target::Onboarding;
    case Action::GUILD_HOME_FEATURE_ITEM:
    case Action::GUILD_HOME_REMOVE_ITEM:
        return Target::GuildHome;
    case Action::HOME_SETTINGS_CREATE:
    case Action::HOME_SETTINGS_UPDATE:
        return Target::HomeSettings;
    case Action::VOICE_CHANNEL_STATUS_CREATE:
    case Action::VOICE_CHANNEL_STATUS_DELETE:
        return Target::VoiceChannelStatus;
    case Action::GUILD_MEMBER_VERIFICATION_UPDATE:
        return Target::MemberVerification;
    case Action::GUILD_PROFILE_UPDATE:
        return Target::GuildProfile;
    }
    return Target::Unknown;
}

AuditLogFormatter::Category AuditLogFormatter::categoryOf(Action action)
{
    switch (action) {
    case Action::CHANNEL_CREATE:
    case Action::CHANNEL_OVERWRITE_CREATE:
    case Action::MEMBER_BAN_REMOVE:
    case Action::BOT_ADD:
    case Action::ROLE_CREATE:
    case Action::INVITE_CREATE:
    case Action::WEBHOOK_CREATE:
    case Action::EMOJI_CREATE:
    case Action::MESSAGE_PIN:
    case Action::INTEGRATION_CREATE:
    case Action::STAGE_INSTANCE_CREATE:
    case Action::STICKER_CREATE:
    case Action::GUILD_SCHEDULED_EVENT_CREATE:
    case Action::THREAD_CREATE:
    case Action::SOUNDBOARD_SOUND_CREATE:
    case Action::AUTO_MODERATION_RULE_CREATE:
    case Action::CREATOR_MONETIZATION_REQUEST_CREATED:
    case Action::ONBOARDING_PROMPT_CREATE:
    case Action::ONBOARDING_CREATE:
    case Action::GUILD_HOME_FEATURE_ITEM:
    case Action::HOME_SETTINGS_CREATE:
    case Action::VOICE_CHANNEL_STATUS_CREATE:
        return Category::Create;
    case Action::CHANNEL_DELETE:
    case Action::CHANNEL_OVERWRITE_DELETE:
    case Action::MEMBER_KICK:
    case Action::MEMBER_PRUNE:
    case Action::MEMBER_BAN_ADD:
    case Action::MEMBER_DISCONNECT:
    case Action::ROLE_DELETE:
    case Action::INVITE_DELETE:
    case Action::WEBHOOK_DELETE:
    case Action::EMOJI_DELETE:
    case Action::MESSAGE_DELETE:
    case Action::MESSAGE_BULK_DELETE:
    case Action::MESSAGE_UNPIN:
    case Action::INTEGRATION_DELETE:
    case Action::STAGE_INSTANCE_DELETE:
    case Action::STICKER_DELETE:
    case Action::GUILD_SCHEDULED_EVENT_DELETE:
    case Action::THREAD_DELETE:
    case Action::SOUNDBOARD_SOUND_DELETE:
    case Action::AUTO_MODERATION_RULE_DELETE:
    case Action::AUTO_MODERATION_BLOCK_MESSAGE:
    case Action::ONBOARDING_PROMPT_DELETE:
    case Action::GUILD_HOME_REMOVE_ITEM:
    case Action::VOICE_CHANNEL_STATUS_DELETE:
    case Action::GUILD_SCHEDULED_EVENT_EXCEPTION_DELETE:
        return Category::Delete;
    case Action::HARMFUL_LINKS_BLOCKED_MESSAGE:
        return Category::Other;
    default:
        return Category::Update;
    }
}

bool AuditLogFormatter::neverMerged(Action action)
{
    switch (action) {
    case Action::MESSAGE_DELETE:
    case Action::MESSAGE_BULK_DELETE:
    case Action::MESSAGE_PIN:
    case Action::MESSAGE_UNPIN:
    case Action::MEMBER_MOVE:
    case Action::MEMBER_DISCONNECT:
    case Action::BOT_ADD:
    case Action::APPLICATION_COMMAND_PERMISSION_UPDATE:
    case Action::MEMBER_PRUNE:
        return true;
    default:
        return false;
    }
}

QList<AuditLogFormatter::Row> AuditLogFormatter::addPage(const Discord::AuditLog &page)
{
    for (const Discord::User &user : page.users)
        users.insert(user.id.get(), user);
    auto index = [](QHash<Core::Snowflake, QJsonObject> &into, const QJsonArray &items) {
        for (const QJsonValue &item : items)
            into.insert(snowflakeOf(item.toObject().value("id")), item.toObject());
    };
    index(integrations, page.integrations);
    index(webhooks, page.webhooks);
    index(scheduledEvents, page.guildScheduledEvents);
    index(autoModerationRules, page.autoModerationRules);
    index(threads, page.threads);
    index(applicationCommands, page.applicationCommands);

    QList<Entry> entries;
    for (auto it = page.entries.crbegin(); it != page.entries.crend(); ++it) {
        const Discord::AuditLogEntry &raw = *it;
        Entry entry;
        entry.raw = raw;
        entry.action = raw.actionType.get();
        entry.target = targetOf(entry.action);
        entry.category = categoryOf(entry.action);
        entry.id = raw.id.get();
        entry.userId = raw.userId.hasValue() ? raw.userId.get() : Core::Snowflake();
        entry.targetId = raw.targetId.hasValue() ? raw.targetId.get() : Core::Snowflake();
        entry.time = entry.id.toDateTime();

        if (raw.reason.hasValue() && !raw.reason->isEmpty())
            entry.changes.append({ QStringLiteral("reason"), QJsonValue(), raw.reason.get() });
        for (const Discord::AuditLogChange &change : raw.changes)
            entry.changes.append({ change.key, change.oldValue, change.newValue });
        if (entry.action == Action::MEMBER_PRUNE)
            entry.changes.append({ QStringLiteral("prune_delete_days"), QJsonValue(),
                                   raw.options.contains("delete_member_days") ? raw.options.value("delete_member_days")
                                                                              : QJsonValue(1) });
        if (entry.action == Action::AUTO_MODERATION_BLOCK_MESSAGE && raw.options.contains("auto_moderation_rule_name"))
            entry.changes.append({ QStringLiteral("triggered_rule_name"), QJsonValue(), raw.options.value("auto_moderation_rule_name") });
        if (entry.action == Action::VOICE_CHANNEL_STATUS_CREATE && raw.options.contains("status"))
            entry.changes.append({ QStringLiteral("status"), QJsonValue(), raw.options.value("status") });
        entries.append(entry);
    }

    QList<QList<Entry>> groups;
    for (const Entry &entry : entries) {
        if (!groups.isEmpty()) {
            const Entry &first = groups.last().first();
            const bool mergeable = first.action == entry.action &&
                                   first.targetId == entry.targetId &&
                                   first.userId == entry.userId &&
                                   first.raw.options == entry.raw.options &&
                                   first.time.secsTo(entry.time) < MergeWindowSecs &&
                                   groups.last().size() <= MaxMerges &&
                                   entry.target != Target::Invite &&
                                   !neverMerged(entry.action);
            if (mergeable) {
                groups.last().append(entry);
                continue;
            }
        }
        rememberDeletedName(entry);
        groups.append({ entry });
    }

    QList<Row> rows;
    for (auto it = groups.crbegin(); it != groups.crend(); ++it) {
        const Entry &first = it->first();
        const bool keptWithoutTarget = first.action == Action::MEMBER_PRUNE ||
                                       first.action == Action::MEMBER_DISCONNECT ||
                                       first.action == Action::MEMBER_MOVE ||
                                       first.action == Action::CREATOR_MONETIZATION_REQUEST_CREATED ||
                                       first.action == Action::CREATOR_MONETIZATION_TERMS_ACCEPTED;
        if (first.target == Target::Unknown)
            continue;
        if (targetName(first).isEmpty() && !keptWithoutTarget)
            continue;
        rows.append(format(*it));
    }
    return rows;
}

void AuditLogFormatter::rememberDeletedName(const Entry &entry)
{
    if (entry.category != Category::Delete || !entry.targetId.isValid())
        return;

    QString name;
    bool textLike = false;
    for (const Change &change : entry.changes) {
        if ((change.key == QLatin1String("name") || change.key == QLatin1String("title")) && name.isEmpty())
            name = change.oldValue.toString();
        if (change.key == QLatin1String("type"))
            textLike = isTextLikeChannelType(number(change.oldValue));
    }
    if (name.isEmpty())
        return;
    if ((entry.target == Target::Channel || entry.target == Target::ChannelOverwrite) && textLike)
        name.prepend(QLatin1Char('#'));
    deletedNames.insert(qMakePair(int(entry.target), entry.targetId), name);
}

std::optional<Discord::User> AuditLogFormatter::user(Core::Snowflake userId) const
{
    const auto known = users.constFind(userId);
    if (known != users.constEnd())
        return known.value();
    return instance ? instance->users()->getUser(userId) : std::nullopt;
}

QString AuditLogFormatter::userTag(Core::Snowflake userId) const
{
    const auto found = user(userId);
    return found ? found->tag() : QString::number(quint64(userId));
}

QString AuditLogFormatter::channelName(Core::Snowflake channelId) const
{
    const auto channel = instance ? instance->getChannel(channelId) : std::nullopt;
    if (!channel || !channel->name.hasValue())
        return QString::number(quint64(channelId));

    const QString name = channel->name.get();
    switch (channel->type.valueOr(Discord::ChannelType::GUILD_TEXT)) {
    case Discord::ChannelType::GUILD_VOICE:
    case Discord::ChannelType::GUILD_STAGE_VOICE:
    case Discord::ChannelType::GUILD_CATEGORY:
        return name;
    case Discord::ChannelType::NEWS_THREAD:
    case Discord::ChannelType::PUBLIC_THREAD:
    case Discord::ChannelType::PRIVATE_THREAD:
        return QStringLiteral("\"%1\"").arg(name);
    default:
        return QLatin1Char('#') + name;
    }
}

QString AuditLogFormatter::roleName(Core::Snowflake roleId) const
{
    if (instance)
        for (const Discord::Role &role : instance->getRolesForGuild(guildId))
            if (role.id.get() == roleId)
                return role.name.get();
    return {};
}

QString AuditLogFormatter::authorName(const Entry &entry) const
{
    if (entry.userId.isValid()) {
        if (const auto found = user(entry.userId))
            return found->tag();
    }
    if (entry.raw.options.contains("integration_type"))
        return integrationPlatform(entry.raw.options.value("integration_type").toString());
    return tr("Unknown User");
}

QString AuditLogFormatter::targetName(const Entry &entry) const
{
    const Core::Snowflake id = entry.targetId;
    auto changedValue = [&entry](const char *key) -> QString {
        for (const Change &change : entry.changes)
            if (change.key == QLatin1String(key))
                return text(truthy(change.newValue) ? change.newValue : change.oldValue);
        return {};
    };
    auto lookedUp = [&id](const QHash<Core::Snowflake, QJsonObject> &lookup, const char *field) -> QString {
        const auto found = lookup.constFind(id);
        return found != lookup.constEnd() ? found->value(field).toString() : QString();
    };
    auto fallback = [&](const QString &found, const char *key) -> QString {
        if (!found.isEmpty())
            return found;
        const auto deleted = deletedNames.constFind(qMakePair(int(entry.target), id));
        if (deleted != deletedNames.constEnd())
            return deleted.value();
        const QString changed = key ? changedValue(key) : QString();
        if (!changed.isEmpty())
            return changed;
        return id.isValid() ? QString::number(quint64(id)) : QString();
    };

    switch (entry.target) {
    case Target::Guild:
    case Target::GuildHome:
    case Target::GuildProfile:
    case Target::Onboarding:
    case Target::MemberVerification: {
        const auto guild = instance ? instance->getGuild(guildId) : std::nullopt;
        return guild ? guild->name.get() : QString();
    }
    case Target::Channel:
    case Target::ChannelOverwrite:
    case Target::VoiceChannelStatus: {
        const auto channel = instance && id.isValid() ? instance->getChannel(id) : std::nullopt;
        return fallback(channel ? channelName(id) : QString(), entry.target == Target::VoiceChannelStatus ? "status" : "name");
    }
    case Target::User: {
        const auto found = id.isValid() ? user(id) : std::nullopt;
        return fallback(found ? found->tag() : QString(), "nick");
    }
    case Target::Role:
        return fallback(id.isValid() ? roleName(id) : QString(), "name");
    case Target::OnboardingPrompt: {
        const QString title = changedValue("title");
        return title.isEmpty() ? tr("[empty]") : title;
    }
    case Target::Invite:
        return fallback(QString(), "code");
    case Target::Integration:
        return fallback(lookedUp(integrations, "name"), "type");
    case Target::Webhook:
        return fallback(lookedUp(webhooks, "name"), "name");
    case Target::Emoji:
    case Target::Sticker:
    case Target::Soundboard:
        return fallback(QString(), "name");
    case Target::StageInstance:
        return fallback(QString(), "topic");
    case Target::ScheduledEvent:
    case Target::ScheduledEventException:
        return fallback(lookedUp(scheduledEvents, "name"), "name");
    case Target::Thread:
        return fallback(lookedUp(threads, "name"), "name");
    case Target::AutoModerationRule:
        return fallback(lookedUp(autoModerationRules, "name"), "name");
    case Target::ApplicationCommand: {
        const Core::Snowflake applicationId = snowflakeOf(entry.raw.options.value("application_id"));
        if (id == applicationId) {
            for (const QJsonObject &integration : integrations)
                if (snowflakeOf(integration.value("application").toObject().value("id")) == applicationId)
                    return integration.value("name").toString();
            return QString::number(quint64(id));
        }
        const auto command = applicationCommands.constFind(id);
        if (command == applicationCommands.constEnd())
            return fallback(QString(), "name");
        QString name = command->value("name_localized").toString();
        if (name.isEmpty())
            name = command->value("name").toString();
        return command->value("type").toInt(1) == 1 ? QStringLiteral("/") + QChar(0x2060) + name : name;
    }
    case Target::HomeSettings:
        return tr("Server Guide");
    case Target::Unknown:
        break;
    }
    return {};
}

QString AuditLogFormatter::titleFor(const Entry &entry, const QString &target, const QString &count, const QString &channel, const QString &subtarget) const
{
    const QString u = bold(authorName(entry));
    const QString t = bold(target);
    const QString c = bold(channel);
    const qint64 n = count.toLongLong();
    auto counted = [n](const char *one, const char *other) { return QStringLiteral("<b>%1</b>").arg(countText(n, one, other).toHtmlEscaped()); };
    auto typeChange = [&entry]() -> std::optional<qint64> {
        for (const Change &change : entry.changes)
            if (change.key == QLatin1String("type") && !isNull(change.newValue))
                return number(change.newValue);
        return std::nullopt;
    };

    switch (entry.action) {
    case Action::GUILD_UPDATE:
    case Action::CHANNEL_UPDATE:
        return tr("%1 made changes to %2").arg(u, t);
    case Action::CHANNEL_CREATE:
        switch (typeChange().value_or(0)) {
        case 13:
            return tr("%1 created a stage channel %2").arg(u, t);
        case 2:
            return tr("%1 created a voice channel %2").arg(u, t);
        case 4:
            return tr("%1 created a category %2").arg(u, t);
        case 15:
            return tr("%1 created a forum channel %2").arg(u, t);
        case 16:
            return tr("%1 created a media channel %2").arg(u, t);
        case 5:
            return tr("%1 created an announcement channel %2").arg(u, t);
        default:
            return tr("%1 created a text channel %2").arg(u, t);
        }
    case Action::CHANNEL_DELETE:
        return tr("%1 removed %2").arg(u, t);
    case Action::CHANNEL_OVERWRITE_CREATE:
        return tr("%1 created channel overrides for %2").arg(u, t);
    case Action::CHANNEL_OVERWRITE_UPDATE:
        return tr("%1 updated channel overrides for %2").arg(u, t);
    case Action::CHANNEL_OVERWRITE_DELETE:
        return tr("%1 removed channel overrides for %2").arg(u, t);
    case Action::MEMBER_KICK:
        return tr("%1 kicked %2").arg(u, t);
    case Action::MEMBER_PRUNE:
        return tr("%1 pruned %2").arg(u, counted(QT_TRANSLATE_NOOP("AuditLogFormatter", "a member"), QT_TRANSLATE_NOOP("AuditLogFormatter", "%1 members")));
    case Action::MEMBER_BAN_ADD:
        return tr("%1 banned %2").arg(u, t);
    case Action::MEMBER_BAN_REMOVE:
        return tr("%1 removed the ban for %2").arg(u, t);
    case Action::MEMBER_UPDATE:
        return tr("%1 updated %2").arg(u, t);
    case Action::MEMBER_ROLE_UPDATE:
        return tr("%1 updated roles for %2").arg(u, t);
    case Action::MEMBER_MOVE:
        return tr("%1 moved %2 to %3").arg(u, counted(QT_TRANSLATE_NOOP("AuditLogFormatter", "a user"), QT_TRANSLATE_NOOP("AuditLogFormatter", "%1 users")), c);
    case Action::MEMBER_DISCONNECT:
        return tr("%1 disconnected %2 from voice")
                .arg(u, counted(QT_TRANSLATE_NOOP("AuditLogFormatter", "a user"), QT_TRANSLATE_NOOP("AuditLogFormatter", "%1 users")));
    case Action::BOT_ADD:
        return tr("%1 added %2 to the server").arg(u, t);
    case Action::ROLE_CREATE:
        return tr("%1 created the role %2").arg(u, t);
    case Action::ROLE_UPDATE:
        return tr("%1 updated the role %2").arg(u, t);
    case Action::ROLE_DELETE:
        return tr("%1 deleted the role %2").arg(u, t);
    case Action::INVITE_CREATE:
        return tr("%1 created an invite %2").arg(u, t);
    case Action::INVITE_UPDATE:
        return tr("%1 updated an invite %2").arg(u, t);
    case Action::INVITE_DELETE:
        return tr("%1 deleted an invite %2").arg(u, t);
    case Action::WEBHOOK_CREATE:
        return tr("%1 created the webhook %2").arg(u, t);
    case Action::WEBHOOK_UPDATE:
        return tr("%1 updated the webhook %2").arg(u, t);
    case Action::WEBHOOK_DELETE:
        return tr("%1 deleted the webhook %2").arg(u, t);
    case Action::EMOJI_CREATE:
        return tr("%1 created the emoji %2").arg(u, t);
    case Action::EMOJI_UPDATE:
        return tr("%1 updated the emoji %2").arg(u, t);
    case Action::EMOJI_DELETE:
        return tr("%1 deleted the emoji %2").arg(u, t);
    case Action::STICKER_CREATE:
        return tr("%1 created the sticker %2").arg(u, t);
    case Action::STICKER_UPDATE:
        return tr("%1 updated the sticker %2").arg(u, t);
    case Action::STICKER_DELETE:
        return tr("%1 deleted the sticker %2").arg(u, t);
    case Action::MESSAGE_DELETE:
        return tr("%1 deleted %2 by %3 in %4")
                .arg(u, counted(QT_TRANSLATE_NOOP("AuditLogFormatter", "a message"), QT_TRANSLATE_NOOP("AuditLogFormatter", "%1 messages")),
                     t, c);
    case Action::MESSAGE_BULK_DELETE:
        return tr("%1 deleted %2 in %3")
                .arg(u, counted(QT_TRANSLATE_NOOP("AuditLogFormatter", "a message"), QT_TRANSLATE_NOOP("AuditLogFormatter", "%1 messages")),
                     t);
    case Action::MESSAGE_PIN:
        return tr("%1 pinned a message by %2 in %3").arg(u, t, c);
    case Action::MESSAGE_UNPIN:
        return tr("%1 unpinned a message by %2 in %3").arg(u, t, c);
    case Action::INTEGRATION_CREATE:
        return tr("%1 added an integration for %2").arg(u, t);
    case Action::INTEGRATION_UPDATE:
        return tr("%1 updated the integration for %2").arg(u, t);
    case Action::INTEGRATION_DELETE:
        return tr("%1 deleted the integration for %2").arg(u, t);
    case Action::STAGE_INSTANCE_CREATE:
        return tr("%1 started the stage for %2").arg(u, c);
    case Action::STAGE_INSTANCE_UPDATE:
        return tr("%1 updated the stage for %2").arg(u, c);
    case Action::STAGE_INSTANCE_DELETE:
        if (!entry.userId.isValid())
            return tr("Discord ended the stage for %1 due to inactivity.").arg(c);
        return tr("%1 ended the stage for %2").arg(u, c);
    case Action::GUILD_SCHEDULED_EVENT_CREATE:
        return tr("%1 scheduled the event %2").arg(u, t);
    case Action::GUILD_SCHEDULED_EVENT_UPDATE:
        return tr("%1 updated the scheduled event %2").arg(u, t);
    case Action::GUILD_SCHEDULED_EVENT_DELETE:
        return tr("%1 canceled the scheduled event %2").arg(u, t);
    case Action::GUILD_SCHEDULED_EVENT_EXCEPTION_CREATE:
    case Action::GUILD_SCHEDULED_EVENT_EXCEPTION_UPDATE:
        return tr("%1 updated an event recurrence for %2 at %3").arg(u, t, bold(subtarget));
    case Action::GUILD_SCHEDULED_EVENT_EXCEPTION_DELETE:
        return tr("%1 reset an event recurrence for %2 at %3").arg(u, t, bold(subtarget));
    case Action::THREAD_CREATE:
        switch (typeChange().value_or(11)) {
        case 12:
            return tr("%1 created a private thread %2").arg(u, t);
        case 10:
            return tr("%1 created an announcement thread %2").arg(u, t);
        default:
            return tr("%1 created a thread %2").arg(u, t);
        }
    case Action::THREAD_UPDATE:
        return tr("%1 made changes to the thread %2").arg(u, t);
    case Action::THREAD_DELETE:
        return tr("%1 deleted the thread %2").arg(u, t);
    case Action::APPLICATION_COMMAND_PERMISSION_UPDATE:
        return tr("%1 updated permissions for %2").arg(u, t);
    case Action::AUTO_MODERATION_BLOCK_MESSAGE:
        return tr("AutoMod blocked a message sent by %1 in %2").arg(t, c);
    case Action::AUTO_MODERATION_FLAG_TO_CHANNEL:
        if (entry.raw.options.value("auto_moderation_rule_trigger_type").toString() == QLatin1String("6"))
            return tr("AutoMod flagged %1 for violating content in their user profile").arg(u);
        return tr("AutoMod flagged a message sent by %1 in %2").arg(t, c);
    case Action::AUTO_MODERATION_USER_COMMUNICATION_DISABLED:
        return tr("AutoMod timed out %1 for a message posted in %2").arg(t, c);
    case Action::AUTO_MODERATION_QUARANTINE_USER:
        return tr("AutoMod quarantined %1 for violating content in their user profile").arg(u);
    case Action::CREATOR_MONETIZATION_REQUEST_CREATED:
        return tr("%1 applied for monetization").arg(u);
    case Action::CREATOR_MONETIZATION_TERMS_ACCEPTED:
        return tr("%1 accepted terms for monetization").arg(u);
    case Action::AUTO_MODERATION_RULE_CREATE:
        return tr("%1 created AutoMod rule %2").arg(u, t);
    case Action::AUTO_MODERATION_RULE_UPDATE:
        return tr("%1 updated AutoMod rule %2").arg(u, t);
    case Action::AUTO_MODERATION_RULE_DELETE:
        return tr("%1 deleted AutoMod rule %2").arg(u, t);
    case Action::ONBOARDING_PROMPT_CREATE:
        return tr("%1 created a new customization question").arg(u);
    case Action::ONBOARDING_PROMPT_UPDATE:
        return tr("%1 updated the customization question %2").arg(u, t);
    case Action::ONBOARDING_PROMPT_DELETE:
        return tr("%1 deleted the customization question %2").arg(u, t);
    case Action::ONBOARDING_CREATE:
        return tr("%1 started onboarding for this server").arg(u);
    case Action::ONBOARDING_UPDATE:
        return tr("%1 updated onboarding for this server").arg(u);
    case Action::HOME_SETTINGS_CREATE:
        return tr("%1 created the Server Guide for this server").arg(u);
    case Action::HOME_SETTINGS_UPDATE:
        return tr("%1 updated the Server Guide for this server").arg(u);
    case Action::GUILD_HOME_FEATURE_ITEM: {
        for (const Change &change : entry.changes) {
            if (change.key != QLatin1String("entity_type"))
                continue;
            if (change.newValue.toString() == QLatin1String("message"))
                return tr("%1 featured a message on Home").arg(u);
            if (change.newValue.toString() == QLatin1String("forum_post"))
                return tr("%1 featured a forum post on Home").arg(u);
        }
        return tr("%1 featured something on Home").arg(u);
    }
    case Action::GUILD_HOME_REMOVE_ITEM:
        return tr("%1 removed an item from Home").arg(u);
    case Action::SOUNDBOARD_SOUND_CREATE:
        return tr("%1 uploaded a Soundboard sound").arg(u);
    case Action::SOUNDBOARD_SOUND_UPDATE:
        return tr("%1 updated a Soundboard sound").arg(u);
    case Action::SOUNDBOARD_SOUND_DELETE:
        return tr("%1 deleted a Soundboard sound").arg(u);
    case Action::VOICE_CHANNEL_STATUS_CREATE:
        return tr("%1 set a Voice Channel Status in %2").arg(u, c);
    case Action::VOICE_CHANNEL_STATUS_DELETE:
        return tr("%1 removed a Voice Channel Status in %2").arg(u, c);
    case Action::GUILD_MEMBER_VERIFICATION_UPDATE:
        return tr("%1 updated member application settings").arg(u);
    case Action::GUILD_PROFILE_UPDATE:
        return tr("%1 updated server profile settings").arg(u);
    case Action::GUILD_MIGRATE_PIN_PERMISSION:
        return tr("%1 updated Pin Messages permission").arg(u);
    case Action::GUILD_MIGRATE_BYPASS_SLOWMODE_PERMISSION:
        return tr("%1 updated Bypass Slowmode permission").arg(u);
    case Action::HARMFUL_LINKS_BLOCKED_MESSAGE:
        break;
    }
    return {};
}

QList<AuditLogFormatter::Change> AuditLogFormatter::transform(const Entry &entry) const
{
    QList<Change> result;
    const bool onChannel = entry.target == Target::Channel || entry.target == Target::ChannelOverwrite;
    auto mapBoth = [](Change change, const std::function<QJsonValue(const QJsonValue &)> &map) {
        if (!isNull(change.oldValue))
            change.oldValue = map(change.oldValue);
        if (!isNull(change.newValue))
            change.newValue = map(change.newValue);
        return change;
    };
    auto channelValue = [this](const QJsonValue &value) { return QJsonValue(channelName(snowflakeOf(value))); };
    auto firstTagDifference = [](const Change &change) -> std::optional<Change> {
        QHash<QString, QJsonObject> before;
        for (const QJsonValue &tag : change.oldValue.toArray())
            before.insert(tag.toObject().value("id").toString(), tag.toObject());
        QSet<QString> after;
        for (const QJsonValue &tag : change.newValue.toArray()) {
            const QJsonObject added = tag.toObject();
            const QString id = added.value("id").toString();
            after.insert(id);
            const auto previous = before.constFind(id);
            if (previous == before.constEnd())
                return Change{ QStringLiteral("available_tag_add"), QJsonValue(), tagText(added) };
            if (previous.value() != added)
                return Change{ QStringLiteral("available_tag_edit"), tagText(previous.value()), tagText(added) };
        }
        for (const QJsonValue &tag : change.oldValue.toArray())
            if (!after.contains(tag.toObject().value("id").toString()))
                return Change{ QStringLiteral("available_tag_delete"), QJsonValue(), tagText(tag.toObject()) };
        return std::nullopt;
    };

    for (Change change : entry.changes) {
        const QString &key = change.key;

        if (entry.action == Action::APPLICATION_COMMAND_PERMISSION_UPDATE && key != QLatin1String("reason")) {
            const QJsonObject permission = (isNull(change.oldValue) ? change.newValue : change.oldValue).toObject();
            const Core::Snowflake id = snowflakeOf(permission.value("id"));
            switch (permission.value("type").toInt()) {
            case 1:
                change.subtarget = roleName(id);
                break;
            case 2:
                change.subtarget = userTag(id);
                break;
            case 3:
                change.subtarget = quint64(id) == quint64(guildId) - 1 ? tr("all channels") : channelName(id);
                break;
            }
            if (change.subtarget.isEmpty())
                change.subtarget = QString::number(quint64(id));
            result.append(change);
            continue;
        }

        if (key == QLatin1String("owner_id")) {
            result.append(mapBoth(change, [this](const QJsonValue &value) {
                const auto owner = user(snowflakeOf(value));
                return QJsonValue(owner ? owner->username.get() : QStringLiteral("???"));
            }));
        } else if (key == QLatin1String("channel_id") ||
                   key == QLatin1String("afk_channel_id") ||
                   key == QLatin1String("system_channel_id") ||
                   key == QLatin1String("rules_channel_id") ||
                   key == QLatin1String("public_updates_channel_id") ||
                   key == QLatin1String("widget_channel_id")) {
            result.append(mapBoth(change, channelValue));
        } else if (key == QLatin1String("afk_timeout")) {
            result.append(mapBoth(change, [](const QJsonValue &value) { return QJsonValue(number(value) / 60); }));
        } else if (key == QLatin1String("bitrate")) {
            result.append(mapBoth(change, [](const QJsonValue &value) { return QJsonValue(number(value) / 1000); }));
        } else if (key == QLatin1String("color")) {
            result.append(mapBoth(change, [](const QJsonValue &value) { return QJsonValue(colorText(value)); }));
        } else if (key == QLatin1String("max_age")) {
            result.append(mapBoth(change, [](const QJsonValue &value) { return QJsonValue(inviteMaxAgeText(number(value))); }));
        } else if (key == QLatin1String("permissions") && entry.target == Target::Role) {
            const quint64 before = bitsOf(change.oldValue);
            const quint64 after = bitsOf(change.newValue);
            const QStringList granted = permissionNames(after & ~before, false);
            const QStringList denied = permissionNames(before & ~after, false);
            if (!granted.isEmpty())
                result.append({ QStringLiteral("allow"), QJsonValue(), QJsonValue(), QString(), granted });
            if (!denied.isEmpty())
                result.append({ QStringLiteral("deny"), QJsonValue(), QJsonValue(), QString(), denied });
        } else if ((key == QLatin1String("allow") || key == QLatin1String("deny")) && onChannel) {
            const quint64 before = bitsOf(change.oldValue);
            const quint64 after = bitsOf(change.newValue);
            const QStringList added = permissionNames(after & ~before, true);
            const QStringList reset = permissionNames(before & ~after, true);
            if (!added.isEmpty())
                result.append({ key, QJsonValue(), QJsonValue(), QString(), added });
            if (!reset.isEmpty())
                result.append({ QStringLiteral("reset"), QJsonValue(), QJsonValue(), QString(), reset });
        } else if (key == QLatin1String("flags")) {
            const quint64 before = quint64(number(change.oldValue));
            const quint64 after = quint64(number(change.newValue));
            if (entry.target == Target::Invite) {
                if ((after & ~before) == 1)
                    result.append({ key, QJsonValue(), QJsonValue(1) });
                continue;
            }
            QStringList items;
            const quint64 added = after & ~before;
            const quint64 removed = before & ~after;
            if (added & 1)
                items.append(tr("Removed channel from Home"));
            if (added & 2)
                items.append(tr("Pinned"));
            if (added & 4)
                items.append(tr("Removed channel from Active Now"));
            if (removed & 1)
                items.append(tr("Added channel back to Home"));
            if (removed & 2)
                items.append(tr("Unpinned"));
            if (removed & 4)
                items.append(tr("Added channel back to Active Now"));
            if (added || removed)
                result.append({ key, change.oldValue, change.newValue, QString(), items });
        } else if (key == QLatin1String("preferred_locale")) {
            result.append(mapBoth(change, [](const QJsonValue &value) { return QJsonValue(localeName(value.toString())); }));
        } else if (key == QLatin1String("video_quality_mode")) {
            result.append(mapBoth(change, [](const QJsonValue &value) { return QJsonValue(number(value) == 2 ? QStringLiteral("720p") : tr("Auto")); }));
        } else if (key == QLatin1String("system_channel_flags")) {
            const quint64 before = quint64(number(change.oldValue));
            const quint64 after = quint64(number(change.newValue));
            static const QPair<quint64, const char *> flagKeys[] = {
                { 1, "join_notifications" },
                { 2, "premium_subscriptions" },
                { 4, "reminder_notifications" },
                { 8, "join_notification_replies" },
            };
            for (const auto &flagKey : flagKeys)
                if ((before & flagKey.first) != (after & flagKey.first))
                    result.append({ QString::fromLatin1(flagKey.second), QJsonValue(!(before & flagKey.first)), QJsonValue(!(after & flagKey.first)) });
        } else if (key == QLatin1String("actions") && entry.target == Target::AutoModerationRule) {
            result.append(mapBoth(change, [](const QJsonValue &value) {
                QStringList names;
                for (const QJsonValue &action : value.toArray())
                    names.append(autoModActionName(action.toObject().value("type").toInt()));
                return QJsonValue(names.join(QStringLiteral(", ")));
            }));
        } else if (key == QLatin1String("event_type") && entry.target == Target::AutoModerationRule) {
            result.append(mapBoth(change, [](const QJsonValue &value) { return QJsonValue(autoModEventName(number(value))); }));
        } else if (key == QLatin1String("trigger_type") && entry.target == Target::AutoModerationRule) {
            result.append(mapBoth(change, [](const QJsonValue &value) { return QJsonValue(autoModTriggerName(number(value))); }));
        } else if (key == QLatin1String("trigger_metadata")) {
            result.append(mapBoth(change, [](const QJsonValue &value) {
                const QJsonObject metadata = value.toObject();
                if (metadata.value("keyword_filter").isArray())
                    return QJsonValue(tr("keywords to %1").arg(quotedList(metadata.value("keyword_filter"))));
                return QJsonValue(text(value));
            }));
        } else if (key.startsWith(QLatin1String("$add_")) || key.startsWith(QLatin1String("$remove_"))) {
            result.append(mapBoth(change, [](const QJsonValue &value) { return QJsonValue(quotedList(value)); }));
        } else if (key == QLatin1String("exempt_channels") || key == QLatin1String("exempt_roles")) {
            const bool channels = key == QLatin1String("exempt_channels");
            result.append(mapBoth(change, [this, channels](const QJsonValue &value) {
                QStringList names;
                for (const QJsonValue &id : value.toArray()) {
                    const QString name = channels ? (instance && instance->getChannel(snowflakeOf(id)) ? channelName(snowflakeOf(id)) : QString())
                                                  : roleName(snowflakeOf(id));
                    if (!name.isEmpty())
                        names.append(name);
                }
                return QJsonValue(names.isEmpty() ? tr("None") : names.join(QStringLiteral(", ")));
            }));
        } else if ((key == QLatin1String("$add") || key == QLatin1String("$remove")) && entry.target == Target::User) {
            for (const QJsonValue &role : change.newValue.toArray())
                change.items.append(role.toObject().value("name").toString());
            result.append(change);
        } else if (key == QLatin1String("role_ids") && entry.target == Target::Invite) {
            for (const QJsonValue &roleId : change.newValue.toArray()) {
                const QString name = roleName(snowflakeOf(roleId));
                if (!name.isEmpty())
                    change.items.append(name);
            }
            result.append(change);
        } else if (key == QLatin1String("available_tags")) {
            if (const auto difference = firstTagDifference(change))
                result.append(*difference);
        } else if (key == QLatin1String("applied_tags")) {
            QHash<Core::Snowflake, QString> tagNames;
            const auto thread = threads.constFind(entry.targetId);
            const Core::Snowflake forumId = thread != threads.constEnd() ? snowflakeOf(thread->value("parent_id")) : Core::Snowflake();
            if (const auto forum = instance && forumId.isValid() ? instance->getChannel(forumId) : std::nullopt; forum && forum->availableTags.hasValue())
                for (const Discord::ForumTag &tag : forum->availableTags.get())
                    tagNames.insert(tag.id.get(), tag.emojiName.hasValue() && !tag.emojiName->isEmpty()
                                                          ? tag.emojiName.get() + QLatin1Char(' ') + tag.name.get()
                                                          : tag.name.get());
            auto nameOf = [&tagNames](const QJsonValue &id) {
                const auto found = tagNames.constFind(snowflakeOf(id));
                return found != tagNames.constEnd() ? found.value() : id.toString();
            };
            QSet<QString> before;
            for (const QJsonValue &id : change.oldValue.toArray())
                before.insert(id.toString());
            QSet<QString> after;
            for (const QJsonValue &id : change.newValue.toArray())
                after.insert(id.toString());
            for (const QJsonValue &id : change.newValue.toArray())
                if (!before.contains(id.toString()))
                    result.append({ QStringLiteral("available_tag_add"), QJsonValue(), nameOf(id) });
            for (const QJsonValue &id : change.oldValue.toArray())
                if (!after.contains(id.toString()))
                    result.append({ QStringLiteral("available_tag_delete"), QJsonValue(), nameOf(id) });
        } else if (key == QLatin1String("scheduled_start_time") || key == QLatin1String("scheduled_end_time")) {
            result.append(mapBoth(change, [](const QJsonValue &value) { return QJsonValue(longDateTime(value)); }));
        } else if (key == QLatin1String("communication_disabled_until") && entry.action == Action::MEMBER_UPDATE) {
            result.append(mapBoth(change, [](const QJsonValue &value) {
                const QDateTime until = QDateTime::fromString(value.toString(), Qt::ISODateWithMs);
                return until.isValid() ? QJsonValue(calendar(until)) : value;
            }));
        } else if (key == QLatin1String("type") && onChannel) {
            result.append(mapBoth(change, [](const QJsonValue &value) { return QJsonValue(channelTypeName(number(value))); }));
        } else {
            result.append(change);
        }
    }
    return result;
}

std::optional<AuditLogFormatter::Line> AuditLogFormatter::describe(const Entry &entry, const Change &change) const
{
    const QString &key = change.key;
    const QJsonValue &oldValue = change.oldValue;
    const QJsonValue &newValue = change.newValue;
    const bool hasOld = !isNull(oldValue);
    const bool hasNew = !isNull(newValue);
    const QString oldText = bold(text(oldValue));
    const QString newText = bold(text(newValue));
    const qint64 count = change.items.isEmpty() && newValue.isArray() ? newValue.toArray().size() : change.items.size();

    auto line = [&change](const QString &html) { return std::optional<Line>(Line{ html, change.items }); };
    auto createdOrChanged = [&](const QString &created, const QString &changed) { return line(hasOld ? changed : created); };
    auto clearedOrSet = [&](const QString &cleared, const QString &set) { return line(hasNew ? set : cleared); };
    auto changedSetCleared = [&](const QString &changed, const QString &set, const QString &cleared) -> std::optional<Line> {
        if (hasOld && hasNew)
            return line(changed);
        if (hasNew)
            return line(set);
        if (hasOld)
            return line(cleared);
        return std::nullopt;
    };
    auto onOff = [&](const QString &on, const QString &off) { return line(truthy(newValue) ? on : off); };
    auto nameLines = [&](const char *created) {
        return createdOrChanged(tr(created).arg(newText), tr("Changed the name from %1 to %2").arg(oldText, newText));
    };
    auto slowmode = [&]() {
        const qint64 seconds = number(newValue);
        if (seconds == 0)
            return line(hasOld ? tr("Disabled slowmode") : tr("Set slowmode disabled"));
        return line(tr("Set slowmode to %1").arg(QStringLiteral("<b>%1</b>").arg(countText(seconds, QT_TRANSLATE_NOOP("AuditLogFormatter", "1 second"), QT_TRANSLATE_NOOP("AuditLogFormatter", "%1 seconds")))));
    };
    auto hideDuration = [&](const char *removed, const char *set, const char *changed) {
        const qint64 minutes = number(newValue);
        if (minutes == 0)
            return line(tr(removed));
        const QString duration = QStringLiteral("<b>%1</b>").arg(countText(minutes, QT_TRANSLATE_NOOP("AuditLogFormatter", "1 minute"), QT_TRANSLATE_NOOP("AuditLogFormatter", "%1 minutes")));
        return line(tr(hasOld ? changed : set).arg(duration));
    };

    if (key == QLatin1String("reason")) {
        if (entry.target == Target::GuildProfile)
            return std::nullopt;
        return line(tr("With reason %1").arg(newText));
    }

    if (entry.category == Category::Delete &&
        entry.action != Action::MEMBER_KICK &&
        entry.action != Action::MEMBER_BAN_ADD &&
        entry.action != Action::MEMBER_PRUNE)
        return std::nullopt;

    switch (entry.target) {
    case Target::Guild:
        if (key == QLatin1String("name"))
            return line(tr("Set the server name to %1").arg(newText));
        if (key == QLatin1String("description"))
            return clearedOrSet(tr("<b>Cleared</b> the server description"), tr("Set the server description to %1").arg(newText));
        if (key == QLatin1String("icon_hash"))
            return line(tr("Set the server icon"));
        if (key == QLatin1String("splash_hash"))
            return line(tr("Set the server invite background"));
        if (key == QLatin1String("discovery_splash_hash"))
            return line(tr("Set the Server Discovery background"));
        if (key == QLatin1String("banner_hash"))
            return clearedOrSet(tr("Removed the server banner"), tr("Set the server banner"));
        if (key == QLatin1String("owner_id"))
            return line(tr("Passed ownership to %1").arg(newText));
        if (key == QLatin1String("region"))
            return line(tr("Set the voice region to %1").arg(newText));
        if (key == QLatin1String("preferred_locale"))
            return line(tr("Set the preferred locale to %1").arg(newText));
        if (key == QLatin1String("afk_channel_id"))
            return clearedOrSet(tr("<b>Cleared</b> the inactive channel"), tr("Set the inactive channel to %1").arg(newText));
        if (key == QLatin1String("afk_timeout"))
            return line(tr("Set the inactive timeout to %1 minutes").arg(newText));
        if (key == QLatin1String("system_channel_id"))
            return clearedOrSet(tr("<b>Disabled</b> the welcome notification messages"), tr("Set the welcome notification channel to %1").arg(newText));
        if (key == QLatin1String("rules_channel_id"))
            return clearedOrSet(tr("<b>Cleared</b> the rules channel"), tr("Set the rules channel to %1").arg(newText));
        if (key == QLatin1String("public_updates_channel_id"))
            return clearedOrSet(tr("<b>Cleared</b> the Community Server updates channel"), tr("Set the Community Server updates channel to %1").arg(newText));
        if (key == QLatin1String("mfa_level")) {
            if (!hasNew)
                return std::nullopt;
            return line(number(newValue) == 1 ? tr("<b>Enabled</b> two-factor authentication requirement")
                                              : tr("<b>Disabled</b> two-factor authentication requirement"));
        }
        if (key == QLatin1String("widget_enabled"))
            return onOff(tr("<b>Enabled</b> the widget"), tr("<b>Disabled</b> the widget"));
        if (key == QLatin1String("widget_channel_id"))
            return clearedOrSet(tr("<b>Removed</b> the widget channel"), tr("Set the widget channel to %1").arg(newText));
        if (key == QLatin1String("verification_level")) {
            static const char *const levels[] = {
                QT_TRANSLATE_NOOP("AuditLogFormatter", "None"),
                QT_TRANSLATE_NOOP("AuditLogFormatter", "Low"),
                QT_TRANSLATE_NOOP("AuditLogFormatter", "Medium"),
                QT_TRANSLATE_NOOP("AuditLogFormatter", "High"),
                QT_TRANSLATE_NOOP("AuditLogFormatter", "Very High"),
            };
            const qint64 level = number(newValue);
            if (!hasNew || level < 0 || level > 4)
                return std::nullopt;
            return line(tr("Set the server verification level to %1").arg(bold(tr(levels[level]))));
        }
        if (key == QLatin1String("default_message_notifications")) {
            if (!hasNew || number(newValue) > 1)
                return std::nullopt;
            return line(tr("Set the default message notification setting to %1").arg(bold(number(newValue) == 0 ? tr("All Messages") : tr("Only Mentions"))));
        }
        if (key == QLatin1String("vanity_url_code"))
            return clearedOrSet(tr("<b>Removed</b> the Custom Invite Link"), tr("Set the Custom Invite Link to %1").arg(newText));
        if (key == QLatin1String("explicit_content_filter")) {
            switch (hasNew ? number(newValue) : -1) {
            case 0:
                return line(tr("<b>Disabled</b> the explicit content filter"));
            case 1:
                return line(tr("Set the explicit content filter to scan messages from %1").arg(bold(tr("members without a role"))));
            case 2:
                return line(tr("Set the explicit content filter to scan messages from %1").arg(bold(tr("all members"))));
            }
            return std::nullopt;
        }
        if (key == QLatin1String("premium_progress_bar_enabled"))
            return onOff(tr("Turned <b>on</b> the Boost progress bar"), tr("Turned <b>off</b> the Boost progress bar"));
        if (key == QLatin1String("triggered_rule_name"))
            return line(tr("Detected by rule %1").arg(newText));
        if (key == QLatin1String("join_notifications"))
            return line(tr("Set system channel welcome messages to %1").arg(newText));
        if (key == QLatin1String("premium_subscriptions"))
            return line(tr("Set system channel boost notifications to %1").arg(newText));
        if (key == QLatin1String("reminder_notifications"))
            return line(tr("Set system channel server setup tips to %1").arg(newText));
        if (key == QLatin1String("join_notification_replies"))
            return line(tr("Set system channel welcome stickers to %1").arg(newText));
        return std::nullopt;

    case Target::Channel:
    case Target::ChannelOverwrite:
        if (key == QLatin1String("id") ||
            key == QLatin1String("permission_overwrites") ||
            (entry.target == Target::ChannelOverwrite && key == QLatin1String("type")))
            return std::nullopt;
        if (key == QLatin1String("name"))
            return nameLines(QT_TRANSLATE_NOOP("AuditLogFormatter", "Set the name to %1"));
        if (key == QLatin1String("position"))
            return createdOrChanged(tr("In position %1").arg(newText), tr("Moved from %1 to %2").arg(oldText, newText));
        if (key == QLatin1String("topic"))
            return changedSetCleared(tr("Changed the topic to %1").arg(newText), tr("Set the topic to %1").arg(newText), tr("<b>Cleared</b> the topic"));
        if (key == QLatin1String("bitrate"))
            return createdOrChanged(tr("Set the bitrate to %1").arg(newText), tr("Changed the bitrate to %1").arg(newText));
        if (key == QLatin1String("rtc_region"))
            return changedSetCleared(tr("Changed the region override from %1 to %2").arg(oldText, newText),
                                     tr("Set the region override to %1").arg(newText), tr("Removed the region override"));
        if (key == QLatin1String("user_limit"))
            return createdOrChanged(tr("Set the user limit to %1").arg(newText), tr("Changed the user limit to %1").arg(newText));
        if (key == QLatin1String("rate_limit_per_user"))
            return slowmode();
        if (key == QLatin1String("reset") || key == QLatin1String("allow") || key == QLatin1String("deny")) {
            const QString permissions = count == 1 ? tr("a permission") : tr("permissions");
            QString subtarget = entry.raw.options.value("type").toString() == QLatin1String("1")
                                        ? userTag(snowflakeOf(entry.raw.options.value("id")))
                                        : entry.raw.options.value("role_name").toString();
            if (key == QLatin1String("reset"))
                return line(tr("<b>Reset</b> %1 for %2").arg(permissions, bold(subtarget)));
            if (key == QLatin1String("allow"))
                return line(tr("<b>Granted</b> %1 for %2").arg(permissions, bold(subtarget)));
            return line(tr("<b>Denied</b> %1 for %2").arg(permissions, bold(subtarget)));
        }
        if (key == QLatin1String("nsfw"))
            return onOff(tr("Marked the channel as age-restricted"), tr("Unmarked the channel as age-restricted"));
        if (key == QLatin1String("type"))
            return createdOrChanged(tr("Set the type to %1").arg(newText), tr("Changed the type from %1 to %2").arg(oldText, newText));
        if (key == QLatin1String("video_quality_mode"))
            return line(tr("Set the video quality mode to %1").arg(newText));
        if (key == QLatin1String("default_auto_archive_duration"))
            return hideDuration(QT_TRANSLATE_NOOP("AuditLogFormatter", "Removed default hide duration"),
                                QT_TRANSLATE_NOOP("AuditLogFormatter", "Set default hide duration to %1"),
                                QT_TRANSLATE_NOOP("AuditLogFormatter", "Changed default hide duration to %1"));
        if (key == QLatin1String("default_thread_rate_limit_per_user")) {
            const QString seconds = QStringLiteral("<b>%1</b>").arg(countText(number(newValue), QT_TRANSLATE_NOOP("AuditLogFormatter", "1 second"), QT_TRANSLATE_NOOP("AuditLogFormatter", "%1 seconds")));
            return changedSetCleared(tr("Changed default thread slowmode to %1").arg(seconds),
                                     tr("Set default thread slowmode to %1").arg(seconds), tr("Disabled default thread slowmode"));
        }
        if (key == QLatin1String("flags"))
            return line(tr("Changed channel settings"));
        if (key == QLatin1String("available_tag_add"))
            return line(tr("Added tag %1").arg(newText));
        if (key == QLatin1String("available_tag_edit"))
            return line(tr("Updated tag from %1 to %2").arg(oldText, newText));
        if (key == QLatin1String("available_tag_delete"))
            return line(tr("Deleted tag %1").arg(newText));
        return std::nullopt;

    case Target::User:
        if (key == QLatin1String("nick"))
            return changedSetCleared(tr("Changed their nickname from %1 to %2").arg(oldText, newText),
                                     tr("Set their nickname to %1").arg(newText), tr("<b>Removed</b> their nickname of %1").arg(oldText));
        if (key == QLatin1String("deaf"))
            return onOff(tr("<b>Deafened</b> them"), tr("<b>Undeafened</b> them"));
        if (key == QLatin1String("mute"))
            return onOff(tr("<b>Muted</b> them"), tr("<b>Unmuted</b> them"));
        if (key == QLatin1String("$remove"))
            return line(tr("<b>Removed</b> %1").arg(count == 1 ? tr("a role") : tr("some roles")));
        if (key == QLatin1String("$add"))
            return line(tr("<b>Added</b> %1").arg(count == 1 ? tr("a role") : tr("some roles")));
        if (key == QLatin1String("prune_delete_days"))
            return line(tr("For %1 of inactivity")
                                .arg(QStringLiteral("<b>%1</b>").arg(countText(number(newValue), QT_TRANSLATE_NOOP("AuditLogFormatter", "a day"), QT_TRANSLATE_NOOP("AuditLogFormatter", "%1 days")))));
        if (key == QLatin1String("communication_disabled_until")) {
            if (hasNew)
                return line(tr("Set timeout for user until %1").arg(newText));
            return hasOld ? line(tr("Removed timeout")) : std::nullopt;
        }
        if (key == QLatin1String("bypasses_verification"))
            return onOff(tr("manually verified them"), tr("removed their manual verification"));
        if (key == QLatin1String("triggered_rule_name"))
            return line(tr("Detected by rule %1").arg(newText));
        return std::nullopt;

    case Target::Role:
        if (key == QLatin1String("name"))
            return nameLines(QT_TRANSLATE_NOOP("AuditLogFormatter", "With the name %1"));
        if (key == QLatin1String("description"))
            return createdOrChanged(tr("With the description %1").arg(newText), tr("Changed the description from %1 to %2").arg(oldText, newText));
        if (key == QLatin1String("allow"))
            return line(tr("<b>Granted</b> %1").arg(count == 1 ? tr("permission") : tr("permissions")));
        if (key == QLatin1String("deny"))
            return line(tr("<b>Denied</b> %1").arg(count == 1 ? tr("permission") : tr("permissions")));
        if (key == QLatin1String("color")) {
            if (newValue.toString() == QLatin1String("#000000"))
                return line(tr("With no color"));
            return line(tr("Set the color to %1").arg(newText + QLatin1Char(' ') + swatch(newValue.toString())));
        }
        if (key == QLatin1String("colors")) {
            const QJsonObject colors = newValue.toObject();
            if (isNull(colors.value("secondary_color")))
                return line(tr("With no gradient"));
            QStringList parts;
            for (const char *field : { "primary_color", "secondary_color", "tertiary_color" })
                if (!isNull(colors.value(field)))
                    parts.append(colorText(colors.value(field)) + QLatin1Char(' ') + swatch(colorText(colors.value(field))));
            return line(tr("Set the gradient to [%1]").arg(parts.join(QStringLiteral(", "))));
        }
        if (key == QLatin1String("hoist"))
            return onOff(tr("Hoisted"), tr("Not hoisted"));
        if (key == QLatin1String("mentionable"))
            return onOff(tr("Mentionable"), tr("Not mentionable"));
        if (key == QLatin1String("icon_hash"))
            return line(tr("Set the icon"));
        if (key == QLatin1String("unicode_emoji"))
            return line(tr("Set the unicode emoji"));
        return std::nullopt;

    case Target::Invite:
        if (key == QLatin1String("code"))
            return line(tr("With code %1").arg(newText));
        if (key == QLatin1String("channel_id"))
            return line(tr("For channel %1").arg(newText));
        if (key == QLatin1String("max_uses"))
            return line(number(newValue) == 0 ? tr("Which has <b>unlimited</b> uses") : tr("Which expires after %1 uses").arg(newText));
        if (key == QLatin1String("max_age"))
            return line(newValue.toString() == tr("Never") ? tr("Which <b>never</b> expires") : tr("Which expires after %1").arg(newText));
        if (key == QLatin1String("temporary"))
            return onOff(tr("With temporary <b>on</b>"), tr("With temporary <b>off</b>"));
        if (key == QLatin1String("flags"))
            return line(tr("Which is a <b>guest</b> invite"));
        if (key == QLatin1String("role_ids")) {
            Line roles{ tr("With %1").arg(count == 1 ? tr("<b>a role</b>") : tr("<b>some roles</b>")), {} };
            if (entry.action == Action::INVITE_CREATE)
                roles.items = change.items;
            return roles;
        }
        return std::nullopt;

    case Target::Webhook:
        if (key == QLatin1String("channel_id"))
            return createdOrChanged(tr("With channel %1").arg(newText), tr("Changed the channel to %1").arg(newText));
        if (key == QLatin1String("name"))
            return nameLines(QT_TRANSLATE_NOOP("AuditLogFormatter", "With name %1"));
        if (key == QLatin1String("avatar_hash"))
            return line(tr("Changed the avatar"));
        return std::nullopt;

    case Target::Emoji:
        if (key == QLatin1String("name"))
            return nameLines(QT_TRANSLATE_NOOP("AuditLogFormatter", "With the name %1"));
        return std::nullopt;

    case Target::Sticker:
        if (key == QLatin1String("name"))
            return nameLines(QT_TRANSLATE_NOOP("AuditLogFormatter", "With the name %1"));
        if (key == QLatin1String("tags"))
            return createdOrChanged(tr("With the tags %1").arg(newText), tr("Changed the tags from %1 to %2").arg(oldText, newText));
        if (key == QLatin1String("description"))
            return createdOrChanged(tr("With the description %1").arg(newText), tr("Changed the description from %1 to %2").arg(oldText, newText));
        return std::nullopt;

    case Target::Integration:
        if (key == QLatin1String("enable_emoticons"))
            return onOff(tr("Enabled emoticons"), tr("Disabled custom emoticons"));
        if (key == QLatin1String("expire_behavior")) {
            if (!hasNew || number(newValue) > 1)
                return std::nullopt;
            return line(tr("Set the expired sub behavior to %1").arg(bold(number(newValue) == 0 ? tr("Remove Role") : tr("Kick"))));
        }
        if (key == QLatin1String("expire_grace_period"))
            return line(tr("Set the expire grace period to %1")
                                .arg(QStringLiteral("<b>%1</b>").arg(countText(number(newValue), QT_TRANSLATE_NOOP("AuditLogFormatter", "a day"), QT_TRANSLATE_NOOP("AuditLogFormatter", "%1 days")))));
        return std::nullopt;

    case Target::StageInstance:
    case Target::ScheduledEvent:
        if (key == QLatin1String("privacy_level")) {
            switch (hasNew ? number(newValue) : 0) {
            case 1:
                return line(tr("Set the privacy level to %1").arg(bold(tr("Public"))));
            case 2:
                return line(tr("Set the privacy level to %1").arg(bold(tr("Closed"))));
            }
            return std::nullopt;
        }
        if (entry.target == Target::StageInstance) {
            if (key == QLatin1String("topic"))
                return createdOrChanged(tr("Set the topic to %1").arg(newText), tr("Changed the topic to %1").arg(newText));
            return std::nullopt;
        }
        if (key == QLatin1String("name"))
            return line(tr("With the name %1").arg(newText));
        if (key == QLatin1String("description"))
            return line(tr("Set the description to %1").arg(newText));
        if (key == QLatin1String("status")) {
            static const char *const statuses[] = {
                QT_TRANSLATE_NOOP("AuditLogFormatter", "Scheduled"),
                QT_TRANSLATE_NOOP("AuditLogFormatter", "Active"),
                QT_TRANSLATE_NOOP("AuditLogFormatter", "Completed"),
                QT_TRANSLATE_NOOP("AuditLogFormatter", "Canceled"),
            };
            const qint64 status = number(newValue);
            if (!hasNew || status < 1 || status > 4)
                return std::nullopt;
            return line(tr("Set the status to %1").arg(bold(tr(statuses[status - 1]))));
        }
        if (key == QLatin1String("entity_type")) {
            switch (hasNew ? number(newValue) : -1) {
            case 0:
                return line(tr("Linked to <b>Nothing</b>"));
            case 1:
                return line(tr("Linked with a <b>Stage</b>"));
            case 2:
                return line(tr("Linked with a <b>Voice Channel</b>"));
            case 3:
                return line(tr("Linked <b>externally</b>"));
            }
            return std::nullopt;
        }
        if (key == QLatin1String("channel_id"))
            return clearedOrSet(tr("Removed the channel"), tr("Set the channel to %1").arg(newText));
        if (key == QLatin1String("location"))
            return clearedOrSet(tr("Removed the location"), tr("Set the location to %1").arg(newText));
        if (key == QLatin1String("image_hash"))
            return clearedOrSet(tr("Removed the cover image"), tr("Set the cover image"));
        return std::nullopt;

    case Target::ScheduledEventException:
        if (key == QLatin1String("scheduled_start_time"))
            return clearedOrSet(tr("Reset the start time"), tr("Set the start time to %1").arg(newText));
        if (key == QLatin1String("scheduled_end_time"))
            return clearedOrSet(tr("Reset the end time"), tr("Set the end time to %1").arg(newText));
        if (key == QLatin1String("is_canceled")) {
            if (!truthy(oldValue) && truthy(newValue))
                return line(tr("Canceled this event"));
            if (truthy(oldValue) && hasNew && !truthy(newValue))
                return line(tr("Restored this event"));
        }
        return std::nullopt;

    case Target::Thread:
        if (key == QLatin1String("id") || key == QLatin1String("type"))
            return std::nullopt;
        if (key == QLatin1String("name"))
            return nameLines(QT_TRANSLATE_NOOP("AuditLogFormatter", "Set the name to %1"));
        if (key == QLatin1String("archived"))
            return onOff(tr("Closed the thread"), tr("Reopened the thread"));
        if (key == QLatin1String("locked"))
            return onOff(tr("Locked the thread, restricting it to only be opened by moderators"),
                         tr("Unlocked the thread, allowing it to be opened by non-moderators"));
        if (key == QLatin1String("invitable"))
            return onOff(tr("Allowed non-moderators to add members to the thread"), tr("Disallowed non-moderators from adding members to the thread"));
        if (key == QLatin1String("auto_archive_duration"))
            return hideDuration(QT_TRANSLATE_NOOP("AuditLogFormatter", "Removed auto hide duration"),
                                QT_TRANSLATE_NOOP("AuditLogFormatter", "Set auto hide duration to %1"),
                                QT_TRANSLATE_NOOP("AuditLogFormatter", "Changed auto hide duration to %1"));
        if (key == QLatin1String("rate_limit_per_user"))
            return slowmode();
        if (key == QLatin1String("flags")) {
            Line flags{ tr("Updated thread properties"), {} };
            if (entry.action == Action::THREAD_UPDATE)
                flags.items = change.items;
            return flags;
        }
        if (key == QLatin1String("available_tag_add"))
            return line(tr("Added tag %1").arg(newText));
        if (key == QLatin1String("available_tag_delete"))
            return line(tr("Deleted tag %1").arg(newText));
        return std::nullopt;

    case Target::ApplicationCommand: {
        const QString subtarget = bold(change.subtarget);
        if (!hasNew)
            return line(tr("<b>Removed override</b> for %1").arg(subtarget));
        return line(newValue.toObject().value("permission").toBool() ? tr("<b>Granted</b> permission for %1").arg(subtarget)
                                                                     : tr("<b>Denied</b> permission for %1").arg(subtarget));
    }

    case Target::AutoModerationRule:
        if (key == QLatin1String("name"))
            return line(tr("Set the name to %1").arg(newText));
        if (key == QLatin1String("trigger_type"))
            return line(tr("Set the trigger type to %1").arg(newText));
        if (key == QLatin1String("event_type"))
            return line(tr("Set the event type to %1").arg(newText));
        if (key == QLatin1String("actions"))
            return line(tr("Set actions to %1").arg(newText));
        if (key == QLatin1String("enabled"))
            return line((hasNew ? newValue : oldValue).toBool() ? tr("Enabled rule") : tr("Disabled rule"));
        if (key == QLatin1String("exempt_roles"))
            return line(tr("Set exempt roles to %1").arg(newText));
        if (key == QLatin1String("exempt_channels"))
            return line(tr("Set exempt channels to %1").arg(newText));
        if (key == QLatin1String("trigger_metadata"))
            return line(tr("Set trigger metadata %1").arg(newText));
        if (key == QLatin1String("$add_keyword_filter"))
            return line(tr("added keywords %1").arg(newText));
        if (key == QLatin1String("$remove_keyword_filter"))
            return line(tr("removed keywords %1").arg(newText));
        if (key == QLatin1String("$add_regex_patterns"))
            return line(tr("added regex patterns %1").arg(newText));
        if (key == QLatin1String("$remove_regex_patterns"))
            return line(tr("removed regex patterns %1").arg(newText));
        if (key == QLatin1String("$add_allow_list"))
            return line(tr("added allow list keywords %1").arg(newText));
        if (key == QLatin1String("$remove_allow_list"))
            return line(tr("removed allow list keywords %1").arg(newText));
        return std::nullopt;

    case Target::Soundboard:
        if (key == QLatin1String("name"))
            return nameLines(QT_TRANSLATE_NOOP("AuditLogFormatter", "With the name %1"));
        if (key == QLatin1String("volume"))
            return createdOrChanged(tr("With the volume set to %1").arg(newText), tr("Changed the volume from %1 to %2").arg(oldText, newText));
        if (key == QLatin1String("emoji_name") || key == QLatin1String("emoji_id"))
            return changedSetCleared(tr("Changed the emoji from %1 to %2").arg(oldText, newText), tr("With the emoji %1").arg(newText),
                                     tr("Removed the emoji %1").arg(oldText));
        return std::nullopt;

    case Target::VoiceChannelStatus:
        if (key == QLatin1String("status"))
            return line(tr("Set to %1").arg(newText));
        return std::nullopt;

    case Target::MemberVerification:
        if (key == QLatin1String("verification_enabled"))
            return line(newValue.toBool() ? tr("Enabled join settings") : tr("Disabled join settings"));
        if (key == QLatin1String("manual_approval_enabled"))
            return line(newValue.toBool() ? tr("Manual verification enabled") : tr("Manual verification disabled"));
        return std::nullopt;

    case Target::GuildProfile:
        if (key == QLatin1String("description"))
            return line(tr("Updated server description"));
        if (key == QLatin1String("brand_color_primary"))
            return line(tr("Updated profile banner color"));
        if (key == QLatin1String("custom_banner_hash"))
            return line(tr("Updated profile banner"));
        if (key == QLatin1String("traits"))
            return line(tr("Updated profile traits"));
        if (key == QLatin1String("game_application_ids"))
            return line(tr("Updated profile games played"));
        if (key == QLatin1String("visibility"))
            return line(tr("Updated profile visibility"));
        if (key == QLatin1String("server_tag"))
            return clearedOrSet(tr("<b>Disabled</b> the server tag"), tr("Updated server tag to %1").arg(newText));
        return std::nullopt;

    case Target::OnboardingPrompt:
        if (key == QLatin1String("title"))
            return createdOrChanged(tr("Set the title to %1").arg(newText), tr("Changed the title from %1 to %2").arg(oldText, newText));
        if (key == QLatin1String("description"))
            return createdOrChanged(tr("Set the description to %1").arg(newText), tr("Changed the description from %1 to %2").arg(oldText, newText));
        if (key == QLatin1String("options"))
            return line(tr("Changed answers for customization question"));
        if (key == QLatin1String("single_select"))
            return onOff(tr("Set the customization question to single select"), tr("Set the customization question to multi select"));
        if (key == QLatin1String("required"))
            return onOff(tr("Set the customization question to required"), tr("Set the customization question to optional"));
        return std::nullopt;

    case Target::Onboarding:
        if (key == QLatin1String("default_channel_ids"))
            return line(countText(count, QT_TRANSLATE_NOOP("AuditLogFormatter", "Set 1 default channel"),
                                  QT_TRANSLATE_NOOP("AuditLogFormatter", "Set %1 default channels")));
        if (key == QLatin1String("enable_default_channels"))
            return onOff(tr("Set default channels to enabled"), tr("Set default channels to disabled"));
        if (key == QLatin1String("enable_onboarding_prompts"))
            return onOff(tr("Set customization questions to enabled"), tr("Set customization questions to disabled"));
        if (key == QLatin1String("enabled"))
            return onOff(tr("Enabled Onboarding"), tr("Disabled Onboarding"));
        return std::nullopt;

    case Target::HomeSettings:
        if (key == QLatin1String("welcome_message"))
            return line(tr("Changed the welcome message"));
        if (key == QLatin1String("new_member_actions"))
            return line(tr("Changed the new member To Dos"));
        if (key == QLatin1String("resource_channels"))
            return line(tr("Changed resource channels"));
        return std::nullopt;

    case Target::GuildHome:
    case Target::Unknown:
        break;
    }
    return std::nullopt;
}

AuditLogFormatter::Row AuditLogFormatter::format(const QList<Entry> &merged)
{
    const Entry &first = merged.first();
    const QJsonObject &options = first.raw.options;

    QString count = options.value("count").toVariant().toString();
    const QString removed = options.value("members_removed").toVariant().toString();
    if (!removed.isEmpty() && removed != QLatin1String("0"))
        count = removed;

    const Core::Snowflake channelId = snowflakeOf(options.value("channel_id"));
    const QString channel = channelId.isValid() ? channelName(channelId) : QString();

    QString subtarget;
    const Core::Snowflake exceptionId = snowflakeOf(options.value("event_exception_id"));
    if (first.target == Target::ScheduledEventException && exceptionId.isValid())
        subtarget = QLocale::system().toString(exceptionId.toDateTime().toLocalTime().date(), QStringLiteral("MMMM d, yyyy"));

    Row row;
    row.action = first.action;
    row.userId = first.userId;
    row.category = first.category;
    row.start = first.time;
    row.end = merged.last().time;
    row.titleHtml = titleFor(first, targetName(first), count, channel, subtarget);
    for (const Entry &entry : merged)
        for (const Change &change : transform(entry))
            if (const auto line = describe(entry, change))
                row.lines.append(*line);
    return row;
}

QString AuditLogFormatter::timeText(const QDateTime &start, const QDateTime &end)
{
    const QString startText = calendar(start);
    const QString endText = calendar(end);
    return startText == endText ? startText : startText + QStringLiteral("—") + endText;
}

QList<QPair<Discord::AuditLogAction, QString>> AuditLogFormatter::actionFilters()
{
    return {
        { Action::GUILD_UPDATE, tr("Update Server") },
        { Action::CHANNEL_CREATE, tr("Create Channel") },
        { Action::CHANNEL_UPDATE, tr("Update Channel") },
        { Action::CHANNEL_DELETE, tr("Delete Channel") },
        { Action::CHANNEL_OVERWRITE_CREATE, tr("Create Channel Permissions") },
        { Action::CHANNEL_OVERWRITE_UPDATE, tr("Update Channel Permissions") },
        { Action::CHANNEL_OVERWRITE_DELETE, tr("Delete Channel Permissions") },
        { Action::MEMBER_KICK, tr("Kick Member") },
        { Action::MEMBER_PRUNE, tr("Prune Members") },
        { Action::MEMBER_BAN_ADD, tr("Ban Member") },
        { Action::MEMBER_BAN_REMOVE, tr("Unban Member") },
        { Action::MEMBER_UPDATE, tr("Update Member") },
        { Action::MEMBER_ROLE_UPDATE, tr("Update Member Roles") },
        { Action::MEMBER_MOVE, tr("Move Member") },
        { Action::MEMBER_DISCONNECT, tr("Disconnect Member") },
        { Action::BOT_ADD, tr("Add Bot") },
        { Action::THREAD_CREATE, tr("Create Thread") },
        { Action::THREAD_UPDATE, tr("Update Thread") },
        { Action::THREAD_DELETE, tr("Delete Thread") },
        { Action::ROLE_CREATE, tr("Create Role") },
        { Action::ROLE_UPDATE, tr("Update Role") },
        { Action::ROLE_DELETE, tr("Delete Role") },
        { Action::ONBOARDING_PROMPT_CREATE, tr("Create Customization Question") },
        { Action::ONBOARDING_PROMPT_UPDATE, tr("Update Customization Question") },
        { Action::ONBOARDING_PROMPT_DELETE, tr("Delete Customization Question") },
        { Action::ONBOARDING_CREATE, tr("Create Onboarding") },
        { Action::ONBOARDING_UPDATE, tr("Update Onboarding") },
        { Action::HOME_SETTINGS_CREATE, tr("Create Server Guide") },
        { Action::HOME_SETTINGS_UPDATE, tr("Update Server Guide") },
        { Action::INVITE_CREATE, tr("Create Invite") },
        { Action::INVITE_UPDATE, tr("Update Invite") },
        { Action::INVITE_DELETE, tr("Delete Invite") },
        { Action::WEBHOOK_CREATE, tr("Create Webhook") },
        { Action::WEBHOOK_UPDATE, tr("Update Webhook") },
        { Action::WEBHOOK_DELETE, tr("Delete Webhook") },
        { Action::EMOJI_CREATE, tr("Create Emoji") },
        { Action::EMOJI_UPDATE, tr("Update Emoji") },
        { Action::EMOJI_DELETE, tr("Delete Emoji") },
        { Action::MESSAGE_DELETE, tr("Delete Messages") },
        { Action::MESSAGE_BULK_DELETE, tr("Bulk Delete Messages") },
        { Action::MESSAGE_PIN, tr("Pin Message") },
        { Action::MESSAGE_UNPIN, tr("Unpin Message") },
        { Action::INTEGRATION_CREATE, tr("Create Integration") },
        { Action::INTEGRATION_UPDATE, tr("Update Integration") },
        { Action::INTEGRATION_DELETE, tr("Delete Integration") },
        { Action::STICKER_CREATE, tr("Create Sticker") },
        { Action::STICKER_UPDATE, tr("Update Sticker") },
        { Action::STICKER_DELETE, tr("Delete Sticker") },
        { Action::STAGE_INSTANCE_CREATE, tr("Start Stage") },
        { Action::STAGE_INSTANCE_UPDATE, tr("Update Stage") },
        { Action::STAGE_INSTANCE_DELETE, tr("End Stage") },
        { Action::GUILD_SCHEDULED_EVENT_CREATE, tr("Create Event") },
        { Action::GUILD_SCHEDULED_EVENT_UPDATE, tr("Update Event") },
        { Action::GUILD_SCHEDULED_EVENT_DELETE, tr("Cancel Event") },
        { Action::APPLICATION_COMMAND_PERMISSION_UPDATE, tr("Update Command Permissions") },
        { Action::AUTO_MODERATION_BLOCK_MESSAGE, tr("AutoMod Block Message") },
        { Action::AUTO_MODERATION_RULE_CREATE, tr("Create AutoMod Rule") },
        { Action::AUTO_MODERATION_RULE_UPDATE, tr("Update AutoMod Rule") },
        { Action::AUTO_MODERATION_RULE_DELETE, tr("Delete AutoMod Rule") },
        { Action::GUILD_HOME_FEATURE_ITEM, tr("Feature Item on Home") },
        { Action::GUILD_HOME_REMOVE_ITEM, tr("Remove Item from Home") },
        { Action::SOUNDBOARD_SOUND_CREATE, tr("Create Soundboard Sound") },
        { Action::SOUNDBOARD_SOUND_UPDATE, tr("Update Soundboard Sound") },
        { Action::SOUNDBOARD_SOUND_DELETE, tr("Delete Soundboard Sound") },
        { Action::VOICE_CHANNEL_STATUS_CREATE, tr("Create Voice Channel Status") },
        { Action::VOICE_CHANNEL_STATUS_DELETE, tr("Delete Voice Channel Status") },
    };
}

} // namespace UI
} // namespace Acheron
