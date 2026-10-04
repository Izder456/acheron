#include "GuildInvitesPage.hpp"

#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QPushButton>
#include <QStackedWidget>
#include <QTimer>
#include <QToolButton>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <algorithm>

#include "Core/ClientInstance.hpp"
#include "Core/ImageManager.hpp"
#include "Core/Theme/Icons.hpp"
#include "Discord/ApiError.hpp"

namespace Acheron {
namespace UI {

namespace {

constexpr int CodeRole = Qt::UserRole;
constexpr int InviterColumn = 0;
constexpr int ChannelColumn = 1;
constexpr int CodeColumn = 2;
constexpr int UsesColumn = 3;
constexpr int ExpiresColumn = 4;
constexpr int RevokeColumn = 5;
constexpr QSize AvatarSize(24, 24);

struct Duration
{
    int seconds;
    const char *label;
};

constexpr Duration ExpireOptions[] = {
    { 1800, QT_TRANSLATE_NOOP("GuildInvitesPage", "30 minutes") },
    { 3600, QT_TRANSLATE_NOOP("GuildInvitesPage", "1 hour") },
    { 21600, QT_TRANSLATE_NOOP("GuildInvitesPage", "6 hours") },
    { 43200, QT_TRANSLATE_NOOP("GuildInvitesPage", "12 hours") },
    { 86400, QT_TRANSLATE_NOOP("GuildInvitesPage", "1 day") },
    { 604800, QT_TRANSLATE_NOOP("GuildInvitesPage", "7 days") },
    { 0, QT_TRANSLATE_NOOP("GuildInvitesPage", "Never") },
};
constexpr int DefaultExpireSeconds = 604800;
constexpr int MaxUsesOptions[] = { 0, 1, 5, 10, 25, 50, 100 };
constexpr int PauseHourOptions[] = { 1, 2, 4, 6, 12, 24 };
constexpr int DefaultPauseHours = 2;

QString inviteUrl(const QString &code)
{
    return QStringLiteral("https://discord.gg/") + code;
}

bool isPaused(const QDateTime &until)
{
    return until.isValid() && until > QDateTime::currentDateTimeUtc();
}

} // namespace

GuildInvitesPage::GuildInvitesPage(Core::ClientInstance *instance, Core::ImageManager *images,
                                   Core::Snowflake guildId, QWidget *parent)
    : GuildSettingsPage(instance, images, guildId, parent)
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(24, 20, 24, 16);
    layout->addWidget(makeTitle(tr("Invites"), this));
    permissionNotice = makePermissionNotice(tr("Showing the invites you've created while you've been connected. The full "
                                               "invite list needs the Manage Server permission."),
                                            this);
    layout->addWidget(permissionNotice);

    auto *actions = new QHBoxLayout();
    summaryLabel = makeFieldLabel(QString(), this);
    actions->addWidget(summaryLabel, 1);
    pauseButton = new QPushButton(this);
    actions->addWidget(pauseButton);
    createButton = new QPushButton(tr("Create invite link"), this);
    actions->addWidget(createButton);
    layout->addLayout(actions);

    states = new QStackedWidget(this);
    placeholder = makeDescription(QString(), states);
    placeholder->setAlignment(Qt::AlignCenter);
    states->addWidget(placeholder);

    table = new QTreeWidget(states);
    table->setColumnCount(6);
    table->setHeaderLabels({ tr("Inviter"), tr("Channel"), tr("Invite Code"), tr("Uses"), tr("Expires"), QString() });
    table->setRootIsDecorated(false);
    table->setUniformRowHeights(true);
    table->setIconSize(AvatarSize);
    table->setContextMenuPolicy(Qt::CustomContextMenu);
    table->header()->setStretchLastSection(false);
    table->header()->setSectionResizeMode(InviterColumn, QHeaderView::Stretch);
    table->header()->setSectionResizeMode(RevokeColumn, QHeaderView::Fixed);
    table->setColumnWidth(RevokeColumn, 40);
    states->addWidget(table);

    emptyState = new QWidget(states);
    auto *emptyLayout = new QVBoxLayout(emptyState);
    emptyLayout->addStretch();
    auto *emptyTitle = makeFieldLabel(tr("No invites yet"), emptyState);
    emptyTitle->setAlignment(Qt::AlignCenter);
    emptyLayout->addWidget(emptyTitle);
    auto *emptyText = makeDescription(tr("Feeling aimless? Like a paper plane drifting through the skies? Get some friends "
                                         "in here by creating an invite link!"),
                                      emptyState);
    emptyText->setAlignment(Qt::AlignCenter);
    emptyLayout->addWidget(emptyText);
    emptyLayout->addStretch();
    states->addWidget(emptyState);
    layout->addWidget(states, 1);

