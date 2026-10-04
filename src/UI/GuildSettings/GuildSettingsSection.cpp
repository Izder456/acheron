#include "GuildSettingsSection.hpp"

#include <QCoreApplication>
#include <QMenu>
#include <QMouseEvent>

#include "Core/ClientInstance.hpp"

namespace Acheron {
namespace UI {
namespace GuildSettingsAccess {

using Discord::Permission;

namespace {

QString tr(const char *text)
{
    return QCoreApplication::translate("GuildSettings", text);
}

bool hasAny(Core::ClientInstance *instance, Core::Snowflake guildId, Discord::Permissions permissions)
{
    const auto granted = instance->permissions()->getGuildPermissions(instance->accountId(), guildId);
    return (granted & permissions) != Discord::NO_PERMISSIONS;
}

class SubmenuEntryClick : public QObject
{
public:
    SubmenuEntryClick(QMenu *parentMenu, QMenu *submenu) : QObject(submenu), parentMenu(parentMenu), submenu(submenu) { parentMenu->installEventFilter(this); }

protected:
    bool eventFilter(QObject *watched, QEvent *event) override
    {
        if (watched != parentMenu || event->type() != QEvent::MouseButtonRelease)
            return false;
        const auto *mouseEvent = static_cast<QMouseEvent *>(event);
        if (mouseEvent->button() != Qt::LeftButton || parentMenu->actionAt(mouseEvent->pos()) != submenu->menuAction())
            return false;
        const QList<QAction *> entries = submenu->actions();
        if (entries.isEmpty())
            return false;
        QAction *first = entries.first();
        parentMenu->close();
        first->trigger();
        return true;
    }

private:
    QMenu *parentMenu;
    QMenu *submenu;
};

} // namespace

bool canOpen(Core::ClientInstance *instance, Core::Snowflake guildId)
{
    return instance && instance->getGuild(guildId).has_value();
}

QList<GuildSettingsSection> visibleSections(Core::ClientInstance *instance, Core::Snowflake guildId)
{
    const auto guild = instance ? instance->getGuild(guildId) : std::nullopt;
    if (!guild)
        return {};
    auto has = [&](Discord::Permissions permissions) { return hasAny(instance, guildId, permissions); };

    QList<GuildSettingsSection> sections{ GuildSettingsSection::Profile, GuildSettingsSection::Engagement,
                                          GuildSettingsSection::BoostPerks, GuildSettingsSection::Emoji,
                                          GuildSettingsSection::Stickers, GuildSettingsSection::Members,
                                          GuildSettingsSection::Roles };

    if (has(Permission::MANAGE_GUILD | Permission::CREATE_INSTANT_INVITE))
        sections << GuildSettingsSection::Invites;
    if (has(Permission::VIEW_AUDIT_LOG))
        sections << GuildSettingsSection::AuditLog;
    if (has(Permission::BAN_MEMBERS | Permission::VIEW_AUDIT_LOG))
        sections << GuildSettingsSection::Bans;
    if (guild->ownerId.get() == instance->accountId())
        sections << GuildSettingsSection::DeleteServer;
    return sections;
}

bool selfHasMfa(Core::ClientInstance *instance)
{
    return instance && instance->discord()->getMe().mfaEnabled.valueOr(false);
}

bool canEdit(Core::ClientInstance *instance, Core::Snowflake guildId, GuildSettingsSection section)
{
    if (!instance)
        return false;
    auto has = [&](Discord::Permissions permissions) { return hasAny(instance, guildId, permissions); };

    switch (section) {
    case GuildSettingsSection::Profile:
    case GuildSettingsSection::Engagement:
    case GuildSettingsSection::BoostPerks:
        return has(Permission::MANAGE_GUILD);
    case GuildSettingsSection::Invites:
        return has(Permission::MANAGE_GUILD | Permission::CREATE_INSTANT_INVITE);
    case GuildSettingsSection::Emoji:
    case GuildSettingsSection::Stickers:
        return has(Permission::MANAGE_EXPRESSIONS | Permission::CREATE_EXPRESSIONS);
    case GuildSettingsSection::Members:
        return has(Permission::ADMINISTRATOR |
                   Permission::MANAGE_GUILD |
                   Permission::BAN_MEMBERS |
                   Permission::KICK_MEMBERS |
                   Permission::MODERATE_MEMBERS |
                   Permission::MANAGE_ROLES |
                   Permission::MANAGE_NICKNAMES);
    case GuildSettingsSection::Roles:
        return has(Permission::MANAGE_ROLES);
    case GuildSettingsSection::AuditLog:
        return has(Permission::VIEW_AUDIT_LOG);
    case GuildSettingsSection::Bans:
        return has(Permission::BAN_MEMBERS);
    case GuildSettingsSection::DeleteServer: {
        const auto guild = instance->getGuild(guildId);
        return guild &&
               guild->ownerId.get() == instance->accountId() &&
               (selfHasMfa(instance) || guild->mfaLevel.valueOr(Discord::MfaLevel::NONE) != Discord::MfaLevel::ELEVATED);
    }
    }
    return false;
}

QString title(GuildSettingsSection section)
{
    switch (section) {
    case GuildSettingsSection::Profile:
        return tr("Server Profile");
    case GuildSettingsSection::Engagement:
        return tr("Engagement");
    case GuildSettingsSection::BoostPerks:
        return tr("Boost Perks");
    case GuildSettingsSection::Emoji:
        return tr("Emoji");
    case GuildSettingsSection::Stickers:
        return tr("Stickers");
    case GuildSettingsSection::Members:
        return tr("Members");
    case GuildSettingsSection::Roles:
        return tr("Roles");
    case GuildSettingsSection::Invites:
        return tr("Invites");
    case GuildSettingsSection::AuditLog:
        return tr("Audit Log");
    case GuildSettingsSection::Bans:
        return tr("Bans");
    case GuildSettingsSection::DeleteServer:
        return tr("Delete Server");
    }
    return {};
}

GuildSettingsGroup group(GuildSettingsSection section)
{
    switch (section) {
    case GuildSettingsSection::Profile:
    case GuildSettingsSection::Engagement:
    case GuildSettingsSection::BoostPerks:
        return GuildSettingsGroup::Server;
    case GuildSettingsSection::Emoji:
    case GuildSettingsSection::Stickers:
        return GuildSettingsGroup::Expression;
    case GuildSettingsSection::Members:
    case GuildSettingsSection::Roles:
    case GuildSettingsSection::Invites:
        return GuildSettingsGroup::People;
    case GuildSettingsSection::AuditLog:
    case GuildSettingsSection::Bans:
        return GuildSettingsGroup::Moderation;
    case GuildSettingsSection::DeleteServer:
        return GuildSettingsGroup::Danger;
    }
    return GuildSettingsGroup::Server;
}

QString groupTitle(GuildSettingsGroup group)
{
    switch (group) {
    case GuildSettingsGroup::Expression:
        return tr("Expression");
    case GuildSettingsGroup::People:
        return tr("People");
    case GuildSettingsGroup::Moderation:
        return tr("Moderation");
    case GuildSettingsGroup::Server:
    case GuildSettingsGroup::Danger:
        return {};
    }
    return {};
}

void addMenu(QMenu *menu, const SectionsProvider &provider, Core::Snowflake accountId, Core::Snowflake guildId,
             const std::function<void(GuildSettingsSection)> &open)
{
    const QList<GuildSettingsSection> sections = provider ? provider(accountId, guildId) : QList<GuildSettingsSection>();
    if (sections.isEmpty())
        return;

    menu->addSeparator();
    QMenu *settings = menu->addMenu(tr("Server Settings"));
    for (GuildSettingsSection section : sections)
        if (section != GuildSettingsSection::DeleteServer)
            settings->addAction(title(section), settings, [open, section]() { open(section); });
    new SubmenuEntryClick(menu, settings);
}

} // namespace GuildSettingsAccess
} // namespace UI
} // namespace Acheron
