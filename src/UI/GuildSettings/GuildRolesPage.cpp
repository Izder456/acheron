#include "GuildRolesPage.hpp"

#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QColorDialog>
#include <QDialogButtonBox>
#include <QFile>
#include <QGridLayout>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLayout>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMimeDatabase>
#include <QPainter>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QSet>
#include <QSignalBlocker>
#include <QStackedWidget>
#include <QStyle>
#include <QStyledItemDelegate>
#include <QTabWidget>
#include <QTimer>
#include <QToolButton>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <algorithm>
#include <iterator>
#include <vector>

#include "Core/ClientInstance.hpp"
#include "Core/ImageManager.hpp"
#include "Core/PermissionComputer.hpp"
#include "Core/Theme/Icons.hpp"
#include "Core/Theme/Manager.hpp"
#include "Discord/CdnUrls.hpp"
#include "UI/Dialogs/BasePopup.hpp"
#include "UI/Dialogs/ConfirmPopup.hpp"

#include "ImageUpload.hpp"
#include "UnsavedChangesBar.hpp"

namespace Acheron {
namespace UI {

using Discord::Permission;

namespace {

constexpr int RoleIdRole = Qt::UserRole;
constexpr int MemberCountRole = Qt::UserRole + 1;
constexpr int LockedRole = Qt::UserRole + 2;
constexpr int UserIdRole = Qt::UserRole;

constexpr int RoleNameMaxLength = 100;
constexpr int DefaultRoleColor = 0x99aab5;
constexpr int PresetColors[] = {
    0x1abc9c,
    0x2ecc71,
    0x3498db,
    0x9b59b6,
    0xe91e63,
    0xf1c40f,
    0xe67e22,
    0xe74c3c,
    0x95a5a6,
    0x607d8b,
    0x11806a,
    0x1f8b4c,
    0x206694,
    0x71368a,
    0xad1457,
    0xc27c0e,
    0xa84300,
    0x992d22,
    0x979c9f,
    0x546e7a,
};
constexpr int PresetsPerRow = 10;
constexpr qint64 RoleIconMaxBytes = 256000;
constexpr qint64 MemberCountsMaxAgeSecs = 120;
constexpr qint64 RoleMembersMaxAgeSecs = 10;
constexpr int MemberSearchLimit = 200;
constexpr int AddMembersMax = 30;
constexpr int AddMembersShown = 100;
constexpr int MemberSearchDebounceMs = 300;
constexpr int AddMembersSearchDebounceMs = 500;
constexpr QSize SwatchSize(26, 26);
constexpr QSize IconPreviewSize(48, 48);
constexpr QSize MemberAvatarSize(24, 24);

enum class PermissionGate {
    None,
    Community,
    CreatorMonetizable,
};

struct PermissionInfo
{
    const char *section;
    Permission permission;
    const char *title;
    const char *description;
    PermissionGate gate = PermissionGate::None;
    const char *shortTitle = nullptr;
};

const PermissionInfo PermissionCatalog[] = {
    { QT_TRANSLATE_NOOP("GuildRolesPage", "General Server"), Permission::VIEW_CHANNEL,
      QT_TRANSLATE_NOOP("GuildRolesPage", "View Channels"),
      QT_TRANSLATE_NOOP("GuildRolesPage", "Allows members to view channels by default (excluding private channels).") },
    { QT_TRANSLATE_NOOP("GuildRolesPage", "General Server"), Permission::MANAGE_CHANNELS,
      QT_TRANSLATE_NOOP("GuildRolesPage", "Manage Channels"),
      QT_TRANSLATE_NOOP("GuildRolesPage", "Allows members to create, edit, or delete channels.") },
    { QT_TRANSLATE_NOOP("GuildRolesPage", "General Server"), Permission::MANAGE_ROLES, QT_TRANSLATE_NOOP("GuildRolesPage", "Manage Roles"),
      QT_TRANSLATE_NOOP("GuildRolesPage", "Allows members to create new roles and edit or delete roles lower than their "
                                          "highest role. Also allows members to change permissions of individual channels that they have access to.") },
    { QT_TRANSLATE_NOOP("GuildRolesPage", "General Server"), Permission::CREATE_EXPRESSIONS,
      QT_TRANSLATE_NOOP("GuildRolesPage", "Create Expressions"),
      QT_TRANSLATE_NOOP("GuildRolesPage", "Allows members to add custom emoji, stickers, and sounds in this server.") },
    { QT_TRANSLATE_NOOP("GuildRolesPage", "General Server"), Permission::MANAGE_EXPRESSIONS, QT_TRANSLATE_NOOP("GuildRolesPage", "Manage Expressions"),
      QT_TRANSLATE_NOOP("GuildRolesPage", "Allows members to edit or remove custom emoji, stickers, and sounds in this server.") },
    { QT_TRANSLATE_NOOP("GuildRolesPage", "General Server"), Permission::VIEW_AUDIT_LOG,
      QT_TRANSLATE_NOOP("GuildRolesPage", "View Audit Log"),
      QT_TRANSLATE_NOOP("GuildRolesPage", "Allows members to view a record of who made which changes in this server.") },
    { QT_TRANSLATE_NOOP("GuildRolesPage", "General Server"), Permission::VIEW_GUILD_INSIGHTS, QT_TRANSLATE_NOOP("GuildRolesPage", "View Server Insights"),
      QT_TRANSLATE_NOOP("GuildRolesPage", "Allows members to view Server Insights, which shows data on community growth, "
                                          "engagement, and more. This will allow them to see certain data about channel "
                                          "activity, even for channels they cannot access."),
      PermissionGate::Community },
    { QT_TRANSLATE_NOOP("GuildRolesPage", "General Server"), Permission::VIEW_CREATOR_MONETIZATION_ANALYTICS,
      QT_TRANSLATE_NOOP("GuildRolesPage", "View Server Subscription Insights"),
      QT_TRANSLATE_NOOP("GuildRolesPage", "Allows members to view Server Subscription Insights, which shows data on revenue, subscribers, and free trials."),
      PermissionGate::CreatorMonetizable, QT_TRANSLATE_NOOP("GuildRolesPage", "Subscription Insights") },
    { QT_TRANSLATE_NOOP("GuildRolesPage", "General Server"), Permission::MANAGE_WEBHOOKS, QT_TRANSLATE_NOOP("GuildRolesPage", "Manage Webhooks"),
      QT_TRANSLATE_NOOP("GuildRolesPage", "Allows members to create, edit, or delete webhooks, which can post messages from "
                                          "other apps or sites into this server.") },
    { QT_TRANSLATE_NOOP("GuildRolesPage", "General Server"), Permission::MANAGE_GUILD, QT_TRANSLATE_NOOP("GuildRolesPage", "Manage Server"),
      QT_TRANSLATE_NOOP("GuildRolesPage", "Allow members to change this server's name, switch regions, view all invites, "
                                          "add apps to this server and create and update AutoMod rules.") },

    { QT_TRANSLATE_NOOP("GuildRolesPage", "Membership"), Permission::CREATE_INSTANT_INVITE,
      QT_TRANSLATE_NOOP("GuildRolesPage", "Create Invite"),
      QT_TRANSLATE_NOOP("GuildRolesPage", "Allows members to invite new people to this server.") },
    { QT_TRANSLATE_NOOP("GuildRolesPage", "Membership"), Permission::CHANGE_NICKNAME, QT_TRANSLATE_NOOP("GuildRolesPage", "Change Nickname"),
      QT_TRANSLATE_NOOP("GuildRolesPage", "Allows members to change their own nickname, a custom name for just this server.") },
    { QT_TRANSLATE_NOOP("GuildRolesPage", "Membership"), Permission::MANAGE_NICKNAMES,
      QT_TRANSLATE_NOOP("GuildRolesPage", "Manage Nicknames"),
      QT_TRANSLATE_NOOP("GuildRolesPage", "Allows members to change the nicknames of other members.") },
    { QT_TRANSLATE_NOOP("GuildRolesPage", "Membership"), Permission::KICK_MEMBERS, QT_TRANSLATE_NOOP("GuildRolesPage", "Kick, Approve, and Reject Members"),
      QT_TRANSLATE_NOOP("GuildRolesPage", "Kick will remove other members from this server. Kicked members will be able to "
                                          "rejoin if they have another invite. If the server enables Member Requirements, "
                                          "this permission enables the ability to approve or reject members who request to join."),
      PermissionGate::None, QT_TRANSLATE_NOOP("GuildRolesPage", "Kick Members") },
    { QT_TRANSLATE_NOOP("GuildRolesPage", "Membership"), Permission::BAN_MEMBERS, QT_TRANSLATE_NOOP("GuildRolesPage", "Ban Members"),
      QT_TRANSLATE_NOOP("GuildRolesPage", "Allows members to permanently ban and delete the message history of other members from this server.") },
    { QT_TRANSLATE_NOOP("GuildRolesPage", "Membership"), Permission::MODERATE_MEMBERS, QT_TRANSLATE_NOOP("GuildRolesPage", "Timeout Members"),
      QT_TRANSLATE_NOOP("GuildRolesPage", "When you put a user in timeout they will not be able to send messages in chat, "
                                          "reply within threads, react to messages, or speak in voice or Stage channels.") },

    { QT_TRANSLATE_NOOP("GuildRolesPage", "Text Channels"), Permission::SEND_MESSAGES, QT_TRANSLATE_NOOP("GuildRolesPage", "Send Messages and Create Posts"),
      QT_TRANSLATE_NOOP("GuildRolesPage", "Allow members to send messages in text channels and create posts in forum channels."),
      PermissionGate::None, QT_TRANSLATE_NOOP("GuildRolesPage", "Send Messages") },
    { QT_TRANSLATE_NOOP("GuildRolesPage", "Text Channels"), Permission::SEND_MESSAGES_IN_THREADS,
      QT_TRANSLATE_NOOP("GuildRolesPage", "Send Messages in Threads and Posts"),
      QT_TRANSLATE_NOOP("GuildRolesPage", "Allow members to send messages in threads and in posts on forum channels."),
      PermissionGate::None, QT_TRANSLATE_NOOP("GuildRolesPage", "Send in Threads") },
    { QT_TRANSLATE_NOOP("GuildRolesPage", "Text Channels"), Permission::CREATE_PUBLIC_THREADS,
      QT_TRANSLATE_NOOP("GuildRolesPage", "Create Public Threads"),
      QT_TRANSLATE_NOOP("GuildRolesPage", "Allow members to create threads that everyone in a channel can view.") },
    { QT_TRANSLATE_NOOP("GuildRolesPage", "Text Channels"), Permission::CREATE_PRIVATE_THREADS,
      QT_TRANSLATE_NOOP("GuildRolesPage", "Create Private Threads"),
      QT_TRANSLATE_NOOP("GuildRolesPage", "Allow members to create invite-only threads.") },
    { QT_TRANSLATE_NOOP("GuildRolesPage", "Text Channels"), Permission::EMBED_LINKS,
      QT_TRANSLATE_NOOP("GuildRolesPage", "Embed Links"),
      QT_TRANSLATE_NOOP("GuildRolesPage", "Allows links that members share to show embedded content in text channels.") },
    { QT_TRANSLATE_NOOP("GuildRolesPage", "Text Channels"), Permission::ATTACH_FILES,
      QT_TRANSLATE_NOOP("GuildRolesPage", "Attach Files"),
      QT_TRANSLATE_NOOP("GuildRolesPage", "Allows members to upload files or media in text channels.") },
    { QT_TRANSLATE_NOOP("GuildRolesPage", "Text Channels"), Permission::ADD_REACTIONS, QT_TRANSLATE_NOOP("GuildRolesPage", "Add Reactions"),
      QT_TRANSLATE_NOOP("GuildRolesPage", "Allows members to add new emoji reactions to a message. If this permission is "
                                          "disabled, members can still react using any existing reactions on a message.") },
    { QT_TRANSLATE_NOOP("GuildRolesPage", "Text Channels"), Permission::USE_EXTERNAL_EMOJIS, QT_TRANSLATE_NOOP("GuildRolesPage", "Use External Emoji"),
      QT_TRANSLATE_NOOP("GuildRolesPage", "Allows members to use emoji from other servers, if they're a Discord Nitro member.") },
    { QT_TRANSLATE_NOOP("GuildRolesPage", "Text Channels"), Permission::USE_EXTERNAL_STICKERS, QT_TRANSLATE_NOOP("GuildRolesPage", "Use External Stickers"),
      QT_TRANSLATE_NOOP("GuildRolesPage", "Allows members to use stickers from other servers, if they're a Discord Nitro member.") },
    { QT_TRANSLATE_NOOP("GuildRolesPage", "Text Channels"), Permission::MENTION_EVERYONE,
      QT_TRANSLATE_NOOP("GuildRolesPage", "Mention @everyone, @here, and All Roles"),
      QT_TRANSLATE_NOOP("GuildRolesPage", "Allows members to use @everyone (everyone in the server) or @here (only online "
                                          "members in that channel). They can also @mention all roles, even if the role's "
                                          "\"Allow anyone to mention this role\" permission is disabled."),
      PermissionGate::None, QT_TRANSLATE_NOOP("GuildRolesPage", "Mention @everyone") },
    { QT_TRANSLATE_NOOP("GuildRolesPage", "Text Channels"), Permission::MANAGE_MESSAGES,
      QT_TRANSLATE_NOOP("GuildRolesPage", "Manage Messages"),
      QT_TRANSLATE_NOOP("GuildRolesPage", "Allows members to delete or remove embeds from messages by other members.") },
    { QT_TRANSLATE_NOOP("GuildRolesPage", "Text Channels"), Permission::PIN_MESSAGES,
      QT_TRANSLATE_NOOP("GuildRolesPage", "Pin Messages"),
      QT_TRANSLATE_NOOP("GuildRolesPage", "Allows members to pin or unpin any message.") },
    { QT_TRANSLATE_NOOP("GuildRolesPage", "Text Channels"), Permission::BYPASS_SLOWMODE,
      QT_TRANSLATE_NOOP("GuildRolesPage", "Bypass Slowmode"),
      QT_TRANSLATE_NOOP("GuildRolesPage", "Allows members to send messages without being affected by slowmode.") },
    { QT_TRANSLATE_NOOP("GuildRolesPage", "Text Channels"), Permission::MANAGE_THREADS, QT_TRANSLATE_NOOP("GuildRolesPage", "Manage Threads and Posts"),
      QT_TRANSLATE_NOOP("GuildRolesPage", "Allows members to rename, delete, close, and turn on slow mode for threads and "
                                          "posts. They can also view private threads."),
      PermissionGate::None, QT_TRANSLATE_NOOP("GuildRolesPage", "Manage Threads") },
    { QT_TRANSLATE_NOOP("GuildRolesPage", "Text Channels"), Permission::READ_MESSAGE_HISTORY, QT_TRANSLATE_NOOP("GuildRolesPage", "Read Message History"),
      QT_TRANSLATE_NOOP("GuildRolesPage", "Allows members to read previous messages sent in channels. If this permission is "
                                          "disabled, members only see messages sent when they are online. This does not "
                                          "fully apply to threads and forum posts.") },
    { QT_TRANSLATE_NOOP("GuildRolesPage", "Text Channels"), Permission::SEND_TTS_MESSAGES, QT_TRANSLATE_NOOP("GuildRolesPage", "Send Text-to-Speech Messages"),
      QT_TRANSLATE_NOOP("GuildRolesPage", "Allows members to send text-to-speech messages by starting a message with /tts. "
                                          "These messages can be heard by anyone focused on the channel."),
      PermissionGate::None, QT_TRANSLATE_NOOP("GuildRolesPage", "Send TTS Messages") },
    { QT_TRANSLATE_NOOP("GuildRolesPage", "Text Channels"), Permission::SEND_VOICE_MESSAGES,
      QT_TRANSLATE_NOOP("GuildRolesPage", "Send Voice Messages"),
      QT_TRANSLATE_NOOP("GuildRolesPage", "Allows members to send voice messages.") },
    { QT_TRANSLATE_NOOP("GuildRolesPage", "Text Channels"), Permission::SEND_POLLS,
      QT_TRANSLATE_NOOP("GuildRolesPage", "Create Polls"), QT_TRANSLATE_NOOP("GuildRolesPage", "Allows members to create polls.") },

    { QT_TRANSLATE_NOOP("GuildRolesPage", "Voice Channels"), Permission::CONNECT,
      QT_TRANSLATE_NOOP("GuildRolesPage", "Connect"),
      QT_TRANSLATE_NOOP("GuildRolesPage", "Allows members to join voice channels and hear others.") },
    { QT_TRANSLATE_NOOP("GuildRolesPage", "Voice Channels"), Permission::SPEAK, QT_TRANSLATE_NOOP("GuildRolesPage", "Speak"),
      QT_TRANSLATE_NOOP("GuildRolesPage", "Allows members to talk in voice channels. If this permission is disabled, members "
                                          "are default muted until somebody with the \"Mute Members\" permission un-mutes them.") },
    { QT_TRANSLATE_NOOP("GuildRolesPage", "Voice Channels"), Permission::STREAM, QT_TRANSLATE_NOOP("GuildRolesPage", "Video"),
      QT_TRANSLATE_NOOP("GuildRolesPage", "Allows members to share their video, screen share, or stream a game in this server.") },
    { QT_TRANSLATE_NOOP("GuildRolesPage", "Voice Channels"), Permission::USE_SOUNDBOARD,
      QT_TRANSLATE_NOOP("GuildRolesPage", "Use Soundboard"),
      QT_TRANSLATE_NOOP("GuildRolesPage", "Allows members to send sounds from server soundboard.") },
    { QT_TRANSLATE_NOOP("GuildRolesPage", "Voice Channels"), Permission::USE_EXTERNAL_SOUNDS, QT_TRANSLATE_NOOP("GuildRolesPage", "Use External Sounds"),
      QT_TRANSLATE_NOOP("GuildRolesPage", "Allows members to use sounds from other servers, if they're a Discord Nitro member.") },
    { QT_TRANSLATE_NOOP("GuildRolesPage", "Voice Channels"), Permission::USE_VAD, QT_TRANSLATE_NOOP("GuildRolesPage", "Use Voice Activity"),
      QT_TRANSLATE_NOOP("GuildRolesPage", "Allows members to speak in voice channels by simply talking. If this permission "
                                          "is disabled, members are required to use Push-to-talk. Good for controlling background noise or noisy members.") },
    { QT_TRANSLATE_NOOP("GuildRolesPage", "Voice Channels"), Permission::PRIORITY_SPEAKER, QT_TRANSLATE_NOOP("GuildRolesPage", "Priority Speaker"),
      QT_TRANSLATE_NOOP("GuildRolesPage", "Allows members to be more easily heard in voice channels. When activated, the "
                                          "volume of others without this permission will be automatically lowered. Priority "
                                          "Speaker is activated by using the Push to Talk (Priority) keybind.") },
    { QT_TRANSLATE_NOOP("GuildRolesPage", "Voice Channels"), Permission::MUTE_MEMBERS,
      QT_TRANSLATE_NOOP("GuildRolesPage", "Mute Members"),
      QT_TRANSLATE_NOOP("GuildRolesPage", "Allows members to mute other members in voice channels for everyone.") },
    { QT_TRANSLATE_NOOP("GuildRolesPage", "Voice Channels"), Permission::DEAFEN_MEMBERS, QT_TRANSLATE_NOOP("GuildRolesPage", "Deafen Members"),
      QT_TRANSLATE_NOOP("GuildRolesPage", "Allows members to deafen other members in voice channels, which means they won't "
                                          "be able to speak or hear others.") },
    { QT_TRANSLATE_NOOP("GuildRolesPage", "Voice Channels"), Permission::MOVE_MEMBERS, QT_TRANSLATE_NOOP("GuildRolesPage", "Move Members"),
      QT_TRANSLATE_NOOP("GuildRolesPage", "Allows members to disconnect or move other members between voice channels that "
                                          "the member with this permission has access to.") },
    { QT_TRANSLATE_NOOP("GuildRolesPage", "Voice Channels"), Permission::SET_VOICE_CHANNEL_STATUS,
      QT_TRANSLATE_NOOP("GuildRolesPage", "Set Voice Channel Status"),
      QT_TRANSLATE_NOOP("GuildRolesPage", "Allows members to create and edit voice channel status."),
      PermissionGate::None, QT_TRANSLATE_NOOP("GuildRolesPage", "Set Voice Status") },

    { QT_TRANSLATE_NOOP("GuildRolesPage", "Apps"), Permission::USE_APPLICATION_COMMANDS, QT_TRANSLATE_NOOP("GuildRolesPage", "Use Application Commands"),
      QT_TRANSLATE_NOOP("GuildRolesPage", "Allows members to use commands from applications, including slash commands and context menu commands."),
      PermissionGate::None, QT_TRANSLATE_NOOP("GuildRolesPage", "Use App Commands") },
    { QT_TRANSLATE_NOOP("GuildRolesPage", "Apps"), Permission::USE_EMBEDDED_ACTIVITIES,
      QT_TRANSLATE_NOOP("GuildRolesPage", "Use Activities"), QT_TRANSLATE_NOOP("GuildRolesPage", "Allows members to use Activities.") },
    { QT_TRANSLATE_NOOP("GuildRolesPage", "Apps"), Permission::USE_EXTERNAL_APPS, QT_TRANSLATE_NOOP("GuildRolesPage", "Use External Apps"),
      QT_TRANSLATE_NOOP("GuildRolesPage", "Allows apps that members have added to their account to post messages. When "
                                          "disabled, the messages will be private.") },

    { QT_TRANSLATE_NOOP("GuildRolesPage", "Stage Channels"), Permission::REQUEST_TO_SPEAK, QT_TRANSLATE_NOOP("GuildRolesPage", "Request to Speak"),
      QT_TRANSLATE_NOOP("GuildRolesPage", "Allow requests to speak in Stage channels. Stage moderators manually approve or deny each request."),
      PermissionGate::Community },

    { QT_TRANSLATE_NOOP("GuildRolesPage", "Events"), Permission::CREATE_EVENTS,
      QT_TRANSLATE_NOOP("GuildRolesPage", "Create Events"), QT_TRANSLATE_NOOP("GuildRolesPage", "Allows members to create events.") },
    { QT_TRANSLATE_NOOP("GuildRolesPage", "Events"), Permission::MANAGE_EVENTS,
      QT_TRANSLATE_NOOP("GuildRolesPage", "Manage Events"),
      QT_TRANSLATE_NOOP("GuildRolesPage", "Allows members to edit and cancel events.") },

    { QT_TRANSLATE_NOOP("GuildRolesPage", "Advanced"), Permission::ADMINISTRATOR, QT_TRANSLATE_NOOP("GuildRolesPage", "Administrator"),
      QT_TRANSLATE_NOOP("GuildRolesPage", "Members with this permission will have every permission and will also bypass all "
                                          "channel specific permissions or restrictions (for example, these members would get "
                                          "access to all private channels). This is a dangerous permission to grant.") },
};

bool gateOpen(PermissionGate gate, const std::optional<Discord::Guild> &guild)
{
    switch (gate) {
    case PermissionGate::None:
        return true;
    case PermissionGate::Community:
        return guild && guild->hasFeature(QStringLiteral("COMMUNITY"));
    case PermissionGate::CreatorMonetizable:
        return guild &&
               (guild->hasFeature(QStringLiteral("CREATOR_MONETIZABLE")) || guild->hasFeature(QStringLiteral("CREATOR_MONETIZABLE_PROVISIONAL"))) &&
               !guild->hasFeature(QStringLiteral("CREATOR_MONETIZABLE_DISABLED"));
    }
    return false;
}

QColor roleColorOf(const Discord::Role &role)
{
    return role.hasColor() ? role.getColor() : QColor::fromRgb(DefaultRoleColor);
}

QIcon swatchIcon(const QColor &color)
{
    return GuildSettingsPage::colorSwatch(color, SwatchSize);
}

bool hasRoleIcon(const Discord::Role &role)
{
    return (role.icon.hasValue() && !role.icon->isEmpty()) || (role.unicodeEmoji.hasValue() && !role.unicodeEmoji->isEmpty());
}

QLabel *makeErrorLabel(QWidget *parent)
{
    auto *label = new QLabel(parent);
    label->setWordWrap(true);
    QPalette palette = label->palette();
    palette.setColor(QPalette::WindowText, Core::Theme::Manager::instance().color(Core::Theme::Token::ChatError));
    label->setPalette(palette);
    label->hide();
    return label;
}

bool editsDiffer(const Discord::Role &a, const Discord::Role &b)
{
    return Discord::RoleEdit::fromRole(a).toJson().toBytes() != Discord::RoleEdit::fromRole(b).toJson().toBytes();
}

bool sameColors(const Discord::Role &a, const Discord::Role &b)
{
    auto colorsOf = [](const Discord::Role &role) { return role.colors.hasValue() ? role.colors->toJson().toBytes() : QByteArray(); };
    return a.color.valueOr(0) == b.color.valueOr(0) && colorsOf(a) == colorsOf(b);
}

Discord::Role rebaseDraft(const Discord::Role &draft, const Discord::Role &base, Discord::Role current)
{
    if (draft.name.get() != base.name.get())
        current.name = draft.name.get();
    const Discord::Permissions toggled = draft.permissions.get() ^ base.permissions.get();
    current.permissions = (current.permissions.get() & ~toggled) | (draft.permissions.get() & toggled);
    if (!sameColors(draft, base)) {
        current.color = draft.color;
        current.colors = draft.colors;
    }
    if (draft.hoist.valueOr(false) != base.hoist.valueOr(false))
        current.hoist = draft.hoist;
    if (draft.mentionable.valueOr(false) != base.mentionable.valueOr(false))
        current.mentionable = draft.mentionable;
    return current;
}

QList<QPair<Core::Snowflake, int>> rolePositions(const QList<Core::Snowflake> &order)
{
    QList<QPair<Core::Snowflake, int>> positions;
    for (int index = int(order.size()) - 1; index >= 0; index--)
        positions.append({ order[index], int(order.size()) - index });
    return positions;
}

class MasonryLayout : public QLayout
{
public:
    MasonryLayout(int columnSpacing, int itemSpacing)
        : columnSpacing(columnSpacing), itemSpacing(itemSpacing)
    {
        setContentsMargins(0, 0, 0, 0);
    }

