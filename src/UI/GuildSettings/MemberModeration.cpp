#include "MemberModeration.hpp"

#include <QButtonGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QPlainTextEdit>
#include <QPointer>
#include <QPushButton>
#include <QRadioButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidgetAction>

#include <memory>

#include "Core/ClientInstance.hpp"
#include "Core/PermissionComputer.hpp"
#include "Core/RoleHierarchy.hpp"
#include "Core/Theme/Manager.hpp"
#include "UI/Dialogs/BasePopup.hpp"

#include "GuildSettingsPage.hpp"

namespace Acheron {
namespace UI {
namespace MemberModeration {

using Discord::Permission;

namespace {

constexpr int ReasonMaxLength = 512;
constexpr int NicknameMaxLength = 32;
constexpr int RolesListMaxHeight = 360;
constexpr int RolesFilterThreshold = 10;

QString tr(const char *text)
{
    return QCoreApplication::translate("MemberModeration", text);
}

bool canManageUser(Core::ClientInstance *instance, const MemberModerationTarget &target, Discord::Permissions permission)
{
    if (!instance || target.userId == instance->accountId())
        return false;
    if (!instance->permissions()->hasGuildPermission(instance->accountId(), target.guildId, permission))
        return false;
    const auto hierarchy = instance->selfRoleHierarchy(target.guildId);
    return hierarchy && hierarchy->outranksMember(target.userId, target.roleIds);
}

bool isAdministrator(Core::ClientInstance *instance, const MemberModerationTarget &target)
{
    const auto guild = instance->getGuild(target.guildId);
    if (!guild)
        return false;
    const auto permissions = Core::PermissionComputer::computeBasePermissions(
            guild->ownerId.get(), target.userId, target.guildId, target.roleIds, instance->getRolesForGuild(target.guildId));
    return permissions.testFlag(Permission::ADMINISTRATOR);
}

void reportFailure(const QPointer<QWidget> &parent, const Core::Result<void> &result)
{
    if (!result.success() && parent)
        GuildSettingsPage::showError(parent, result.error);
}

class ModerationPopup : public BasePopup
{
public:
    ModerationPopup(const QString &title, const QString &confirmLabel, bool destructive, QWidget *parent)
        : BasePopup(parent)
    {
        setAttribute(Qt::WA_DeleteOnClose);

        auto *layout = new QVBoxLayout(getContainer());
        layout->setSpacing(10);
        layout->setContentsMargins(24, 24, 24, 24);

        auto *titleLabel = new QLabel(title, getContainer());
        QFont titleFont = titleLabel->font();
        titleFont.setBold(true);
        titleFont.setPointSize(titleFont.pointSize() + 2);
        titleLabel->setFont(titleFont);
        titleLabel->setWordWrap(true);
        layout->addWidget(titleLabel);

        body = new QVBoxLayout();
        body->setSpacing(6);
        layout->addLayout(body);

        auto *buttons = new QDialogButtonBox(getContainer());
        buttons->addButton(QDialogButtonBox::Cancel);
        QPushButton *confirm = buttons->addButton(confirmLabel, QDialogButtonBox::AcceptRole);
        confirm->setDefault(true);
        if (destructive) {
            QPalette palette = confirm->palette();
            palette.setColor(QPalette::ButtonText, Core::Theme::Manager::instance().color(Core::Theme::Token::ChatError));
            confirm->setPalette(palette);
        }
        connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
        connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
        layout->addWidget(buttons);
    }

    QWidget *content() const { return getContainer(); }

    QLabel *addText(const QString &text)
    {
        auto *label = new QLabel(text, getContainer());
        label->setWordWrap(true);
        body->addWidget(label);
        return label;
    }

    void addFieldLabel(const QString &text)
    {
        body->addSpacing(6);
        QLabel *label = addText(text);
        QFont font = label->font();
        font.setBold(true);
        label->setFont(font);
    }