    countdown = new QTimer(this);
    countdown->setInterval(1000);
    connect(countdown, &QTimer::timeout, this, &GuildInvitesPage::refreshCountdowns);

    connect(pauseButton, &QPushButton::clicked, this, &GuildInvitesPage::openPauseDialog);
    connect(createButton, &QPushButton::clicked, this, &GuildInvitesPage::openCreateDialog);
    connect(table, &QTreeWidget::customContextMenuRequested, this, &GuildInvitesPage::showContextMenu);

    connect(instance, &Core::ClientInstance::guildUpdated, this, [this](const Discord::Guild &guild) {
        if (guild.id.get() == this->guildId)
            updatePauseButton();
    });
    connect(images, &Core::ImageManager::imageFetched, this, [this](const QUrl &url, const QSize &size, const QPixmap &) {
        if (size == AvatarSize)
            avatarTracker.notify(url, [this](int row) { applyAvatar(row); });
    });

    updatePauseButton();
    updatePermissions();
}

void GuildInvitesPage::load()
{
    showInvites();
}

void GuildInvitesPage::updatePermissions()
{
    const bool manage = canManage();
    permissionNotice->setVisible(!manage);
    pauseButton->setVisible(manage);
    createButton->setVisible(!inviteChannels().isEmpty());
    if (isLoaded())
        showInvites();
}

void GuildInvitesPage::showInvites()
{
    if (canManage()) {
        if (listFetched)
            rebuildTable();
        else
            fetchInvites();
        return;
    }
    listFetched = false;
    invites = instance ? instance->createdInvites(guildId) : QList<Discord::Invite>();
    rebuildTable();
}

bool GuildInvitesPage::canManage() const
{
    return hasPermission(Discord::Permission::MANAGE_GUILD);
}

bool GuildInvitesPage::canRevoke(const Discord::Invite &invite) const
{
    return canManage() ||
           (instance && instance->permissions()->hasChannelPermission(selfId(), invite.resolvedChannelId(),
                                                                      Discord::Permission::MANAGE_CHANNELS));
}

QList<Discord::Channel> GuildInvitesPage::inviteChannels() const
{
    QList<Discord::Channel> channels;
    if (!instance)
        return channels;
    for (auto type : { Discord::ChannelType::GUILD_TEXT, Discord::ChannelType::GUILD_NEWS, Discord::ChannelType::GUILD_FORUM,
                       Discord::ChannelType::GUILD_MEDIA, Discord::ChannelType::GUILD_VOICE,
                       Discord::ChannelType::GUILD_STAGE_VOICE })
        for (const auto &channel : channelsOfType(type))
            if (instance->permissions()->hasChannelPermission(selfId(), channel.id.get(),
                                                              Discord::Permission::VIEW_CHANNEL | Discord::Permission::CREATE_INSTANT_INVITE))
                channels.append(channel);
    return channels;
}

void GuildInvitesPage::addInvite(const Discord::Invite &invite)
{
    invites.erase(std::remove_if(invites.begin(), invites.end(),
                                 [&invite](const Discord::Invite &known) { return known.code.get() == invite.code.get(); }),
                  invites.end());
    invites.append(invite);
    rebuildTable();
}

void GuildInvitesPage::showEvent(QShowEvent *event)
{
    countdown->start();
    GuildSettingsPage::showEvent(event);
}

void GuildInvitesPage::hideEvent(QHideEvent *event)
{
    countdown->stop();
    GuildSettingsPage::hideEvent(event);
}

void GuildInvitesPage::fetchInvites()
{
    if (!instance || !canManage())
        return;

    if (!listFetched) {
        summaryLabel->clear();
        placeholder->setText(tr("Loading invites…"));
        states->setCurrentWidget(placeholder);
    }

    QPointer<GuildInvitesPage> self(this);
    instance->discord()->fetchGuildInvites(guildId, [self](const Core::Result<QList<Discord::Invite>> &result) {
        if (!self)
            return;
        if (!result.success()) {
            if (self->listFetched)
                self->showActionError(result.error);
            else
                self->placeholder->setText(result.error);
            return;
        }
        self->listFetched = true;
        self->invites = *result.value;
        self->rebuildTable();
    });
}

QString GuildInvitesPage::expiryText(const Discord::Invite &invite)
{
    const QDateTime expires = invite.expiryTime();
    if (!expires.isValid())
        return QStringLiteral("∞");

    qint64 remaining = qMax<qint64>(0, QDateTime::currentDateTimeUtc().secsTo(expires));
    const qint64 days = remaining / 86400;
    remaining %= 86400;
    const QString clock = QStringLiteral("%1:%2:%3")
                                  .arg(remaining / 3600, 2, 10, QLatin1Char('0'))
                                  .arg((remaining % 3600) / 60, 2, 10, QLatin1Char('0'))
                                  .arg(remaining % 60, 2, 10, QLatin1Char('0'));
    return days > 0 ? QStringLiteral("%1:%2").arg(days, 2, 10, QLatin1Char('0')).arg(clock) : clock;
}

void GuildInvitesPage::rebuildTable()
{
    if (!instance)
        return;

    std::sort(invites.begin(), invites.end(), [](const Discord::Invite &a, const Discord::Invite &b) {
        const QString nameA = a.inviter.hasValue() ? a.inviter->username.get() : QString();
        const QString nameB = b.inviter.hasValue() ? b.inviter->username.get() : QString();
        if (const int order = nameA.compare(nameB, Qt::CaseInsensitive))
            return order < 0;
        return a.code.get() < b.code.get();
    });

    summaryLabel->setText(invites.isEmpty() ? tr("No active invite links") : tr("Active invite links"));
    states->setCurrentWidget(invites.isEmpty() ? emptyState : table);

    table->clear();
    avatarTracker.clear();
    for (int row = 0; row < invites.size(); row++) {
        const Discord::Invite &invite = invites[row];
        auto *item = new QTreeWidgetItem(table);
        item->setData(0, CodeRole, invite.code.get());
        if (invite.inviter.hasValue()) {
            item->setText(InviterColumn, instance->users()->getAuthorDisplayName(invite.inviter.get(), guildId));
            item->setToolTip(InviterColumn, invite.inviter->tag());
        } else {
            item->setText(InviterColumn, tr("Unknown User"));
        }
        applyAvatar(row);

        QString channelName;
        if (invite.channel.hasValue() && invite.channel->name.hasValue())
            channelName = invite.channel->name.get();
        else if (auto channel = instance->getChannel(invite.resolvedChannelId()); channel && channel->name.hasValue())
            channelName = channel->name.get();
        item->setText(ChannelColumn, channelName.isEmpty() ? QString() : QStringLiteral("#") + channelName);

        item->setText(CodeColumn, invite.code.get());
        const int maxUses = invite.maxUses.valueOr(0);
        item->setText(UsesColumn, maxUses > 0 ? QStringLiteral("%1/%2").arg(invite.uses.valueOr(0)).arg(maxUses) : QString::number(invite.uses.valueOr(0)));
        item->setText(ExpiresColumn, expiryText(invite));

        if (!canRevoke(invite))
            continue;
        auto *revokeButton = new QToolButton(table);
        revokeButton->setAutoRaise(true);
        revokeButton->setIcon(Core::Theme::Icons::icon(Core::Theme::Icons::Name::X, Core::Theme::Token::ChatError));
        revokeButton->setToolTip(tr("Remove"));
        const QString code = invite.code.get();
        connect(revokeButton, &QToolButton::clicked, this, [this, code]() { revoke(code); });
        table->setItemWidget(item, RevokeColumn, revokeButton);
    }
}

void GuildInvitesPage::applyAvatar(int row)
{
    if (!instance || row >= invites.size() || row >= table->topLevelItemCount() || !invites[row].inviter.hasValue())
        return;
    const QUrl url = instance->users()->getAvatarUrl(invites[row].inviter.get(), guildId, 32);
    const QPixmap avatar = avatarTracker.fetch(images, url, AvatarSize, row, instance->accountId());
    table->topLevelItem(row)->setIcon(InviterColumn, QIcon(roundedPixmap(avatar, AvatarSize.width(), AvatarSize.width() / 2.0)));
}

void GuildInvitesPage::refreshCountdowns()
{
    const QDateTime now = QDateTime::currentDateTimeUtc();
    const auto expired = [&now](const Discord::Invite &invite) { return invite.hasExpired(now); };
    if (std::any_of(invites.cbegin(), invites.cend(), expired)) {
        invites.erase(std::remove_if(invites.begin(), invites.end(), expired), invites.end());
        rebuildTable();
        return;
    }
    for (int row = 0; row < table->topLevelItemCount() && row < invites.size(); row++)
        table->topLevelItem(row)->setText(ExpiresColumn, expiryText(invites[row]));
}

void GuildInvitesPage::updatePauseButton()
{
    const auto guild = instance ? instance->getGuild(guildId) : std::nullopt;
    pauseButton->setText(guild && guild->invitesPaused() ? tr("Enable Invites") : tr("Pause Invites"));
}

void GuildInvitesPage::revoke(const QString &code)
{
    if (!instance)
        return;

    const auto found = std::find_if(invites.begin(), invites.end(), [&code](const Discord::Invite &invite) { return invite.code.get() == code; });
    if (found == invites.end())
        return;
    const Discord::Invite removed = *found;
    invites.erase(found);
    rebuildTable();

    QPointer<GuildInvitesPage> self(this);
    instance->discord()->revokeInvite(code, [self, removed](const Core::Result<void> &result) {
        if (!self || result.success() || result.code == Discord::ApiError::UnknownInvite)
            return;
        self->showActionError(result.error);
        if (self->canManage())
            self->fetchInvites();
        else
            self->addInvite(removed);
    });
}

void GuildInvitesPage::openPauseDialog()
{
    if (!instance)
        return;
    const auto guild = instance->getGuild(guildId);
    const Discord::GuildIncidentsData incidents = guild ? guild->incidentsData.valueOr({}) : Discord::GuildIncidentsData();
    const bool invitesPaused = guild && guild->invitesPaused();
    const bool dmsPaused = incidents.dmsDisabledUntil.hasValue() && isPaused(incidents.dmsDisabledUntil.get());

    auto *dialog = new QDialog(this);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setWindowTitle(tr("Security Actions"));
    auto *layout = new QVBoxLayout(dialog);

    layout->addWidget(makeFieldLabel(tr("Select a time"), dialog));
    auto *hours = new QComboBox(dialog);
    for (int option : PauseHourOptions)
        hours->addItem(option == 1 ? tr("1 hour") : tr("%1 hours").arg(option), option);
    const int currentHours = incidents.lockdownDurationHours.hasValue() ? incidents.lockdownDurationHours.get() : DefaultPauseHours;
    hours->setCurrentIndex(qMax(0, hours->findData(currentHours)));
    layout->addWidget(hours);

    auto *pauseInvites = new QCheckBox(tr("Pause Invites"), dialog);
    pauseInvites->setChecked(invitesPaused);
    layout->addWidget(pauseInvites);
    layout->addWidget(makeDescription(tr("Temporarily stop new members from joining this server via invite or vanity links."), dialog));

    auto *pauseDms = new QCheckBox(tr("Pause DMs"), dialog);
    pauseDms->setChecked(dmsPaused);
    layout->addWidget(pauseDms);
    layout->addWidget(makeDescription(tr("Temporarily stop new direct messages from being sent between members in your "
                                         "server. Friends can still DM each other, moderators can still DM members, and your Apps can still DM members."),
                                      dialog));

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, dialog);
    connect(buttons, &QDialogButtonBox::accepted, dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, dialog, &QDialog::reject);
    layout->addWidget(buttons);