    ~MasonryLayout() override { qDeleteAll(items); }

    void addItem(QLayoutItem *item) override { items.append(item); }
    int count() const override { return int(items.size()); }
    QLayoutItem *itemAt(int index) const override { return items.value(index); }
    QLayoutItem *takeAt(int index) override { return index >= 0 && index < items.size() ? items.takeAt(index) : nullptr; }

    Qt::Orientations expandingDirections() const override { return {}; }
    bool hasHeightForWidth() const override { return true; }
    int heightForWidth(int width) const override { return arrange(QRect(0, 0, width, 0), false); }
    QSize minimumSize() const override { return QSize(columnWidth(), 0); }
    QSize sizeHint() const override { return QSize(columnWidth(), heightForWidth(columnWidth())); }

    void setGeometry(const QRect &rect) override
    {
        QLayout::setGeometry(rect);
        arrange(rect, true);
    }

private:
    int columnWidth() const
    {
        int width = 0;
        for (QLayoutItem *item : items)
            if (!item->isEmpty())
                width = std::max(width, item->sizeHint().width());
        return width;
    }

    int arrange(const QRect &rect, bool apply) const
    {
        const int minimumWidth = columnWidth();
        if (minimumWidth == 0)
            return 0;
        const int columns = std::max(1, (rect.width() + columnSpacing) / (minimumWidth + columnSpacing));
        const int width = (rect.width() - (columns - 1) * columnSpacing) / columns;

        std::vector<int> heights(columns, 0);
        for (QLayoutItem *item : items) {
            if (item->isEmpty())
                continue;
            const auto shortest = std::min_element(heights.begin(), heights.end());
            const int column = int(shortest - heights.begin());
            const int height = item->sizeHint().height();
            if (apply)
                item->setGeometry(QRect(rect.x() + column * (width + columnSpacing), rect.y() + *shortest, width, height));
            *shortest += height + itemSpacing;
        }
        return std::max(0, *std::max_element(heights.begin(), heights.end()) - itemSpacing);
    }