    QPlainTextEdit *addTextArea(int lines, const QString &placeholder = {})
    {
        auto *edit = new QPlainTextEdit(getContainer());
        edit->setPlaceholderText(placeholder);
        edit->setFixedHeight(edit->fontMetrics().lineSpacing() * lines + 12);
        QObject::connect(edit, &QPlainTextEdit::textChanged, edit, [edit]() {
            if (edit->toPlainText().size() <= ReasonMaxLength)
                return;
            QSignalBlocker blocker(edit);
            edit->setPlainText(edit->toPlainText().left(ReasonMaxLength));
            edit->moveCursor(QTextCursor::End);
        });
        body->addWidget(edit);
        return edit;
    }

    void addWidget(QWidget *widget) { body->addWidget(widget); }

private:
    QVBoxLayout *body;
};

struct Choice
{
    int seconds;
    const char *label;
};

constexpr Choice DeleteHistoryChoices[] = {
    { 0, QT_TRANSLATE_NOOP("MemberModeration", "Don't Delete Any") },
    { 3600, QT_TRANSLATE_NOOP("MemberModeration", "Previous Hour") },
    { 21600, QT_TRANSLATE_NOOP("MemberModeration", "Previous 6 Hours") },
    { 43200, QT_TRANSLATE_NOOP("MemberModeration", "Previous 12 Hours") },
    { 86400, QT_TRANSLATE_NOOP("MemberModeration", "Previous 24 Hours") },
    { 259200, QT_TRANSLATE_NOOP("MemberModeration", "Previous 3 Days") },
    { 604800, QT_TRANSLATE_NOOP("MemberModeration", "Previous 7 Days") },
};
constexpr int DefaultDeleteHistorySeconds = 3600;

constexpr Choice TimeoutChoices[] = {
    { 60, QT_TRANSLATE_NOOP("MemberModeration", "60 secs") },
    { 300, QT_TRANSLATE_NOOP("MemberModeration", "5 mins") },
    { 600, QT_TRANSLATE_NOOP("MemberModeration", "10 mins") },
    { 3600, QT_TRANSLATE_NOOP("MemberModeration", "1 hour") },
    { 86400, QT_TRANSLATE_NOOP("MemberModeration", "1 day") },
    { 604800, QT_TRANSLATE_NOOP("MemberModeration", "1 week") },
};
constexpr int DefaultTimeoutSeconds = 60;

const char *const BanReasons[] = {
    QT_TRANSLATE_NOOP("MemberModeration", "Suspicious or spam account"),
    QT_TRANSLATE_NOOP("MemberModeration", "Compromised or hacked account"),
    QT_TRANSLATE_NOOP("MemberModeration", "Breaking server rules"),
};

using DialogOpener = void (*)(QWidget *, Core::ClientInstance *, const MemberModerationTarget &);

} // namespace

std::optional<MemberModerationTarget> targetFor(Core::ClientInstance *instance, Core::Snowflake guildId, Core::Snowflake userId)
{
    if (!instance || !guildId.isValid())
        return std::nullopt;
    const auto member = instance->users()->getMember(guildId, userId);
    if (!member)
        return std::nullopt;

    MemberModerationTarget target;
    target.guildId = guildId;
    target.userId = userId;
    target.roleIds = member->roles.valueOr({});
    if (member->communicationDisabledUntil.hasValue())
        target.timeoutUntil = member->communicationDisabledUntil.get();
    const auto user = member->user.hasValue() ? std::optional<Discord::User>(member->user.get()) : instance->users()->getUser(userId);
    if (user) {
        target.username = user->username.get();
        target.bot = user->bot.valueOr(false);
    }
    return target;
}

bool canKick(Core::ClientInstance *instance, const MemberModerationTarget &target)
{
    return canManageUser(instance, target, Permission::KICK_MEMBERS);
}

bool canBan(Core::ClientInstance *instance, const MemberModerationTarget &target)
{
    return !target.bot && canManageUser(instance, target, Permission::BAN_MEMBERS);
}

bool canTimeout(Core::ClientInstance *instance, const MemberModerationTarget &target)
{
    return canManageUser(instance, target, Permission::MODERATE_MEMBERS) && !isAdministrator(instance, target);
}

bool canChangeNickname(Core::ClientInstance *instance, const MemberModerationTarget &target)
{
    return canManageUser(instance, target, Permission::MANAGE_NICKNAMES);
}

bool canEditRoles(Core::ClientInstance *instance, const MemberModerationTarget &target)
{
    return instance && instance->permissions()->hasGuildPermission(instance->accountId(), target.guildId, Permission::MANAGE_ROLES);
}

QString timeoutRemaining(const QDateTime &until)
{
    const qint64 remaining = qMax<qint64>(1, QDateTime::currentDateTimeUtc().secsTo(until));
    const QString clock = tr("%1h %2m %3s")
                                  .arg((remaining % 86400) / 3600, 2, 10, QLatin1Char('0'))
                                  .arg((remaining % 3600) / 60, 2, 10, QLatin1Char('0'))
                                  .arg(remaining % 60, 2, 10, QLatin1Char('0'));
    const qint64 days = remaining / 86400;
    return days > 0 ? tr("%1d").arg(days, 2, 10, QLatin1Char('0')) + QLatin1Char(' ') + clock : clock;
}

void kick(QWidget *parent, Core::ClientInstance *instance, const MemberModerationTarget &target)
{
    auto *popup = new ModerationPopup(tr("Kick %1 from Server").arg(target.username), tr("Kick"), true, parent);
    popup->addText(tr("Are you sure you want to kick %1 from the server? They will be able to rejoin again with a new invite.").arg(target.username));
    popup->addFieldLabel(tr("Reason for Kick"));
    QPlainTextEdit *reason = popup->addTextArea(4);

    QPointer<Core::ClientInstance> instanceGuard(instance);
    QPointer<QWidget> parentGuard(parent);
    QObject::connect(popup, &QDialog::accepted, popup, [instanceGuard, parentGuard, target, reason]() {
        if (!instanceGuard)
            return;
        instanceGuard->discord()->kickMember(target.guildId, target.userId, reason->toPlainText(), [parentGuard](const Core::Result<void> &result) {
            reportFailure(parentGuard, result);
        });
    });
    popup->open();
}

void ban(QWidget *parent, Core::ClientInstance *instance, const MemberModerationTarget &target)
{
    auto *popup = new ModerationPopup(tr("Ban %1?").arg(target.username), tr("Ban"), true, parent);

    popup->addFieldLabel(tr("Reason for Ban"));
    auto *reasons = new QButtonGroup(popup);
    for (const char *reason : BanReasons) {
        auto *option = new QRadioButton(tr(reason), popup->content());
        reasons->addButton(option);
        popup->addWidget(option);
    }
    auto *other = new QRadioButton(tr("Other"), popup->content());
    reasons->addButton(other);
    popup->addWidget(other);
    QPlainTextEdit *otherText = popup->addTextArea(4);
    otherText->hide();
    QObject::connect(other, &QRadioButton::toggled, otherText, &QWidget::setVisible);

    popup->addFieldLabel(tr("Delete Message History"));
    auto *history = new QComboBox(popup->content());
    for (const Choice &choice : DeleteHistoryChoices)
        history->addItem(tr(choice.label), choice.seconds);
    history->setCurrentIndex(history->findData(DefaultDeleteHistorySeconds));
    popup->addWidget(history);

    QPointer<Core::ClientInstance> instanceGuard(instance);
    QPointer<QWidget> parentGuard(parent);
    QObject::connect(popup, &QDialog::accepted, popup, [instanceGuard, parentGuard, target, reasons, other, otherText, history]() {
        if (!instanceGuard)
            return;

        QString reason;
        if (reasons->checkedButton() == other)
            reason = otherText->toPlainText().trimmed().isEmpty() ? QStringLiteral("other") : otherText->toPlainText();
        else if (reasons->checkedButton())
            reason = reasons->checkedButton()->text();
        instanceGuard->discord()->banMember(target.guildId, target.userId,
                                            history->currentData().toInt(), reason,
                                            [parentGuard](const Core::Result<void> &result) {
                                                reportFailure(parentGuard, result);
                                            });
    });
    popup->open();
}

void timeout(QWidget *parent, Core::ClientInstance *instance, const MemberModerationTarget &target)
{
    auto *popup = new ModerationPopup(tr("Timeout %1").arg(target.username), tr("Timeout"), true, parent);
    popup->addText(tr("Members who are in timeout are temporarily not allowed to chat or react in text channels. They "
                      "are also not allowed to connect to voice or Stage channels."));

    popup->addFieldLabel(tr("Duration"));
    auto *durationRow = new QWidget(popup->content());
    auto *durationLayout = new QHBoxLayout(durationRow);
    durationLayout->setContentsMargins(0, 0, 0, 0);
    auto *durations = new QButtonGroup(popup);
    for (const Choice &choice : TimeoutChoices) {
        auto *button = new QPushButton(tr(choice.label), durationRow);
        button->setCheckable(true);
        button->setAutoDefault(false);
        button->setChecked(choice.seconds == DefaultTimeoutSeconds);
        durations->addButton(button, choice.seconds);
        durationLayout->addWidget(button);
    }
    popup->addWidget(durationRow);

    popup->addFieldLabel(tr("Reason"));
    QPlainTextEdit *reason = popup->addTextArea(3, tr("Enter a reason. This will only be visible in the Audit Log and will not be shown to the member."));

    QPointer<Core::ClientInstance> instanceGuard(instance);
    QPointer<QWidget> parentGuard(parent);
    QObject::connect(popup, &QDialog::accepted, popup, [instanceGuard, parentGuard, target, durations, reason]() {
        if (!instanceGuard)
            return;
        const QDateTime until = QDateTime::currentDateTimeUtc().addSecs(durations->checkedId());
        instanceGuard->discord()->setMemberTimeout(target.guildId, target.userId, until, reason->toPlainText(),
                                                   [parentGuard](const Core::Result<void> &result) {
                                                       reportFailure(parentGuard, result);
                                                   });
    });
    popup->open();
}

void removeTimeout(QWidget *parent, Core::ClientInstance *instance, const MemberModerationTarget &target)
{
    if (!target.isTimedOut())
        return;

    auto *popup = new ModerationPopup(tr("Remove Timeout"), tr("Remove Timeout"), true, parent);
    QLabel *remaining = popup->addText(QString());
    popup->addText(tr("Remove it now to let them post and react to messages, and join voice and stage channels."));

    auto refresh = [popup, remaining, target]() {
        if (!target.isTimedOut()) {
            popup->reject();
            return;
        }
        remaining->setText(tr("%1 has %2 remaining in timeout.").arg(target.username, timeoutRemaining(target.timeoutUntil)));
    };
    refresh();
    auto *ticker = new QTimer(popup);
    QObject::connect(ticker, &QTimer::timeout, popup, refresh);
    ticker->start(1000);

    QPointer<Core::ClientInstance> instanceGuard(instance);
    QPointer<QWidget> parentGuard(parent);
    QObject::connect(popup, &QDialog::accepted, popup, [instanceGuard, parentGuard, target]() {
        if (!instanceGuard)
            return;
        instanceGuard->discord()->setMemberTimeout(target.guildId, target.userId, QDateTime(), std::nullopt, [parentGuard](const Core::Result<void> &result) {
            reportFailure(parentGuard, result);
        });
    });
    popup->open();
}

void changeNickname(QWidget *parent, Core::ClientInstance *instance, const MemberModerationTarget &target)
{
    if (!instance)
        return;
    const auto member = instance->users()->getMember(target.guildId, target.userId);
    const QString current = member ? member->nick.valueOr(QString()) : QString();

    auto *popup = new ModerationPopup(tr("Change Nickname"), tr("Save"), false, parent);
    popup->addText(tr("Nicknames are visible to everyone on this server. Do not change them unless you are enforcing a "
                      "naming system or clearing a bad nickname."));
    popup->addFieldLabel(tr("Nickname"));
    auto *edit = new QLineEdit(current, popup->content());
    edit->setMaxLength(NicknameMaxLength);
    edit->setPlaceholderText(target.username);
    popup->addWidget(edit);
    auto *reset = new QPushButton(tr("Reset Nickname"), popup->content());
    reset->setFlat(true);
    reset->setAutoDefault(false);
    reset->setCursor(Qt::PointingHandCursor);
    QObject::connect(reset, &QPushButton::clicked, edit, &QLineEdit::clear);
    popup->addWidget(reset);

    QPointer<Core::ClientInstance> instanceGuard(instance);
    QPointer<QWidget> parentGuard(parent);
    QObject::connect(popup, &QDialog::accepted, popup, [instanceGuard, parentGuard, target, edit, current]() {
        const QString nick = edit->text().trimmed();
        if (!instanceGuard || nick == current)
            return;
        instanceGuard->discord()->setMemberNickname(target.guildId, target.userId, nick, [parentGuard](const Core::Result<void> &result) {
            reportFailure(parentGuard, result);
        });
    });
    popup->open();
}

void addRolesMenu(QMenu *menu, QWidget *dialogParent, Core::ClientInstance *instance, const MemberModerationTarget &target)
{
    if (!canEditRoles(instance, target))
        return;
    const auto hierarchy = instance->selfRoleHierarchy(target.guildId);
    if (!hierarchy)
        return;

    struct ShownRole
    {
        Discord::Role role;
        bool assignable;
    };
    QList<ShownRole> shown;
    for (const Discord::Role &role : hierarchy->sortedRoles()) {
        const bool assignable = !role.isManaged() && hierarchy->outranksRole(role);
        if (role.id.get() != target.guildId && (assignable || target.roleIds.contains(role.id.get())))
            shown.append({ role, assignable });
    }

    QMenu *rolesMenu = menu->addMenu(tr("Roles"));
    if (shown.isEmpty()) {
        rolesMenu->addAction(tr("No Roles"))->setEnabled(false);
        return;
    }

    auto *picker = new QWidget();
    auto *pickerLayout = new QVBoxLayout(picker);
    pickerLayout->setContentsMargins(6, 6, 6, 6);
    pickerLayout->setSpacing(6);
    auto *filter = new QLineEdit(picker);
    filter->setPlaceholderText(tr("Search roles"));
    filter->setClearButtonEnabled(true);
    filter->setVisible(shown.size() > RolesFilterThreshold);
    pickerLayout->addWidget(filter);

    auto *scroll = new QScrollArea(picker);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    auto *content = new QWidget(scroll);
    auto *rows = new QVBoxLayout(content);
    rows->setContentsMargins(0, 0, 0, 0);
    rows->setSpacing(2);

    auto roleIds = std::make_shared<QList<Core::Snowflake>>(target.roleIds);
    QPointer<Core::ClientInstance> instanceGuard(instance);
    QPointer<QWidget> parentGuard(dialogParent);

    struct RoleCheck
    {
        QCheckBox *box;
        QString roleName;
    };
    QList<RoleCheck> checks;
    for (const auto &[role, assignable] : shown) {
        const Core::Snowflake roleId = role.id.get();
        const QString name = role.name.get();
        auto *check = new QCheckBox(QString(name).replace(QLatin1Char('&'), QLatin1String("&&")), content);
        check->setChecked(target.roleIds.contains(roleId));
        check->setEnabled(assignable);
        if (role.hasColor())
            check->setIcon(GuildSettingsPage::colorDot(role.getColor()));
        rows->addWidget(check);
        checks.append({ check, name });
        QObject::connect(check, &QCheckBox::toggled, check, [instanceGuard, parentGuard, target, roleIds, roleId, check, content](bool checked) {
            if (!instanceGuard)
                return;
            QList<Core::Snowflake> next = *roleIds;
            next.removeAll(roleId);
            if (checked)
                next.append(roleId);
            const QList<Core::Snowflake> toggled{ roleId };
            const QList<Core::Snowflake> none;
            content->setEnabled(false);
            QPointer<QCheckBox> checkGuard(check);
            QPointer<QWidget> contentGuard(content);
            instanceGuard->discord()->setMemberRoles(
                    target.guildId, target.userId, next, checked ? toggled : none, checked ? none : toggled,
                    [parentGuard, roleIds, next, checked, checkGuard, contentGuard](const Core::Result<void> &result) {
                        if (contentGuard)
                            contentGuard->setEnabled(true);
                        if (result.success()) {
                            *roleIds = next;
                            return;
                        }
                        if (checkGuard) {
                            QSignalBlocker blocker(checkGuard.data());
                            checkGuard->setChecked(!checked);
                        }
                        reportFailure(parentGuard, result);
                    });
        });
    }
    rows->addStretch();
    scroll->setWidget(content);
    content->setAutoFillBackground(false);
    scroll->viewport()->setAutoFillBackground(false);
    scroll->setFixedHeight(qMin(content->sizeHint().height(), RolesListMaxHeight));
    scroll->setMinimumWidth(content->sizeHint().width() + scroll->verticalScrollBar()->sizeHint().width());
    pickerLayout->addWidget(scroll);

    QObject::connect(filter, &QLineEdit::textChanged, picker, [checks](const QString &text) {
        for (const RoleCheck &check : checks)
            check.box->setVisible(check.roleName.contains(text.trimmed(), Qt::CaseInsensitive));
    });
    if (!filter->isHidden())
        QObject::connect(rolesMenu, &QMenu::aboutToShow, filter, [filter]() { QTimer::singleShot(0, filter, [filter]() { filter->setFocus(); }); });

    auto *action = new QWidgetAction(rolesMenu);
    action->setDefaultWidget(picker);
    rolesMenu->addAction(action);
}

void addModerationActions(QMenu *menu, QWidget *dialogParent, Core::ClientInstance *instance, const MemberModerationTarget &target)
{
    if (!instance)
        return;

    QPointer<Core::ClientInstance> instanceGuard(instance);
    QPointer<QWidget> parentGuard(dialogParent);
    auto addDialogAction = [&](const QString &text, DialogOpener openDialog) {
        menu->addAction(text, menu, [instanceGuard, parentGuard, target, openDialog]() {
            if (instanceGuard && parentGuard)
                openDialog(parentGuard, instanceGuard, target);
        });
    };

    if (canChangeNickname(instance, target))
        addDialogAction(tr("Change Nickname"), &changeNickname);
    addRolesMenu(menu, dialogParent, instance, target);

    const bool timeoutable = canTimeout(instance, target);
    const bool kickable = canKick(instance, target);
    const bool bannable = canBan(instance, target);
    if (!timeoutable && !kickable && !bannable)
        return;

    menu->addSeparator();
    if (timeoutable) {
        if (target.isTimedOut())
            addDialogAction(tr("Remove Timeout From %1").arg(target.username), &removeTimeout);
        else
            addDialogAction(tr("Timeout %1").arg(target.username), &timeout);
    }
    if (kickable)
        addDialogAction(tr("Kick %1").arg(target.username), &kick);
    if (bannable)
        addDialogAction(tr("Ban %1").arg(target.username), &ban);
}

} // namespace MemberModeration
} // namespace UI
} // namespace Acheron