    connect(dialog, &QDialog::accepted, this, [this, hours, pauseInvites, pauseDms]() {
        if (!instance)
            return;
        const int chosenHours = hours->currentData().toInt();
        const QDateTime until = QDateTime::currentDateTimeUtc().addSecs(qint64(chosenHours) * 3600);
        const bool anyPaused = pauseInvites->isChecked() || pauseDms->isChecked();

        QPointer<GuildInvitesPage> self(this);
        instance->discord()->setIncidentActions(guildId, pauseInvites->isChecked() ? until : QDateTime(),
                                                pauseDms->isChecked() ? until : QDateTime(),
                                                anyPaused ? std::optional<int>(chosenHours) : std::nullopt,
                                                [self](const Core::Result<void> &result) {
                                                    if (self && !result.success())
                                                        self->showActionError(result.error);
                                                });
    });
    dialog->open();
}

void GuildInvitesPage::openCreateDialog()
{
    if (!instance)
        return;

    const QList<Discord::Channel> channels = inviteChannels();
    if (channels.isEmpty()) {
        showActionError(tr("You can't create invites in any channel of this server."));
        return;
    }

    auto *dialog = new QDialog(this);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setWindowTitle(tr("Server invite link settings"));
    auto *layout = new QVBoxLayout(dialog);
    auto *form = new QFormLayout();

    auto *channelCombo = new QComboBox(dialog);
    for (const auto &channel : channels)
        channelCombo->addItem(channel.name.valueOr(QString()), QVariant::fromValue<quint64>(channel.id.get()));
    form->addRow(tr("Channel"), channelCombo);

    auto *expireCombo = new QComboBox(dialog);
    for (const Duration &option : ExpireOptions)
        expireCombo->addItem(tr(option.label), option.seconds);
    expireCombo->setCurrentIndex(expireCombo->findData(DefaultExpireSeconds));
    form->addRow(tr("Expire After"), expireCombo);

    auto *usesCombo = new QComboBox(dialog);
    for (int option : MaxUsesOptions)
        usesCombo->addItem(option == 0 ? tr("No Limit") : QString::number(option), option);
    form->addRow(tr("Max Number of Uses"), usesCombo);
    layout->addLayout(form);

    auto *temporary = new QCheckBox(tr("Grant temporary membership"), dialog);
    layout->addWidget(temporary);
    layout->addWidget(makeDescription(tr("Temporary members are automatically kicked when they disconnect unless a role has been assigned"), dialog));

    auto *linkRow = new QHBoxLayout();
    auto *linkEdit = new QLineEdit(dialog);
    linkEdit->setReadOnly(true);
    linkEdit->setPlaceholderText(tr("Generate a link to share it"));
    linkRow->addWidget(linkEdit, 1);
    auto *copyButton = new QPushButton(tr("Copy"), dialog);
    copyButton->setEnabled(false);
    linkRow->addWidget(copyButton);
    layout->addLayout(linkRow);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, dialog);
    QPushButton *generate = buttons->addButton(tr("Generate a New Link"), QDialogButtonBox::ActionRole);
    layout->addWidget(buttons);

    connect(buttons, &QDialogButtonBox::rejected, dialog, &QDialog::reject);
    connect(copyButton, &QPushButton::clicked, dialog, [linkEdit]() { QGuiApplication::clipboard()->setText(linkEdit->text()); });

    QPointer<QDialog> dialogGuard(dialog);
    QPointer<GuildInvitesPage> self(this);
    connect(generate, &QPushButton::clicked, dialog,
            [this, dialogGuard, self, generate, channelCombo, expireCombo, usesCombo, temporary, linkEdit, copyButton]() {
                if (!instance)
                    return;
                generate->setEnabled(false);
                instance->discord()->createInvite(
                        Core::Snowflake(channelCombo->currentData().toULongLong()), expireCombo->currentData().toInt(),
                        usesCombo->currentData().toInt(), temporary->isChecked(),
                        [self, dialogGuard, linkEdit, copyButton, generate](const Core::Result<Discord::Invite> &result) {
                            if (self && result.success())
                                self->addInvite(*result.value);
                            if (!dialogGuard)
                                return;
                            generate->setEnabled(true);
                            if (!result.success()) {
                                linkEdit->setText(result.error);
                                copyButton->setEnabled(false);
                                return;
                            }
                            linkEdit->setText(inviteUrl(result.value->code.get()));
                            copyButton->setEnabled(true);
                        });
            });

    dialog->open();
}

void GuildInvitesPage::showContextMenu(const QPoint &pos)
{
    QTreeWidgetItem *item = table->itemAt(pos);
    if (!item)
        return;
    const QString code = item->data(0, CodeRole).toString();
    const auto invite = std::find_if(invites.cbegin(), invites.cend(), [&code](const Discord::Invite &known) { return known.code.get() == code; });

    QMenu menu(this);
    menu.addAction(tr("Copy Invite Link"), this, [code]() { QGuiApplication::clipboard()->setText(inviteUrl(code)); });
    if (invite != invites.cend() && canRevoke(*invite)) {
        QAction *revokeAction = menu.addAction(tr("Revoke Invite"), this, [this, code]() { revoke(code); });
        revokeAction->setIcon(Core::Theme::Icons::icon(Core::Theme::Icons::Name::X, Core::Theme::Token::ChatError));
    }
    menu.exec(table->viewport()->mapToGlobal(pos));
}

} // namespace UI
} // namespace Acheron