    QList<QLayoutItem *> items;
    int columnSpacing;
    int itemSpacing;
};

bool memberMatches(const Discord::Member &member, const Discord::User &user, const QString &query)
{
    if (query.isEmpty())
        return true;
    return user.username->contains(query, Qt::CaseInsensitive) ||
           (user.globalName.hasValue() && user.globalName->contains(query, Qt::CaseInsensitive)) ||
           (member.nick.hasValue() && member.nick->contains(query, Qt::CaseInsensitive));
}

class RoleItemDelegate : public QStyledItemDelegate
{
public:
    using QStyledItemDelegate::QStyledItemDelegate;

    void paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const override
    {
        QStyleOptionViewItem opt(option);
        initStyleOption(&opt, index);
        const QString name = opt.text;
        const QIcon dot = opt.icon;
        opt.text.clear();
        opt.icon = QIcon();
        QStyle *style = opt.widget ? opt.widget->style() : QApplication::style();
        style->drawControl(QStyle::CE_ItemViewItem, &opt, painter, opt.widget);

        const bool selected = opt.state & QStyle::State_Selected;
        const QColor muted = opt.palette.color(selected ? QPalette::HighlightedText : QPalette::PlaceholderText);
        QRect area = opt.rect.adjusted(8, 0, -8, 0);
        dot.paint(painter, QRect(area.left(), area.center().y() - 8, 16, 16));
        area.setLeft(area.left() + 24);

        painter->save();
        painter->setPen(muted);
        const QVariant count = index.data(MemberCountRole);
        if (count.isValid()) {
            const QString text = QString::number(count.toInt());
            const int width = opt.fontMetrics.horizontalAdvance(text);
            painter->drawText(QRect(area.right() - width, area.top(), width, area.height()), Qt::AlignVCenter | Qt::AlignRight, text);
            area.setRight(area.right() - width - 10);
        }
        if (index.data(LockedRole).toBool()) {
            const QPixmap lock = Core::Theme::Icons::pixmap(Core::Theme::Icons::Name::Lock, 14, muted, painter->device()->devicePixelRatioF());
            painter->drawPixmap(area.right() - 14, area.center().y() - 7, lock);
            area.setRight(area.right() - 20);
        }
        painter->setPen(opt.palette.color(selected ? QPalette::HighlightedText : QPalette::Text));
        painter->drawText(area, Qt::AlignVCenter | Qt::AlignLeft, opt.fontMetrics.elidedText(name, Qt::ElideRight, area.width()));
        painter->restore();
    }

    QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const override
    {
        QSize size = QStyledItemDelegate::sizeHint(option, index);
        size.setHeight(qMax(size.height(), 32));
        return size;
    }
};

class RoleListWidget : public QListWidget
{
public:
    using QListWidget::QListWidget;

    std::function<void()> onReordered;

protected:
    void dropEvent(QDropEvent *event) override
    {
        QListWidget::dropEvent(event);
        QTimer::singleShot(0, this, [this]() {
            if (onReordered)
                onReordered();
        });
    }
};

class AddRoleMembersDialog : public BasePopup
{
public:
    AddRoleMembersDialog(Core::ClientInstance *instance, Core::Snowflake guildId, Core::Snowflake roleId,
                         const QString &roleName, std::function<void(const QList<Core::Snowflake> &)> onAdded,
                         QWidget *parent)
        : BasePopup(parent), instance(instance), guildId(guildId), roleId(roleId), onAdded(std::move(onAdded))
    {
        setAttribute(Qt::WA_DeleteOnClose);

        auto *layout = new QVBoxLayout(getContainer());
        layout->setSpacing(8);
        layout->setContentsMargins(24, 24, 24, 24);

        auto *title = new QLabel(tr("Add members"), getContainer());
        QFont titleFont = title->font();
        titleFont.setBold(true);
        titleFont.setPointSize(titleFont.pointSize() + 2);
        title->setFont(titleFont);
        layout->addWidget(title);

        auto *subtitle = new QLabel(tr("Select up to %1 members to add to role <b>%2</b>").arg(AddMembersMax).arg(roleName.toHtmlEscaped()), getContainer());
        subtitle->setWordWrap(true);
        layout->addWidget(subtitle);

        search = new QLineEdit(getContainer());
        search->setPlaceholderText(tr("Search members"));
        search->setClearButtonEnabled(true);
        layout->addWidget(search);

        list = new QListWidget(getContainer());
        list->setMinimumHeight(260);
        layout->addWidget(list);

        status = makeErrorLabel(getContainer());
        layout->addWidget(status);

        auto *buttons = new QDialogButtonBox(getContainer());
        buttons->addButton(QDialogButtonBox::Cancel);
        addButton = buttons->addButton(tr("Add"), QDialogButtonBox::AcceptRole);
        addButton->setDefault(true);
        addButton->setEnabled(false);
        connect(buttons, &QDialogButtonBox::accepted, this, [this]() { add(); });
        connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
        layout->addWidget(buttons);

        auto *searchDebounce = new QTimer(this);
        searchDebounce->setSingleShot(true);
        searchDebounce->setInterval(AddMembersSearchDebounceMs);
        refilterTimer = new QTimer(this);
        refilterTimer->setSingleShot(true);
        refilterTimer->setInterval(100);

        connect(search, &QLineEdit::textChanged, searchDebounce, qOverload<>(&QTimer::start));
        connect(searchDebounce, &QTimer::timeout, this, [this]() {
            const QString query = search->text().trimmed();
            if (this->instance && !query.isEmpty())
                this->instance->discord()->queryGuildMembers(this->guildId, query, MemberSearchLimit);
            refilter();
        });
        connect(refilterTimer, &QTimer::timeout, this, [this]() { refilter(); });
        connect(list, &QListWidget::itemChanged, this, [this](QListWidgetItem *item) {
            const Core::Snowflake userId(item->data(UserIdRole).toULongLong());
            if (item->checkState() == Qt::Checked)
                chosen.insert(userId);
            else
                chosen.remove(userId);
            addButton->setEnabled(!chosen.isEmpty() && chosen.size() <= AddMembersMax);
        });
        connect(instance, &Core::ClientInstance::membersUpdated, this, [this](Core::Snowflake changedGuildId) {
            if (changedGuildId == this->guildId)
                refilterTimer->start();
        });

        refilter();
    }

private:
    void refilter()
    {
        if (!instance)
            return;
        const QString query = search->text().trimmed();

        struct Candidate
        {
            Core::Snowflake userId;
            QString name;
            QString username;
        };
        QList<Candidate> candidates;
        for (const Discord::Member &member : instance->users()->getKnownMembers(guildId)) {
            if (!member.user.hasValue() || member.roles.valueOr({}).contains(roleId))
                continue;
            const Discord::User &user = member.user.get();
            if (!memberMatches(member, user, query))
                continue;
            candidates.append({ user.id.get(), instance->users()->getDisplayName(user.id.get(), guildId), user.tag() });
        }
        std::sort(candidates.begin(), candidates.end(), [](const Candidate &a, const Candidate &b) { return a.name.localeAwareCompare(b.name) < 0; });

        QSignalBlocker blocker(list);
        list->clear();
        for (int i = 0; i < candidates.size() && i < AddMembersShown; i++) {
            const Candidate &candidate = candidates[i];
            auto *item = new QListWidgetItem(candidate.name == candidate.username
                                                     ? candidate.name
                                                     : QStringLiteral("%1 (%2)").arg(candidate.name, candidate.username),
                                             list);
            item->setData(UserIdRole, QVariant::fromValue<quint64>(candidate.userId));
            item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsUserCheckable);
            item->setCheckState(chosen.contains(candidate.userId) ? Qt::Checked : Qt::Unchecked);
        }
    }

    void add()
    {
        if (!instance || chosen.isEmpty() || chosen.size() > AddMembersMax)
            return;
        addButton->setEnabled(false);
        status->hide();
        const QList<Core::Snowflake> userIds = chosen.values();
        QPointer<AddRoleMembersDialog> self(this);
        instance->discord()->addRoleMembers(guildId, roleId, userIds, [self, userIds](const Core::Result<QList<Core::Snowflake>> &result) {
            if (!self)
                return;
            if (!result.success()) {
                self->status->setText(result.error);
                self->status->show();
                self->addButton->setEnabled(true);
                return;
            }
            if (self->onAdded)
                self->onAdded(userIds);
            self->accept();
        });
    }

