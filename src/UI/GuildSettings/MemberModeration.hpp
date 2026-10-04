#pragma once

#include <QDateTime>
#include <QList>
#include <QString>

#include <optional>

#include "Core/Snowflake.hpp"

class QMenu;
class QWidget;

namespace Acheron {
namespace Core {
class ClientInstance;
} // namespace Core
namespace UI {

struct MemberModerationTarget
{
    Core::Snowflake guildId;
    Core::Snowflake userId;
    QString username;
    QList<Core::Snowflake> roleIds;
    bool bot = false;
    QDateTime timeoutUntil;

    [[nodiscard]] bool isTimedOut() const { return timeoutUntil.isValid() && timeoutUntil > QDateTime::currentDateTimeUtc(); }
};

namespace MemberModeration {

[[nodiscard]] std::optional<MemberModerationTarget> targetFor(Core::ClientInstance *instance, Core::Snowflake guildId, Core::Snowflake userId);

[[nodiscard]] bool canKick(Core::ClientInstance *instance, const MemberModerationTarget &target);
[[nodiscard]] bool canBan(Core::ClientInstance *instance, const MemberModerationTarget &target);
[[nodiscard]] bool canTimeout(Core::ClientInstance *instance, const MemberModerationTarget &target);
[[nodiscard]] bool canChangeNickname(Core::ClientInstance *instance, const MemberModerationTarget &target);
[[nodiscard]] bool canEditRoles(Core::ClientInstance *instance, const MemberModerationTarget &target);

[[nodiscard]] QString timeoutRemaining(const QDateTime &until);

void kick(QWidget *parent, Core::ClientInstance *instance, const MemberModerationTarget &target);
void ban(QWidget *parent, Core::ClientInstance *instance, const MemberModerationTarget &target);
void timeout(QWidget *parent, Core::ClientInstance *instance, const MemberModerationTarget &target);
void removeTimeout(QWidget *parent, Core::ClientInstance *instance, const MemberModerationTarget &target);
void changeNickname(QWidget *parent, Core::ClientInstance *instance, const MemberModerationTarget &target);

void addRolesMenu(QMenu *menu, QWidget *dialogParent, Core::ClientInstance *instance, const MemberModerationTarget &target);
void addModerationActions(QMenu *menu, QWidget *dialogParent, Core::ClientInstance *instance, const MemberModerationTarget &target);

} // namespace MemberModeration
} // namespace UI
} // namespace Acheron