    QPointer<Core::ClientInstance> instance;
    Core::Snowflake guildId;
    Core::Snowflake roleId;
    std::function<void(const QList<Core::Snowflake> &)> onAdded;
    QLineEdit *search;
    QListWidget *list;
    QLabel *status;
    QPushButton *addButton;
    QTimer *refilterTimer;
    QSet<Core::Snowflake> chosen;
};

} // namespace

GuildRolesPage::GuildRolesPage(Core::ClientInstance *instance, Core::ImageManager *images, Core::Snowflake guildId,
                               QWidget *parent)
    : GuildSettingsPage(instance, images, guildId, parent)
{
    refreshTimer = new QTimer(this);
    refreshTimer->setSingleShot(true);
    refreshTimer->setInterval(0);
    memberSearchDebounce = new QTimer(this);
    memberSearchDebounce->setSingleShot(true);
    memberSearchDebounce->setInterval(MemberSearchDebounceMs);
    memberRebuildTimer = new QTimer(this);
    memberRebuildTimer->setSingleShot(true);
    memberRebuildTimer->setInterval(100);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(24, 20, 24, 16);
    layout->addWidget(makeTitle(tr("Roles"), this));
    layout->addWidget(makeDescription(tr("Use roles to group your server members and assign permissions."), this));

    auto *split = new QHBoxLayout();
    split->setSpacing(16);
    split->addWidget(buildListPane());

    editorStates = new QStackedWidget(this);
    auto *placeholder = makeDescription(tr("Select a role to edit it."), editorStates);
    placeholder->setAlignment(Qt::AlignCenter);
    editorPlaceholder = placeholder;
    editorStates->addWidget(editorPlaceholder);

    editor = new QWidget(editorStates);
    auto *editorLayout = new QVBoxLayout(editor);
    editorLayout->setContentsMargins(0, 0, 0, 0);
    auto *header = new QHBoxLayout();
    editorTitle = makeFieldLabel(QString(), editor);
    editorTitle->setTextFormat(Qt::PlainText);
    QFont editorTitleFont = editorTitle->font();
    editorTitleFont.setPointSizeF(editorTitleFont.pointSizeF() * 1.15);
    editorTitle->setFont(editorTitleFont);
    header->addWidget(editorTitle, 1);
    moreButton = new QToolButton(editor);
    moreButton->setAutoRaise(true);
    moreButton->setIcon(Core::Theme::Icons::icon(Core::Theme::Icons::Name::Ellipsis, Core::Theme::Token::PrimaryText));
    moreButton->setToolTip(tr("More Options"));
    header->addWidget(moreButton);
    editorLayout->addLayout(header);

    editorBanner = new QLabel(editor);
    editorBanner->setTextFormat(Qt::PlainText);
    editorBanner->setWordWrap(true);
    editorBanner->setForegroundRole(QPalette::PlaceholderText);
    editorBanner->hide();
    editorLayout->addWidget(editorBanner);

    tabs = new QTabWidget(editor);
    displayTab = buildDisplayTab();
    permissionsTab = buildPermissionsTab();
    membersTab = buildMembersTab();
    tabs->addTab(displayTab, tr("Display"));
    tabs->addTab(permissionsTab, tr("Permissions"));
    tabs->addTab(membersTab, tr("Manage Members"));
    editorLayout->addWidget(tabs, 1);
    editorStates->addWidget(editor);
    split->addWidget(editorStates, 1);
    layout->addLayout(split, 1);

    saveBar = new UnsavedChangesBar(this);
    saveBar->hide();
    layout->addWidget(saveBar);

    connect(saveBar, &UnsavedChangesBar::resetClicked, this, &GuildRolesPage::resetChanges);
    connect(saveBar, &UnsavedChangesBar::saveClicked, this, &GuildRolesPage::save);
    connect(refreshTimer, &QTimer::timeout, this, &GuildRolesPage::refreshRoles);
    connect(memberRebuildTimer, &QTimer::timeout, this, &GuildRolesPage::rebuildMemberList);
    connect(memberSearchDebounce, &QTimer::timeout, this, [this]() {
        const QString query = memberSearch->text().trimmed();
        if (this->instance && !query.isEmpty())
            this->instance->discord()->queryGuildMembers(this->guildId, query, MemberSearchLimit);
        rebuildMemberList();
    });
    connect(moreButton, &QToolButton::clicked, this, [this]() { showRoleMenu(selectedRoleId, moreButton->mapToGlobal(QPoint(0, moreButton->height()))); });
    connect(tabs, &QTabWidget::currentChanged, this, [this]() {
        if (tabs->currentWidget() == membersTab)
            loadRoleMembers();
    });

    auto refreshIfOurs = [this](Core::Snowflake changedGuildId) {
        if (changedGuildId == this->guildId && !saving)
            refreshTimer->start();
    };
    connect(instance, &Core::ClientInstance::guildRoleCreated, this,
            [refreshIfOurs](const Discord::GuildRoleCreate &event) { refreshIfOurs(event.guildId.get()); });
    connect(instance, &Core::ClientInstance::guildRoleUpdated, this,
            [refreshIfOurs](const Discord::GuildRoleUpdate &event) { refreshIfOurs(event.guildId.get()); });
    connect(instance, &Core::ClientInstance::guildRoleDeleted, this,
            [refreshIfOurs](const Discord::GuildRoleDelete &event) { refreshIfOurs(event.guildId.get()); });
    connect(instance, &Core::ClientInstance::guildUpdated, this, [refreshIfOurs](const Discord::Guild &guild) { refreshIfOurs(guild.id.get()); });
    connect(instance, &Core::ClientInstance::membersUpdated, this, &GuildRolesPage::onMembersUpdated);
    connect(instance, &Core::ClientInstance::memberRemoved, this, &GuildRolesPage::onMemberRemoved);
    connect(instance->discord(), &Discord::Client::roleMemberCountChanged, this, [this](Core::Snowflake changedGuildId, Core::Snowflake roleId, int delta) {
        if (changedGuildId == this->guildId)
            adjustMemberCount(roleId, delta);
    });
    connect(images, &Core::ImageManager::imageFetched, this, &GuildRolesPage::onImageFetched);

    updatePermissions();
}

void GuildRolesPage::updatePermissions()
{
    const bool canManage = canManageRoles();
    createRoleButton->setVisible(canManage);
    emptyCreateButton->setVisible(canManage);
    membersHeader->setVisible(canManage);
    listHint->setText(canManage ? tr("Members use the color of the highest role they have on this list. Drag roles to reorder them.")
                                : tr("Members use the color of the highest role they have on this list."));
    emptyRolesTitle->setText(canManage ? tr("Organize your members") : tr("No Roles"));

    const bool managingChanged = canManage != managingRoles;
    managingRoles = canManage;
    if (!isLoaded())
        return;
    if (managingChanged) {
        memberCounts.clear();
        memberCountsFetchedAt = QDateTime();
        roleMembers.clear();
        fetchMemberCounts();
    }
    if (!saving)
        refreshTimer->start();
    if (managingChanged && tabs->currentWidget() == membersTab)
        loadRoleMembers();
}

QWidget *GuildRolesPage::buildListPane()
{
    auto *pane = new QWidget(this);
    pane->setFixedWidth(290);
    auto *layout = new QVBoxLayout(pane);
    layout->setContentsMargins(0, 8, 0, 0);
    layout->setSpacing(8);

    defaultPermissionsCard = new QPushButton(pane);
    defaultPermissionsCard->setCheckable(true);
    auto *cardLayout = new QVBoxLayout(defaultPermissionsCard);
    cardLayout->setContentsMargins(12, 8, 12, 8);
    cardLayout->setSpacing(2);
    auto *cardTitle = makeFieldLabel(tr("Default Permissions"), defaultPermissionsCard);
    auto *cardText = makeDescription(tr("@everyone • applies to all server members"), defaultPermissionsCard);
    cardTitle->setAttribute(Qt::WA_TransparentForMouseEvents);
    cardText->setAttribute(Qt::WA_TransparentForMouseEvents);
    cardLayout->addWidget(cardTitle);
    cardLayout->addWidget(cardText);
    defaultPermissionsCard->setMinimumHeight(cardLayout->sizeHint().height());
    layout->addWidget(defaultPermissionsCard);

    auto *searchRow = new QHBoxLayout();
    searchEdit = new QLineEdit(pane);
    searchEdit->setPlaceholderText(tr("Search Roles"));
    searchEdit->setClearButtonEnabled(true);
    searchRow->addWidget(searchEdit, 1);
    createRoleButton = new QPushButton(tr("Create Role"), pane);
    searchRow->addWidget(createRoleButton);
    layout->addLayout(searchRow);

    listHint = makeDescription(QString(), pane);
    layout->addWidget(listHint);

    auto *headerRow = new QHBoxLayout();
    rolesHeader = makeFieldLabel(QString(), pane);
    headerRow->addWidget(rolesHeader, 1);
    membersHeader = makeFieldLabel(tr("Members"), pane);
    headerRow->addWidget(membersHeader);
    layout->addLayout(headerRow);

    listStates = new QStackedWidget(pane);
    auto *list = new RoleListWidget(listStates);
    list->setItemDelegate(new RoleItemDelegate(list));
    list->setDragDropMode(QAbstractItemView::InternalMove);
    list->setDefaultDropAction(Qt::MoveAction);
    list->setSelectionMode(QAbstractItemView::SingleSelection);
    list->setContextMenuPolicy(Qt::CustomContextMenu);
    list->setUniformItemSizes(true);
    list->onReordered = [this]() { onListReordered(); };
    roleList = list;
    listStates->addWidget(roleList);

    auto *noMatch = makeDescription(tr("No roles"), listStates);
    noMatch->setAlignment(Qt::AlignHCenter | Qt::AlignTop);
    listNoMatch = noMatch;
    listStates->addWidget(listNoMatch);

    listEmpty = new QWidget(listStates);
    auto *emptyLayout = new QVBoxLayout(listEmpty);
    emptyLayout->addStretch();
    emptyRolesTitle = makeFieldLabel(QString(), listEmpty);
    emptyRolesTitle->setAlignment(Qt::AlignCenter);
    emptyLayout->addWidget(emptyRolesTitle);
    emptyCreateButton = new QPushButton(tr("Create Role"), listEmpty);
    emptyLayout->addWidget(emptyCreateButton, 0, Qt::AlignHCenter);
    emptyLayout->addStretch();
    listStates->addWidget(listEmpty);
    layout->addWidget(listStates, 1);

    connect(defaultPermissionsCard, &QPushButton::clicked, this, [this]() { selectRole(this->guildId); });
    connect(createRoleButton, &QPushButton::clicked, this, &GuildRolesPage::createRole);
    connect(emptyCreateButton, &QPushButton::clicked, this, &GuildRolesPage::createRole);
    connect(searchEdit, &QLineEdit::textChanged, this, &GuildRolesPage::filterList);
    connect(roleList, &QListWidget::currentItemChanged, this, [this](QListWidgetItem *current) {
        if (current)
            selectRole(Core::Snowflake(current->data(RoleIdRole).toULongLong()));
    });
    connect(roleList, &QListWidget::customContextMenuRequested, this, [this](const QPoint &pos) {
        if (QListWidgetItem *item = roleList->itemAt(pos))
            showRoleMenu(Core::Snowflake(item->data(RoleIdRole).toULongLong()), roleList->viewport()->mapToGlobal(pos));
    });
    return pane;
}

QWidget *GuildRolesPage::buildDisplayTab()
{
    auto *scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    auto *content = new QWidget(scroll);
    auto *layout = new QVBoxLayout(content);
    layout->setContentsMargins(4, 12, 12, 12);
    layout->setSpacing(6);

    layout->addWidget(makeFieldLabel(tr("Role name"), content));
    nameEdit = new QLineEdit(content);
    nameEdit->setMaxLength(RoleNameMaxLength);
    layout->addWidget(nameEdit);

    layout->addSpacing(12);
    layout->addWidget(makeFieldLabel(tr("Role color"), content));
    layout->addWidget(makeDescription(tr("Members use the color of the highest role they have on the roles list."), content));
    colorReadout = makeColorReadout(SwatchSize, content);
    layout->addWidget(colorReadout, 0, Qt::AlignLeft);
    colorPicker = new QWidget(content);
    auto *swatchRow = new QHBoxLayout(colorPicker);
    swatchRow->setContentsMargins(0, 0, 0, 0);
    swatchRow->setSpacing(10);
    auto makeSwatch = [this](const QIcon &icon, const QString &tooltip, const QSize &size) {
        auto *button = new QToolButton(colorPicker);
        button->setCheckable(true);
        button->setAutoRaise(true);
        button->setIconSize(size);
        button->setIcon(icon);
        button->setToolTip(tooltip);
        button->setCursor(Qt::PointingHandCursor);
        return button;
    };
    const QSize bigSwatch(SwatchSize.width() * 2, SwatchSize.height() * 2 - 4);
    auto *defaultSwatch = makeSwatch(swatchIcon(QColor::fromRgb(DefaultRoleColor)), tr("Default"), bigSwatch);
    colorSwatches.append({ defaultSwatch, 0 });
    swatchRow->addWidget(defaultSwatch);
    customColorSwatch = makeSwatch(swatchIcon(QColor::fromRgb(DefaultRoleColor)), tr("Custom Color"), bigSwatch);
    swatchRow->addWidget(customColorSwatch);
    auto *presetGrid = new QGridLayout();
    presetGrid->setSpacing(4);
    for (int i = 0; i < int(std::size(PresetColors)); i++) {
        auto *swatch = makeSwatch(swatchIcon(QColor::fromRgb(PresetColors[i])), QColor::fromRgb(PresetColors[i]).name(), SwatchSize);
        colorSwatches.append({ swatch, PresetColors[i] });
        presetGrid->addWidget(swatch, i / PresetsPerRow, i % PresetsPerRow);
    }
    swatchRow->addLayout(presetGrid);
    swatchRow->addStretch();
    layout->addWidget(colorPicker);
    for (const ColorSwatch &swatch : colorSwatches) {
        const int color = swatch.color;
        connect(swatch.button, &QToolButton::clicked, this, [this, color]() { setSelectedColor(color); });
    }
    connect(customColorSwatch, &QToolButton::clicked, this, &GuildRolesPage::chooseCustomColor);

    iconSection = new QWidget(content);
    auto *iconLayout = new QVBoxLayout(iconSection);
    iconLayout->setContentsMargins(0, 12, 0, 0);
    iconLayout->setSpacing(6);
    iconLayout->addWidget(makeFieldLabel(tr("Role icon"), iconSection));
    iconDescription = makeDescription(tr("Upload an image under 256 KB. We recommend at least 64x64 pixels. Members will "
                                         "see the icon for their highest role if they have multiple roles."),
                                      iconSection);
    iconLayout->addWidget(iconDescription);
    auto *iconRow = new QHBoxLayout();
    iconPreview = new QLabel(iconSection);
    iconPreview->setFixedSize(IconPreviewSize);
    iconPreview->setAlignment(Qt::AlignCenter);
    iconPreview->setFrameShape(QFrame::StyledPanel);
    iconRow->addWidget(iconPreview);
    chooseIconButton = new QPushButton(tr("Choose Image"), iconSection);
    iconRow->addWidget(chooseIconButton);
    removeIconButton = new QPushButton(tr("Remove Icon"), iconSection);
    removeIconButton->setFlat(true);
    iconRow->addWidget(removeIconButton);
    iconRow->addStretch();
    iconLayout->addLayout(iconRow);
    iconError = makeErrorLabel(iconSection);
    iconLayout->addWidget(iconError);
    layout->addWidget(iconSection);
    connect(chooseIconButton, &QPushButton::clicked, this, &GuildRolesPage::chooseIcon);
    connect(removeIconButton, &QPushButton::clicked, this, &GuildRolesPage::removeIcon);

    layout->addSpacing(12);
    hoistCheck = new QCheckBox(tr("Display role members separately from online members"), content);
    layout->addWidget(hoistCheck);
    mentionableCheck = new QCheckBox(tr("Allow anyone to @mention this role"), content);
    layout->addWidget(mentionableCheck);
    layout->addWidget(makeDescription(tr("Note: Members with the \"Mention @everyone, @here, and All Roles\" permission will "
                                         "always be able to ping this role."),
                                      content));
    layout->addStretch();
    scroll->setWidget(content);

    connect(nameEdit, &QLineEdit::textEdited, this, [this](const QString &text) { editSelected([text](Discord::Role &role) { role.name = text; }); });
    connect(hoistCheck, &QCheckBox::toggled, this, [this](bool checked) { editSelected([checked](Discord::Role &role) { role.hoist = checked; }); });
    connect(mentionableCheck, &QCheckBox::toggled, this, [this](bool checked) {
        editSelected([checked](Discord::Role &role) { role.mentionable = checked; });
    });
    return scroll;
}

QWidget *GuildRolesPage::buildPermissionsTab()
{
    auto *tab = new QWidget(this);
    auto *tabLayout = new QVBoxLayout(tab);
    tabLayout->setContentsMargins(4, 12, 4, 0);

    auto *toolbar = new QHBoxLayout();
    permissionSearch = new QLineEdit(tab);
    permissionSearch->setPlaceholderText(tr("Search permissions"));
    permissionSearch->setClearButtonEnabled(true);
    toolbar->addWidget(permissionSearch, 1);
    clearPermissionsButton = new QPushButton(tr("Clear permissions"), tab);
    toolbar->addWidget(clearPermissionsButton);
    tabLayout->addLayout(toolbar);

    auto *scroll = new QScrollArea(tab);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    auto *content = new QWidget(scroll);
    auto *layout = new QVBoxLayout(content);
    layout->setContentsMargins(0, 8, 8, 8);

    noPermissionsLabel = makeDescription(tr("No permissions found"), content);
    noPermissionsLabel->hide();
    layout->addWidget(noPermissionsLabel);
    auto *columns = new MasonryLayout(20, 14);
    layout->addLayout(columns);
    layout->addStretch();

    const int indent = style()->pixelMetric(QStyle::PM_IndicatorWidth) + style()->pixelMetric(QStyle::PM_CheckBoxLabelSpacing);
    const auto guild = instance ? instance->getGuild(guildId) : std::nullopt;
    QString currentSection;
    QVBoxLayout *sectionRows = nullptr;
    for (const PermissionInfo &info : PermissionCatalog) {
        if (!gateOpen(info.gate, guild))
            continue;
        if (currentSection != QLatin1String(info.section)) {
            currentSection = QLatin1String(info.section);
            auto *block = new QWidget(content);
            auto *blockLayout = new QVBoxLayout(block);
            blockLayout->setContentsMargins(0, 0, 0, 0);
            blockLayout->setSpacing(2);
            auto *toggle = new QCheckBox(tr(info.section), block);
            QFont toggleFont = toggle->font();
            toggleFont.setBold(true);
            toggle->setFont(toggleFont);
            toggle->setToolTip(tr("Grant or remove every permission here that you can change"));
            blockLayout->addWidget(toggle);
            sectionRows = new QVBoxLayout();
            sectionRows->setContentsMargins(indent, 0, 0, 0);
            sectionRows->setSpacing(0);
            blockLayout->addLayout(sectionRows);
            columns->addWidget(block);

            const int section = int(permissionSections.size());
            permissionSections.append({ block, toggle });
            connect(toggle, &QCheckBox::clicked, this, [this, section]() { toggleSection(section); });
        }

        auto *check = new QCheckBox(tr(info.shortTitle ? info.shortTitle : info.title), permissionSections.last().block);
        sectionRows->addWidget(check);
        const Permission permission = info.permission;
        permissionRows.append({ permission, tr(info.title), tr(info.description), check, int(permissionSections.size()) - 1 });
        connect(check, &QCheckBox::toggled, this, [this, permission](bool checked) {
            editSelected([permission, checked](Discord::Role &role) {
                Discord::Permissions permissions = role.permissions.get();
                permissions.setFlag(permission, checked);
                role.permissions = permissions;
            });
        });
    }
    scroll->setWidget(content);
    tabLayout->addWidget(scroll, 1);

    connect(permissionSearch, &QLineEdit::textChanged, this, &GuildRolesPage::filterPermissions);
    connect(clearPermissionsButton, &QPushButton::clicked, this, [this]() {
        const Discord::Permissions cleared = clearablePermissions();
        editSelected([cleared](Discord::Role &role) { role.permissions = role.permissions.get() & ~cleared; });
    });
    return tab;
}

QWidget *GuildRolesPage::buildMembersTab()
{
    auto *tab = new QWidget(this);
    auto *layout = new QVBoxLayout(tab);
    layout->setContentsMargins(4, 12, 4, 0);

    auto *toolbar = new QHBoxLayout();
    memberSearch = new QLineEdit(tab);
    memberSearch->setPlaceholderText(tr("Search Members"));
    memberSearch->setClearButtonEnabled(true);
    toolbar->addWidget(memberSearch, 1);
    addMembersButton = new QPushButton(tr("Add Members"), tab);
    toolbar->addWidget(addMembersButton);
    layout->addLayout(toolbar);

    membersNote = makeDescription(tr("Not all members are shown, use Search to find specific members"), tab);
    membersNote->hide();
    layout->addWidget(membersNote);

    memberStates = new QStackedWidget(tab);
    memberList = new QTreeWidget(memberStates);
    memberList->setColumnCount(2);
    memberList->setHeaderHidden(true);
    memberList->setRootIsDecorated(false);
    memberList->setUniformRowHeights(true);
    memberList->setIconSize(MemberAvatarSize);
    memberList->header()->setStretchLastSection(false);
    memberList->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    memberList->header()->setSectionResizeMode(1, QHeaderView::Fixed);
    memberList->setColumnWidth(1, 36);
    memberStates->addWidget(memberList);
    membersEmpty = makeDescription(tr("No members were found. Add members to this role."), memberStates);
    membersEmpty->setAlignment(Qt::AlignCenter);
    memberStates->addWidget(membersEmpty);
    layout->addWidget(memberStates, 1);

    connect(memberSearch, &QLineEdit::textChanged, memberSearchDebounce, qOverload<>(&QTimer::start));
    connect(addMembersButton, &QPushButton::clicked, this, &GuildRolesPage::openAddMembersDialog);
    return tab;
}

void GuildRolesPage::load()
{
    refreshRoles();
    fetchMemberCounts();
}

void GuildRolesPage::showEvent(QShowEvent *event)
{
    if (memberCountsFetchedAt.isValid() && memberCountsFetchedAt.secsTo(QDateTime::currentDateTimeUtc()) > MemberCountsMaxAgeSecs)
        fetchMemberCounts();
    GuildSettingsPage::showEvent(event);
}

std::optional<Discord::Role> GuildRolesPage::savedRole(Core::Snowflake roleId) const
{
    for (const Discord::Role &role : savedRoles)
        if (role.id.get() == roleId)
            return role;
    return std::nullopt;
}

Discord::Role GuildRolesPage::currentRole(Core::Snowflake roleId) const
{
    const auto draft = drafts.constFind(roleId);
    if (draft != drafts.constEnd())
        return draft.value();
    return savedRole(roleId).value_or(Discord::Role());
}

QList<Core::Snowflake> GuildRolesPage::savedOrder() const
{
    QList<Core::Snowflake> order;
    for (const Discord::Role &role : savedRoles)
        if (!isEveryone(role.id.get()))
            order.append(role.id.get());
    return order;
}

bool GuildRolesPage::canManageRoles() const
{
    return hasPermission(Permission::MANAGE_ROLES);
}

bool GuildRolesPage::isLocked(const Discord::Role &role) const
{
    return !canManageRoles() || isOutranked(role);
}

bool GuildRolesPage::isOutranked(const Discord::Role &role) const
{
    return canManageRoles() && (!hierarchy || !hierarchy->outranksRole(role));
}

QString GuildRolesPage::lockReason(const Discord::Role &role) const
{
    const auto highest = hierarchy ? hierarchy->selfHighestRole() : std::nullopt;
    if (highest && highest->id.get() == role.id.get())
        return tr("Role is locked because it is your highest ranked role. Please ask a higher rank or Server Owner for help.");
    return tr("Role is locked because it is a higher rank than your highest role.");
}

QString GuildRolesPage::managedReason(const Discord::Role &role) const
{
    const Discord::RoleTags tags = role.tags.valueOr(Discord::RoleTags());
    if (tags.premiumSubscriber)
        return tr("This role is automatically managed by Discord for Server Boosting. It cannot be manually assigned to members or deleted.");
    if (tags.guildConnections)
        return tr("Members are automatically assigned to this role. It cannot be manually assigned to or removed from members.");
    if (tags.botId.hasValue() || tags.integrationId.hasValue()) {
        QString name = role.name.get();
        if (tags.botId.hasValue() && instance)
            if (const auto bot = instance->users()->getUser(tags.botId.get()))
                name = bot->username.get();
        return tr("This role is managed by an integration: %1. It cannot be manually assigned to members. You can remove the integration to remove this role.")
                .arg(name);
    }
    return tr("This role is automatically managed by an integration.");
}

bool GuildRolesPage::affectsSelf(const Discord::Role &role) const
{
    return hierarchy && !hierarchy->isOwner() && hierarchy->selfHolds(role.id.get());
}

Discord::Permissions GuildRolesPage::selfPermissionsWith(Core::Snowflake roleId, Discord::Permissions permissions) const
{
    if (!hierarchy)
        return Discord::NO_PERMISSIONS;
    QList<Discord::Role> roles;
    for (const Discord::Role &saved : savedRoles) {
        const auto draft = drafts.constFind(saved.id.get());
        Discord::Role role = draft != drafts.constEnd() ? draft.value() : saved;
        if (role.id.get() == roleId)
            role.permissions = permissions;
        roles.append(role);
    }
    return Core::PermissionComputer::computeBasePermissions(hierarchy->owner(), selfId(), guildId, hierarchy->selfRoles(), roles);
}

Discord::Permissions GuildRolesPage::clearablePermissions() const
{
    Discord::Permissions clearable = Discord::NO_PERMISSIONS;
    for (const PermissionRow &row : permissionRows)
        if (row.changeable && row.check->isChecked())
            clearable |= row.permission;
    return clearable;
}

Discord::RoleEdit GuildRolesPage::editFor(Core::Snowflake roleId) const
{
    Discord::RoleEdit edit = Discord::RoleEdit::fromRole(currentRole(roleId));
    const auto iconChange = iconChanges.constFind(roleId);
    if (iconChange != iconChanges.constEnd()) {
        if (iconChange->dataUri.isEmpty())
            edit.icon = nullptr;
        else
            edit.icon = iconChange->dataUri;
        edit.unicodeEmoji = nullptr;
    }
    return edit;
}

bool GuildRolesPage::isRoleDirty(Core::Snowflake roleId) const
{
    return iconChanges.contains(roleId) || drafts.contains(roleId);
}

QList<Core::Snowflake> GuildRolesPage::dirtyRoles() const
{
    QList<Core::Snowflake> dirty;
    for (const Discord::Role &role : savedRoles)
        if (isRoleDirty(role.id.get()))
            dirty.append(role.id.get());
    return dirty;
}

bool GuildRolesPage::orderDirty() const
{
    return draftOrder != savedOrder();
}

bool GuildRolesPage::hasUnsavedChanges() const
{
    return orderDirty() || !dirtyRoles().isEmpty();
}

void GuildRolesPage::warnUnsavedChanges()
{
    saveBar->flash();
}

QList<QPair<Core::Snowflake, int>> GuildRolesPage::changedPositions(const QList<Core::Snowflake> &order) const
{
    QList<QPair<Core::Snowflake, int>> changed;
    for (const auto &position : rolePositions(order)) {
        const auto saved = savedRole(position.first);
        if (!saved || saved->position.get() != position.second)
            changed.append(position);
    }
    return changed;
}

void GuildRolesPage::refreshRoles()
{
    if (!instance)
        return;

    const bool reordered = orderDirty();
    const QList<Discord::Role> previous = savedRoles;
    hierarchy = instance->selfRoleHierarchy(guildId);
    savedRoles = hierarchy ? hierarchy->sortedRoles() : QList<Discord::Role>();
    rebaseDrafts(previous);

    const QList<Core::Snowflake> order = savedOrder();
    draftOrder = reordered ? mergedOrder(order) : order;

    for (auto it = iconChanges.begin(); it != iconChanges.end();) {
        if (savedRole(it.key()))
            ++it;
        else
            it = iconChanges.erase(it);
    }
    if (selectedRoleId.isValid() && !savedRole(selectedRoleId))
        selectedRoleId = Core::Snowflake();

    rebuildList();
    updateEditor();
    updateSaveBar();
}

void GuildRolesPage::rebaseDrafts(const QList<Discord::Role> &previous)
{
    for (auto it = drafts.begin(); it != drafts.end();) {
        const auto base = std::find_if(previous.cbegin(), previous.cend(), [&it](const Discord::Role &role) { return role.id.get() == it.key(); });
        const auto current = savedRole(it.key());
        if (base == previous.cend() || !current) {
            it = drafts.erase(it);
            continue;
        }
        const Discord::Role rebased = rebaseDraft(it.value(), *base, *current);
        if (!editsDiffer(rebased, *current)) {
            it = drafts.erase(it);
            continue;
        }
        it.value() = rebased;
        ++it;
    }
}

QList<Core::Snowflake> GuildRolesPage::mergedOrder(const QList<Core::Snowflake> &order) const
{
    QList<Core::Snowflake> merged;
    for (Core::Snowflake roleId : order)
        if (isLocked(currentRole(roleId)))
            merged.append(roleId);
    const int lockedCount = int(merged.size());
    for (Core::Snowflake roleId : draftOrder)
        if (order.contains(roleId) && !merged.contains(roleId))
            merged.append(roleId);
    for (int i = 0; i < order.size(); i++) {
        if (merged.contains(order[i]))
            continue;
        merged.insert(i > 0 ? qMax(lockedCount, int(merged.indexOf(order[i - 1])) + 1) : lockedCount, order[i]);
    }
    return merged;
}

void GuildRolesPage::rebuildList()
{
    createRoleButton->setEnabled(!saving);
    emptyCreateButton->setEnabled(!saving);
    {
        QSignalBlocker blocker(roleList);
        const int scrolled = roleList->verticalScrollBar()->value();
        roleList->clear();
        for (Core::Snowflake roleId : draftOrder) {
            auto *item = new QListWidgetItem(roleList);
            fillListItem(item, roleId);
            if (roleId == selectedRoleId)
                roleList->setCurrentItem(item);
        }
        roleList->verticalScrollBar()->setValue(scrolled);
    }
    defaultPermissionsCard->setChecked(isEveryone(selectedRoleId));
    filterList();
}

void GuildRolesPage::fillListItem(QListWidgetItem *item, Core::Snowflake roleId)
{
    const Discord::Role role = currentRole(roleId);
    const bool outranked = isOutranked(role);
    item->setData(RoleIdRole, QVariant::fromValue<quint64>(roleId));
    item->setText(role.name.get());
    item->setIcon(colorDot(roleColorOf(role)));
    item->setData(LockedRole, outranked);
    item->setToolTip(outranked ? lockReason(role) : QString());
    const auto count = memberCounts.constFind(roleId);
    item->setData(MemberCountRole, count != memberCounts.constEnd() ? QVariant(count.value()) : QVariant());
    Qt::ItemFlags flags = Qt::ItemIsEnabled | Qt::ItemIsSelectable;
    if (!isLocked(role) && !saving)
        flags |= Qt::ItemIsDragEnabled;
    item->setFlags(flags);
}

void GuildRolesPage::updateListItem(Core::Snowflake roleId)
{
    for (int row = 0; row < roleList->count(); row++) {
        QListWidgetItem *item = roleList->item(row);
        if (Core::Snowflake(item->data(RoleIdRole).toULongLong()) == roleId) {
            fillListItem(item, roleId);
            return;
        }
    }
}

void GuildRolesPage::filterList()
{
    const QString text = searchEdit->text().trimmed();
    int visible = 0;
    for (int row = 0; row < roleList->count(); row++) {
        QListWidgetItem *item = roleList->item(row);
        const bool match = text.isEmpty() || item->text().contains(text, Qt::CaseInsensitive);
        item->setHidden(!match);
        visible += match ? 1 : 0;
    }
    roleList->setDragDropMode(text.isEmpty() ? QAbstractItemView::InternalMove : QAbstractItemView::NoDragDrop);
    rolesHeader->setText(tr("Roles — %1").arg(visible));

    if (draftOrder.isEmpty())
        listStates->setCurrentWidget(listEmpty);
    else
        listStates->setCurrentWidget(visible == 0 ? listNoMatch : roleList);
}

void GuildRolesPage::onListReordered()
{
    QList<Core::Snowflake> order;
    for (int row = 0; row < roleList->count(); row++)
        order.append(Core::Snowflake(roleList->item(row)->data(RoleIdRole).toULongLong()));

    QList<Core::Snowflake> lockedBefore;
    for (Core::Snowflake roleId : draftOrder)
        if (isLocked(currentRole(roleId)))
            lockedBefore.append(roleId);

    QList<Core::Snowflake> lockedAfter;
    bool valid = order.size() == draftOrder.size();
    bool seenUnlocked = false;
    for (Core::Snowflake roleId : order) {
        if (!isLocked(currentRole(roleId))) {
            seenUnlocked = true;
            continue;
        }
        if (seenUnlocked)
            valid = false;
        lockedAfter.append(roleId);
    }

    if (valid && lockedBefore == lockedAfter)
        draftOrder = order;
    rebuildList();
    updateSaveBar();
}

void GuildRolesPage::selectRole(Core::Snowflake roleId)
{
    const bool changed = roleId != selectedRoleId;
    selectedRoleId = roleId;
    {
        QSignalBlocker blocker(roleList);
        roleList->clearSelection();
        roleList->setCurrentItem(nullptr);
        for (int row = 0; row < roleList->count(); row++) {
            if (Core::Snowflake(roleList->item(row)->data(RoleIdRole).toULongLong()) == roleId) {
                roleList->setCurrentItem(roleList->item(row));
                break;
            }
        }
    }
    defaultPermissionsCard->setChecked(isEveryone(roleId));

    if (changed) {
        QSignalBlocker blocker(memberSearch);
        memberSearch->clear();
        iconError->hide();
    }
    updateEditor();
    if (tabs->currentWidget() == membersTab)
        loadRoleMembers();
}

void GuildRolesPage::updateEditor()
{
    if (!selectedRoleId.isValid() || !savedRole(selectedRoleId)) {
        editorStates->setCurrentWidget(editorPlaceholder);
        return;
    }
    editorStates->setCurrentWidget(editor);

    const Discord::Role role = currentRole(selectedRoleId);
    const bool everyone = isEveryone(selectedRoleId);
    const bool locked = isLocked(role);
    const bool editable = !locked && !saving;

    editorTitle->setText(tr("Edit Role — %1").arg(role.name.get()));
    QStringList banner;
    if (isOutranked(role))
        banner.append(lockReason(role));
    if (role.isManaged())
        banner.append(managedReason(role));
    editorBanner->setText(banner.join(QStringLiteral("\n\n")));
    editorBanner->setVisible(!banner.isEmpty());

    const int displayIndex = tabs->indexOf(displayTab);
    const int membersIndex = tabs->indexOf(membersTab);
    tabs->setTabVisible(displayIndex, !everyone);
    tabs->setTabVisible(membersIndex, !everyone);
    tabs->setTabEnabled(membersIndex, !role.isManaged());
    tabs->setTabToolTip(membersIndex, role.isManaged() ? tr("Members cannot be manually added or removed from this role") : QString());
    if (!tabs->isTabEnabled(tabs->currentIndex()) || !tabs->isTabVisible(tabs->currentIndex()))
        tabs->setCurrentWidget(everyone ? permissionsTab : displayTab);
    updateMembersTabLabel();

    updateDisplayTab(role, editable);
    updatePermissionsTab(role, editable);
    addMembersButton->setVisible(!locked && !role.isManaged());
    addMembersButton->setEnabled(editable);
    if (tabs->currentWidget() == membersTab)
        rebuildMemberList();
}

void GuildRolesPage::updateDisplayTab(const Discord::Role &role, bool editable)
{
    const Core::Snowflake roleId = role.id.get();
    {
        QSignalBlocker nameBlocker(nameEdit);
        QSignalBlocker hoistBlocker(hoistCheck);
        QSignalBlocker mentionableBlocker(mentionableCheck);
        if (nameEdit->text() != role.name.get())
            nameEdit->setText(role.name.get());
        hoistCheck->setChecked(role.hoist.valueOr(false));
        mentionableCheck->setChecked(role.mentionable.valueOr(false));
    }
    nameEdit->setReadOnly(!editable);
    hoistCheck->setEnabled(editable);
    mentionableCheck->setEnabled(editable);

    const bool changeable = !isLocked(role);
    const int color = role.color.valueOr(0);
    colorPicker->setVisible(changeable);
    colorReadout->setVisible(!changeable);
    colorReadout->setIcon(swatchIcon(roleColorOf(role)));
    colorReadout->setText(color == 0 ? tr("Default") : QColor::fromRgb(color).name().toUpper());
    bool matched = false;
    for (const ColorSwatch &swatch : colorSwatches) {
        const bool selected = swatch.color == color;
        matched = matched || selected;
        swatch.button->setChecked(selected);
        swatch.button->setEnabled(editable);
    }
    customColorSwatch->setChecked(!matched);
    customColorSwatch->setIcon(swatchIcon(matched ? QColor::fromRgb(DefaultRoleColor) : QColor::fromRgb(color)));
    customColorSwatch->setEnabled(editable);

    QPixmap preview;
    QString previewText;
    bool hasIcon = hasRoleIcon(role);
    roleIconUrl = QUrl();
    const auto iconChange = iconChanges.constFind(roleId);
    if (iconChange != iconChanges.constEnd()) {
        hasIcon = !iconChange->dataUri.isEmpty();
        preview = iconChange->preview;
    } else if (role.icon.hasValue() && !role.icon->isEmpty()) {
        roleIconUrl = Discord::Cdn::roleIcon(roleId, role.icon.get(), 64);
        if (instance)
            preview = images->get(roleIconUrl, IconPreviewSize, instance->accountId());
    } else if (hasIcon) {
        previewText = role.unicodeEmoji.get();
    }
    if (!preview.isNull())
        iconPreview->setPixmap(preview.scaled(IconPreviewSize * preview.devicePixelRatio(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
    else
        iconPreview->setText(previewText);

    const auto guild = instance ? instance->getGuild(guildId) : std::nullopt;
    iconSection->setVisible(guild && guild->hasFeature(QStringLiteral("ROLE_ICONS")) && (changeable || hasIcon));
    iconDescription->setVisible(changeable);
    chooseIconButton->setVisible(changeable);
    removeIconButton->setVisible(changeable);
    chooseIconButton->setEnabled(editable);
    removeIconButton->setEnabled(editable && hasIcon);
}

void GuildRolesPage::updatePermissionsTab(const Discord::Role &role, bool editable)
{
    const Discord::Permissions rolePermissions = role.permissions.get();
    const Discord::Permissions self = instance ? instance->permissions()->getGuildPermissions(selfId(), guildId) : Discord::NO_PERMISSIONS;
    const bool mayLoseAccess = editable && affectsSelf(role);
    const QString errorColor = Core::Theme::Manager::instance().color(Core::Theme::Token::ChatError).name();

    for (PermissionRow &row : permissionRows) {
        const bool granted = rolePermissions.testFlag(row.permission);
        {
            QSignalBlocker blocker(row.check);
            row.check->setChecked(granted);
        }
        QString reason;
        if (editable) {
            if (!self.testFlag(row.permission)) {
                reason = tr("You cannot modify this permission because none of your roles have it.");
            } else if (granted && mayLoseAccess) {
                Discord::Permissions without = rolePermissions;
                without.setFlag(row.permission, false);
                if (!selfPermissionsWith(role.id.get(), without).testFlag(row.permission))
                    reason = tr("You cannot modify this permission because removing it would remove it from you.");
            }
        }
        row.changeable = editable && reason.isEmpty();
        row.check->setEnabled(row.changeable);

        QString tooltip = QStringLiteral("<b>%1</b><br>%2").arg(row.title.toHtmlEscaped(), row.description.toHtmlEscaped());
        if (!reason.isEmpty())
            tooltip += QStringLiteral("<br><br><span style=\"color: %1;\">%2</span>").arg(errorColor, reason.toHtmlEscaped());
        row.check->setToolTip(tooltip);
    }
    updateSectionToggles();

    const Discord::Permissions cleared = rolePermissions & ~clearablePermissions();
    bool clearable = cleared != rolePermissions;
    QString clearTooltip;
    if (clearable && mayLoseAccess && selfPermissionsWith(role.id.get(), cleared) != selfPermissionsWith(role.id.get(), rolePermissions)) {
        clearable = false;
        clearTooltip = tr("You cannot clear permissions because it would remove one or more permissions from you.");
    }
    clearPermissionsButton->setVisible(!isLocked(role));
    clearPermissionsButton->setEnabled(clearable);
    clearPermissionsButton->setToolTip(clearTooltip);
}

void GuildRolesPage::updateMembersTabLabel()
{
    const auto count = memberCounts.constFind(selectedRoleId);
    tabs->setTabText(tabs->indexOf(membersTab), count != memberCounts.constEnd() && !isEveryone(selectedRoleId)
                                                        ? tr("Manage Members (%1)").arg(count.value())
                                                : canManageRoles() ? tr("Manage Members")
                                                                   : tr("Members"));
}

void GuildRolesPage::updateSectionToggles()
{
    for (int section = 0; section < permissionSections.size(); section++) {
        int shown = 0;
        int granted = 0;
        bool changeable = false;
        for (const PermissionRow &row : permissionRows) {
            if (row.section != section || !row.matchesSearch)
                continue;
            shown++;
            granted += row.check->isChecked() ? 1 : 0;
            changeable = changeable || row.changeable;
        }

        QCheckBox *toggle = permissionSections[section].toggle;
        QSignalBlocker blocker(toggle);
        toggle->setCheckState(granted == 0 ? Qt::Unchecked : granted == shown ? Qt::Checked
                                                                              : Qt::PartiallyChecked);
        toggle->setEnabled(changeable);
    }
}

void GuildRolesPage::filterPermissions()
{
    const QString text = permissionSearch->text().trimmed();
    QSet<int> matchedSections;
    for (PermissionRow &row : permissionRows) {
        row.matchesSearch = text.isEmpty() || row.title.contains(text, Qt::CaseInsensitive) || row.check->text().contains(text, Qt::CaseInsensitive);
        row.check->setVisible(row.matchesSearch);
        if (row.matchesSearch)
            matchedSections.insert(row.section);
    }
    for (int section = 0; section < permissionSections.size(); section++)
        permissionSections[section].block->setVisible(matchedSections.contains(section));
    noPermissionsLabel->setVisible(matchedSections.isEmpty());
    updateSectionToggles();
}

void GuildRolesPage::toggleSection(int section)
{
    QList<Permission> changeable;
    bool allGranted = true;
    for (const PermissionRow &row : permissionRows) {
        if (row.section != section || !row.matchesSearch || !row.changeable)
            continue;
        changeable.append(row.permission);
        allGranted = allGranted && row.check->isChecked();
    }

    editSelected([&changeable, allGranted](Discord::Role &role) {
        Discord::Permissions permissions = role.permissions.get();
        for (Permission permission : changeable)
            permissions.setFlag(permission, !allGranted);
        role.permissions = permissions;
    });
    updateSectionToggles();
}

void GuildRolesPage::editSelected(const std::function<void(Discord::Role &)> &change)
{
    const auto saved = savedRole(selectedRoleId);
    if (saving || !saved || isLocked(*saved))
        return;
    Discord::Role role = currentRole(selectedRoleId);
    change(role);
    if (editsDiffer(role, *saved))
        drafts.insert(selectedRoleId, role);
    else
        drafts.remove(selectedRoleId);

    updateListItem(selectedRoleId);
    filterList();
    updateEditor();
    updateSaveBar();
}

void GuildRolesPage::setSelectedColor(int color)
{
    editSelected([color](Discord::Role &role) {
        role.color = color;
        role.colors = Discord::RoleColors::solid(color);
    });
}

void GuildRolesPage::chooseCustomColor()
{
    const Discord::Role role = currentRole(selectedRoleId);
    const int current = role.color.valueOr(0);
    auto *dialog = new QColorDialog(QColor::fromRgb(current != 0 ? current : DefaultRoleColor), this);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setWindowTitle(tr("Custom Color"));
    const Core::Snowflake roleId = selectedRoleId;
    connect(dialog, &QColorDialog::colorSelected, this, [this, roleId](const QColor &color) {
        if (roleId == selectedRoleId && color.isValid())
            setSelectedColor(int(color.rgb() & 0xffffff));
    });
    connect(dialog, &QDialog::rejected, this, [this]() { updateEditor(); });
    dialog->open();
}

void GuildRolesPage::chooseIcon()
{
    const Core::Snowflake roleId = selectedRoleId;
    chooseFiles(this, tr("Choose Image"), tr("Images (*.png *.jpg *.jpeg)"), false, [this, roleId](const QStringList &paths) {
        if (roleId != selectedRoleId)
            return;
        QFile file(paths.first());
        if (!file.open(QIODevice::ReadOnly))
            return;
        if (file.size() > RoleIconMaxBytes) {
            iconError->setText(tr("Oh no! File is too big. Please select a .png or .jpg 256 KB or smaller."));
            iconError->show();
            return;
        }
        const QByteArray bytes = file.readAll();
        const QString mimeType = QMimeDatabase().mimeTypeForData(bytes).name();
        QPixmap preview;
        if ((mimeType != QLatin1String("image/png") && mimeType != QLatin1String("image/jpeg")) || !preview.loadFromData(bytes)) {
            iconError->setText(tr("Please select a .png or .jpg image."));
            iconError->show();
            return;
        }
        iconError->hide();
        iconChanges.insert(roleId, { ImageUpload::dataUri(bytes, mimeType), preview });
        updateEditor();
        updateSaveBar();
    });
}

void GuildRolesPage::removeIcon()
{
    const auto saved = savedRole(selectedRoleId);
    if (!saved)
        return;
    if (hasRoleIcon(*saved))
        iconChanges.insert(selectedRoleId, IconChange());
    else
        iconChanges.remove(selectedRoleId);
    iconError->hide();
    updateEditor();
    updateSaveBar();
}

void GuildRolesPage::fetchMemberCounts()
{
    if (!instance || !canManageRoles() || fetchingMemberCounts)
        return;
    fetchingMemberCounts = true;
    QPointer<GuildRolesPage> self(this);
    instance->discord()->fetchRoleMemberCounts(guildId, [self](const Core::Result<QHash<Core::Snowflake, int>> &result) {
        if (!self)
            return;
        self->fetchingMemberCounts = false;
        if (!result.success() || !self->canManageRoles())
            return;
        self->memberCounts = *result.value;
        self->memberCountsFetchedAt = QDateTime::currentDateTimeUtc();
        self->rebuildList();
        self->updateMembersTabLabel();
        if (self->tabs->currentWidget() == self->membersTab)
            self->rebuildMemberList();
    });
}

void GuildRolesPage::adjustMemberCount(Core::Snowflake roleId, int delta)
{
    const auto count = memberCounts.constFind(roleId);
    if (count == memberCounts.constEnd())
        return;
    memberCounts.insert(roleId, qMax(0, count.value() + delta));
    updateListItem(roleId);
    updateMembersTabLabel();
}

void GuildRolesPage::createRole()
{
    if (!instance || saving)
        return;
    QPointer<GuildRolesPage> self(this);
    instance->discord()->createRole(guildId, [self](const Core::Result<Discord::Role> &result) {
        if (!self)
            return;
        if (!result.success()) {
            self->showActionError(result.error);
            return;
        }
        const Core::Snowflake roleId = result.value->id.get();
        self->memberCounts.insert(roleId, 0);
        self->refreshRoles();
        self->selectRole(roleId);
        self->tabs->setCurrentWidget(self->displayTab);
        self->nameEdit->setFocus();
        self->nameEdit->selectAll();
    });
}

void GuildRolesPage::duplicateRole(Core::Snowflake roleId)
{
    if (!instance || saving)
        return;
    if (hasUnsavedChanges()) {
        warnUnsavedChanges();
        return;
    }
    const auto source = savedRole(roleId);
    if (!source)
        return;

    QSet<QString> names;
    for (const Discord::Role &role : savedRoles)
        names.insert(role.name.get());
    QString name = source->name.get() + tr(" copy");
    while (names.contains(name))
        name += tr(" copy");

    const Discord::Role original = *source;
    QPointer<GuildRolesPage> self(this);
    instance->discord()->createRole(guildId, [self, original, name](const Core::Result<Discord::Role> &created) {
        if (!self || !self->instance)
            return;
        if (!created.success()) {
            self->showActionError(created.error);
            return;
        }
        const Core::Snowflake copyId = created.value->id.get();
        self->memberCounts.insert(copyId, 0);
        Discord::RoleEdit edit = Discord::RoleEdit::fromRole(original);
        edit.name = name;
        self->instance->discord()->modifyRole(self->guildId, copyId, edit, [self, original, copyId](const Core::Result<void> &modified) {
            if (!self || !self->instance)
                return;
            if (!modified.success()) {
                self->showActionError(modified.error);
                return;
            }
            QList<Core::Snowflake> order = self->savedOrder();
            order.removeAll(copyId);
            order.insert(order.indexOf(original.id.get()) + 1, copyId);
            self->instance->discord()->modifyRolePositions(self->guildId, rolePositions(order), [self, copyId](const Core::Result<void> &moved) {
                if (!self)
                    return;
                if (!moved.success())
                    self->showActionError(moved.error);
                self->refreshRoles();
                self->selectRole(copyId);
            });
        });
    });
}

void GuildRolesPage::confirmDeleteRole(Core::Snowflake roleId)
{
    const Discord::Role role = currentRole(roleId);
    auto *confirm = new ConfirmPopup(tr("Delete Role"),
                                     tr("Are you sure you want to delete the <b>%1</b> role? This action cannot be undone.")
                                             .arg(role.name->toHtmlEscaped()),
                                     tr("Delete"), this);
    confirm->setAttribute(Qt::WA_DeleteOnClose);
    connect(confirm, &QDialog::accepted, this, [this, roleId]() {
        if (!instance)
            return;
        QPointer<GuildRolesPage> self(this);
        instance->discord()->deleteRole(guildId, roleId, [self](const Core::Result<void> &result) {
            if (self && !result.success())
                self->showActionError(result.error);
        });
    });
    confirm->open();
}

void GuildRolesPage::showRoleMenu(Core::Snowflake roleId, const QPoint &globalPos)
{
    if (!roleId.isValid() || !savedRole(roleId))
        return;
    const Discord::Role role = currentRole(roleId);
    const bool modifiable = !isEveryone(roleId) && !isLocked(role) && !role.isManaged() && !saving;

    QMenu menu(this);
    if (modifiable)
        menu.addAction(tr("Duplicate Role"), this, [this, roleId]() { duplicateRole(roleId); });
    menu.addAction(tr("Copy Role ID"), this, [roleId]() { QGuiApplication::clipboard()->setText(QString::number(quint64(roleId))); });
    if (modifiable) {
        menu.addSeparator();
        QAction *deleteAction = menu.addAction(tr("Delete"), this, [this, roleId]() { confirmDeleteRole(roleId); });
        deleteAction->setIcon(Core::Theme::Icons::icon(Core::Theme::Icons::Name::Trash, Core::Theme::Token::ChatError));
    }
    menu.exec(globalPos);
}

void GuildRolesPage::loadRoleMembers()
{
    if (!instance || !selectedRoleId.isValid() || isEveryone(selectedRoleId))
        return;

    const Core::Snowflake roleId = selectedRoleId;
    if (!canManageRoles()) {
        RoleMemberList holders;
        for (const Discord::Member &member : instance->users()->getKnownMembers(guildId))
            if (member.user.hasValue() && member.roles.valueOr({}).contains(roleId))
                holders.userIds.append(member.user->id.get());
        roleMembers.insert(roleId, holders);
        rebuildMemberList();
        return;
    }

    rebuildMemberList();
    const auto loaded = roleMembers.constFind(roleId);
    if (loaded != roleMembers.constEnd() && loaded->fetchedAt.isValid() && loaded->fetchedAt.secsTo(QDateTime::currentDateTimeUtc()) < RoleMembersMaxAgeSecs)
        return;

    QPointer<GuildRolesPage> self(this);
    instance->discord()->fetchRoleMemberIds(guildId, roleId, [self, roleId](const Core::Result<QList<Core::Snowflake>> &result) {
        if (!self || !self->instance)
            return;
        if (!result.success()) {
            if (self->selectedRoleId == roleId) {
                self->membersEmpty->setText(result.error);
                self->memberStates->setCurrentWidget(self->membersEmpty);
            }
            return;
        }
        self->roleMembers.insert(roleId, { *result.value, QDateTime::currentDateTimeUtc() });

        QList<Core::Snowflake> missing;
        for (Core::Snowflake userId : *result.value)
            if (!self->instance->users()->getMember(self->guildId, userId) || !self->instance->users()->getUser(userId))
                missing.append(userId);
        if (!missing.isEmpty())
            self->instance->discord()->requestGuildMembers(self->guildId, missing, false);
        if (self->selectedRoleId == roleId)
            self->rebuildMemberList();
    });
}

void GuildRolesPage::rebuildMemberList()
{
    memberList->clear();
    avatarTracker.clear();
    if (!instance || !selectedRoleId.isValid() || isEveryone(selectedRoleId))
        return;

    const Core::Snowflake roleId = selectedRoleId;
    const Discord::Role role = currentRole(roleId);
    const bool changeable = !isLocked(role) && !role.isManaged();
    const QString filter = memberSearch->text().trimmed();

    struct Row
    {
        Core::Snowflake userId;
        QString name;
        Discord::User user;
        bool memberKnown;
    };
    QList<Row> rows;
    const auto loaded = roleMembers.constFind(roleId);
    const QList<Core::Snowflake> ids = loaded != roleMembers.constEnd() ? loaded->userIds : QList<Core::Snowflake>();
    for (Core::Snowflake userId : ids) {
        const auto user = instance->users()->getUser(userId);
        if (!user)
            continue;
        const auto member = instance->users()->getMember(guildId, userId);
        if (!memberMatches(member.value_or(Discord::Member()), *user, filter))
            continue;
        rows.append({ userId, instance->users()->getDisplayName(userId, guildId), *user, member.has_value() });
    }
    std::sort(rows.begin(), rows.end(), [](const Row &a, const Row &b) { return a.name.localeAwareCompare(b.name) < 0; });

    const QIcon removeMemberIcon = Core::Theme::Icons::icon(Core::Theme::Icons::Name::X, Core::Theme::Token::PlaceholderText);
    for (const Row &row : rows) {
        auto *item = new QTreeWidgetItem(memberList);
        item->setData(0, UserIdRole, QVariant::fromValue<quint64>(row.userId));
        item->setText(0, row.name == row.user.username.get() ? row.name : QStringLiteral("%1  (%2)").arg(row.name, row.user.tag()));
        const QUrl avatarUrl = instance->users()->getAvatarUrl(row.user, guildId, 32);
        const QPixmap avatar = avatarTracker.fetch(images, avatarUrl, MemberAvatarSize, row.userId, instance->accountId());
        item->setIcon(0, QIcon(roundedPixmap(avatar, MemberAvatarSize.width(), MemberAvatarSize.width() / 2.0)));

        if (!changeable || !row.memberKnown)
            continue;
        auto *remove = new QToolButton(memberList);
        remove->setAutoRaise(true);
        remove->setIcon(removeMemberIcon);
        remove->setToolTip(tr("Remove member"));
        remove->setEnabled(!saving);
        const Core::Snowflake userId = row.userId;
        connect(remove, &QToolButton::clicked, this, [this, userId]() {
            removeMember(userId, QGuiApplication::keyboardModifiers().testFlag(Qt::ShiftModifier));
        });
        memberList->setItemWidget(item, 1, remove);
    }

    const bool knownOnly = !canManageRoles();
    const auto count = memberCounts.constFind(roleId);
    membersNote->setText(knownOnly ? tr("Only the members Acheron has loaded are shown.")
                                   : tr("Not all members are shown, use Search to find specific members"));
    membersNote->setVisible(knownOnly || (count != memberCounts.constEnd() && count.value() > ids.size()));
    if (!rows.isEmpty()) {
        memberStates->setCurrentWidget(memberList);
        return;
    }
    if (loaded == roleMembers.constEnd())
        membersEmpty->setText(tr("Loading members…"));
    else if (knownOnly)
        membersEmpty->setText(tr("None of the members Acheron has loaded have this role."));
    else
        membersEmpty->setText(tr("No members were found. Add members to this role."));
    memberStates->setCurrentWidget(membersEmpty);
}

void GuildRolesPage::removeMember(Core::Snowflake userId, bool skipConfirm)
{
    if (!instance)
        return;
    const Core::Snowflake roleId = selectedRoleId;
    QPointer<GuildRolesPage> self(this);
    auto remove = [self, userId, roleId]() {
        if (!self || !self->instance)
            return;
        const auto member = self->instance->users()->getMember(self->guildId, userId);
        if (!member)
            return;
        QList<Core::Snowflake> roles = member->roles.valueOr({});
        roles.removeAll(roleId);
        self->instance->discord()->setMemberRoles(self->guildId, userId, roles, {}, { roleId }, [self, userId, roleId](const Core::Result<void> &result) {
            if (!self)
                return;
            if (!result.success()) {
                self->showActionError(result.error);
                return;
            }
            const auto loaded = self->roleMembers.find(roleId);
            if (loaded != self->roleMembers.end())
                loaded->userIds.removeAll(userId);
            if (self->selectedRoleId == roleId)
                self->rebuildMemberList();
        });
    };
    if (skipConfirm) {
        remove();
        return;
    }

    const auto user = instance->users()->getUser(userId);
    const QString username = user ? user->username.get() : QString::number(quint64(userId));
    const QString roleName = savedRole(roleId).value_or(Discord::Role()).name.get();
    auto *confirm = new ConfirmPopup(tr("Remove member"),
                                     tr("Remove <b>%1</b> from role <b>%2</b>?").arg(username.toHtmlEscaped(), roleName.toHtmlEscaped()) +
                                             QStringLiteral("<br><br>") + tr("Hold shift when removing members to skip this modal."),
                                     tr("Remove"), this);
    confirm->setAttribute(Qt::WA_DeleteOnClose);
    connect(confirm, &QDialog::accepted, this, remove);
    confirm->open();
}

void GuildRolesPage::openAddMembersDialog()
{
    if (!instance || !selectedRoleId.isValid())
        return;
    const Core::Snowflake roleId = selectedRoleId;
    QPointer<GuildRolesPage> self(this);
    auto *dialog = new AddRoleMembersDialog(
            instance, guildId, roleId, currentRole(roleId).name.get(),
            [self, roleId](const QList<Core::Snowflake> &added) {
                if (!self)
                    return;
                const auto loaded = self->roleMembers.find(roleId);
                if (loaded != self->roleMembers.end())
                    for (Core::Snowflake userId : added)
                        if (!loaded->userIds.contains(userId))
                            loaded->userIds.append(userId);
                if (self->selectedRoleId == roleId)
                    self->rebuildMemberList();
            },
            this);
    dialog->open();
}

void GuildRolesPage::onMembersUpdated(Core::Snowflake changedGuildId, const QList<Core::Snowflake> &userIds)
{
    if (changedGuildId != guildId || !instance)
        return;
    if (userIds.contains(selfId()) && !saving)
        refreshTimer->start();

    for (Core::Snowflake userId : userIds) {
        const auto member = instance->users()->getMember(guildId, userId);
        if (!member || !member->roles.hasValue())
            continue;
        for (auto loaded = roleMembers.begin(); loaded != roleMembers.end(); ++loaded) {
            const bool holds = member->roles->contains(loaded.key());
            const bool listed = loaded->userIds.contains(userId);
            if (holds && !listed)
                loaded->userIds.append(userId);
            else if (!holds && listed)
                loaded->userIds.removeAll(userId);
        }
    }
    if (tabs->currentWidget() == membersTab)
        memberRebuildTimer->start();
}

void GuildRolesPage::onMemberRemoved(Core::Snowflake changedGuildId, Core::Snowflake userId)
{
    if (changedGuildId != guildId)
        return;
    for (RoleMemberList &loaded : roleMembers)
        loaded.userIds.removeAll(userId);
    if (tabs->currentWidget() == membersTab)
        memberRebuildTimer->start();
}

void GuildRolesPage::onImageFetched(const QUrl &url, const QSize &size, const QPixmap &pixmap)
{
    if (size == IconPreviewSize && url == roleIconUrl && !roleIconUrl.isEmpty()) {
        iconPreview->setPixmap(pixmap);
        return;
    }
    if (size != MemberAvatarSize)
        return;
    const QIcon icon(roundedPixmap(pixmap, MemberAvatarSize.width(), MemberAvatarSize.width() / 2.0));
    avatarTracker.notify(url, [this, &icon](Core::Snowflake userId) {
        for (int row = 0; row < memberList->topLevelItemCount(); row++) {
            QTreeWidgetItem *item = memberList->topLevelItem(row);
            if (Core::Snowflake(item->data(0, UserIdRole).toULongLong()) == userId)
                item->setIcon(0, icon);
        }
    });
}

void GuildRolesPage::updateSaveBar()
{
    if (!saving)
        saveBar->setVisible(hasUnsavedChanges());
}

void GuildRolesPage::resetChanges()
{
    drafts.clear();
    iconChanges.clear();
    draftOrder = savedOrder();
    iconError->hide();
    rebuildList();
    updateEditor();
    updateSaveBar();
}

void GuildRolesPage::save()
{
    if (!instance || saving)
        return;

    QList<Core::Snowflake> queue = dirtyRoles();
    std::stable_sort(queue.begin(), queue.end(), [this](Core::Snowflake a, Core::Snowflake b) {
        return currentRole(a).name->trimmed().isEmpty() && !currentRole(b).name->trimmed().isEmpty();
    });

    saving = true;
    saveBar->setSaving(true);
    rebuildList();
    updateEditor();

    if (!orderDirty()) {
        saveNextRole(queue);
        return;
    }
    QPointer<GuildRolesPage> self(this);
    instance->discord()->modifyRolePositions(guildId, changedPositions(draftOrder), [self, queue](const Core::Result<void> &result) {
        if (!self)
            return;
        if (!result.success()) {
            self->finishSave(result.error);
            return;
        }
        self->saveNextRole(queue);
    });
}

void GuildRolesPage::saveNextRole(QList<Core::Snowflake> queue)
{
    if (queue.isEmpty() || !instance) {
        finishSave(QString());
        return;
    }
    const Core::Snowflake roleId = queue.takeFirst();
    QPointer<GuildRolesPage> self(this);
    instance->discord()->modifyRole(guildId, roleId, editFor(roleId), [self, roleId, queue](const Core::Result<void> &result) {
        if (!self)
            return;
        if (!result.success()) {
            self->finishSave(result.error);
            return;
        }
        self->drafts.remove(roleId);
        self->iconChanges.remove(roleId);
        self->saveNextRole(queue);
    });
}

void GuildRolesPage::finishSave(const QString &error)
{
    saving = false;
    if (error.isEmpty()) {
        saveBar->setSaving(false);
    } else {
        saveBar->showError(error);
        saveBar->flash();
    }
    refreshRoles();
}

} // namespace UI
} // namespace Acheron
