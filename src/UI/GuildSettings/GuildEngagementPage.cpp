#include "GuildEngagementPage.hpp"

#include <QButtonGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QLabel>
#include <QRadioButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QVBoxLayout>

#include "Core/ClientInstance.hpp"
#include "Core/Theme/Icons.hpp"
#include "UnsavedChangesBar.hpp"

namespace Acheron {
namespace UI {

using Discord::SystemChannelFlag;

namespace {

struct TimeoutOption
{
    int seconds;
    const char *label;
};

constexpr TimeoutOption TimeoutOptions[] = {
    { 60, QT_TRANSLATE_NOOP("GuildEngagementPage", "1 minute") },
    { 300, QT_TRANSLATE_NOOP("GuildEngagementPage", "5 minutes") },
    { 900, QT_TRANSLATE_NOOP("GuildEngagementPage", "15 minutes") },
    { 1800, QT_TRANSLATE_NOOP("GuildEngagementPage", "30 minutes") },
    { 3600, QT_TRANSLATE_NOOP("GuildEngagementPage", "1 hour") },
};

bool isCreatorMonetizable(const Discord::Guild &guild)
{
    return (guild.hasFeature(QStringLiteral("CREATOR_MONETIZABLE")) || guild.hasFeature(QStringLiteral("CREATOR_MONETIZABLE_PROVISIONAL"))) &&
           !guild.hasFeature(QStringLiteral("CREATOR_MONETIZABLE_DISABLED"));
}

Core::Snowflake comboChannel(const QComboBox *combo)
{
    return Core::Snowflake(combo->currentData().toULongLong());
}

void selectChannel(QComboBox *combo, Core::Snowflake channelId, Core::ClientInstance *instance)
{
    const QVariant value = QVariant::fromValue<quint64>(channelId.isValid() ? quint64(channelId) : 0);
    int index = combo->findData(value);
    if (index < 0 && channelId.isValid()) {
        const auto channel = instance ? instance->getChannel(channelId) : std::nullopt;
        combo->addItem(channel && channel->name.hasValue() ? channel->name.get() : QString::number(quint64(channelId)), value);
        index = combo->count() - 1;
    }
    combo->setCurrentIndex(qMax(0, index));
}

} // namespace

bool GuildEngagementPage::Settings::operator==(const Settings &other) const
{
    return systemChannelId == other.systemChannelId &&
           systemChannelFlags == other.systemChannelFlags &&
           defaultNotifications == other.defaultNotifications &&
           afkChannelId == other.afkChannelId &&
           afkTimeout == other.afkTimeout;
}

GuildEngagementPage::GuildEngagementPage(Core::ClientInstance *instance, Core::ImageManager *images,
                                         Core::Snowflake guildId, QWidget *parent)
    : GuildSettingsPage(instance, images, guildId, parent)
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(24, 20, 24, 16);
    layout->addWidget(makeTitle(tr("Engagement"), this));
    layout->addWidget(makeDescription(tr("Manage settings that help keep your server active."), this));

    auto *scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    auto *content = new QWidget(scroll);
    content->setMaximumWidth(680);
    auto *form = new QVBoxLayout(content);
    form->setContentsMargins(0, 8, 12, 8);
    form->setSpacing(6);

    const auto guild = instance->getGuild(guildId);
    const Discord::SystemChannelFlags currentFlags = guild ? guild->systemChannelFlags.valueOr(Discord::SystemChannelFlags()) : Discord::SystemChannelFlags();

    form->addWidget(makeFieldLabel(tr("System Messages"), content));
    form->addWidget(makeDescription(tr("Configure system event messages sent to your server."), content));

    auto addFlag = [&](const QString &text, SystemChannelFlag flag) {
        auto *check = new QCheckBox(text, content);
        form->addWidget(check);
        flagChecks.append({ check, flag });
        connect(check, &QCheckBox::toggled, this, &GuildEngagementPage::updateControls);
    };
    addFlag(tr("Send a random welcome message when someone joins this server."), SystemChannelFlag::SUPPRESS_JOIN_NOTIFICATIONS);
    addFlag(tr("Prompt members to reply to welcome messages with a sticker."), SystemChannelFlag::SUPPRESS_JOIN_NOTIFICATION_REPLIES);
    addFlag(tr("Send a message when someone boosts this server."), SystemChannelFlag::SUPPRESS_PREMIUM_SUBSCRIPTIONS);
    addFlag(tr("Send helpful tips for server setup."), SystemChannelFlag::SUPPRESS_GUILD_REMINDER_NOTIFICATIONS);
    if ((guild && isCreatorMonetizable(*guild)) || currentFlags.testFlag(SystemChannelFlag::SUPPRESS_ROLE_SUBSCRIPTION_PURCHASE_NOTIFICATIONS))
        addFlag(tr("Send a message when someone purchases a Server Product or Server Subscription"),
                SystemChannelFlag::SUPPRESS_ROLE_SUBSCRIPTION_PURCHASE_NOTIFICATIONS);
    if ((guild && isCreatorMonetizable(*guild)) || currentFlags.testFlag(SystemChannelFlag::SUPPRESS_ROLE_SUBSCRIPTION_PURCHASE_NOTIFICATION_REPLIES))
        addFlag(tr("Prompt members to reply to Server Subscription congratulation messages with a sticker"),
                SystemChannelFlag::SUPPRESS_ROLE_SUBSCRIPTION_PURCHASE_NOTIFICATION_REPLIES);

    form->addSpacing(8);
    form->addWidget(makeFieldLabel(tr("System Messages Channel"), content));
    systemChannelCombo = new QComboBox(content);
    form->addWidget(systemChannelCombo);
    form->addWidget(makeDescription(tr("This is the channel we send system event messages to."), content));

    form->addSpacing(16);
    form->addWidget(makeFieldLabel(tr("Default Notification Settings"), content));
    form->addWidget(makeDescription(tr("This will determine whether members who have not explicitly set their notification "
                                       "settings receive a notification for every message sent in this server or not."),
                                    content));
    form->addWidget(makeDescription(tr("We highly recommend setting this to only @mentions for a Community Server."), content));
    auto *notificationGroup = new QButtonGroup(content);
    allMessagesRadio = new QRadioButton(tr("All Messages"), content);
    onlyMentionsRadio = new QRadioButton(tr("Only @mentions"), content);
    notificationGroup->addButton(allMessagesRadio);
    notificationGroup->addButton(onlyMentionsRadio);
    form->addWidget(allMessagesRadio);
    form->addWidget(onlyMentionsRadio);

    form->addSpacing(16);
    form->addWidget(makeFieldLabel(tr("Inactive Channel"), content));
    afkChannelCombo = new QComboBox(content);
    form->addWidget(afkChannelCombo);
    form->addWidget(makeFieldLabel(tr("Inactive Timeout"), content));
    afkTimeoutCombo = new QComboBox(content);
    for (const TimeoutOption &option : TimeoutOptions)
        afkTimeoutCombo->addItem(tr(option.label), option.seconds);
    form->addWidget(afkTimeoutCombo);
    form->addWidget(makeDescription(tr("Automatically move members to this channel and mute them when they have been idle "
                                       "for longer than the inactive timeout. This does not affect browsers."),
                                    content));
    form->addStretch();

    scroll->setWidget(content);
    layout->addWidget(scroll, 1);

    saveBar = new UnsavedChangesBar(this);
    saveBar->hide();
    connect(saveBar, &UnsavedChangesBar::resetClicked, this, &GuildEngagementPage::showSaved);
    connect(saveBar, &UnsavedChangesBar::saveClicked, this, &GuildEngagementPage::save);
    layout->addWidget(saveBar);

    connect(systemChannelCombo, qOverload<int>(&QComboBox::currentIndexChanged), this, &GuildEngagementPage::updateControls);
    connect(afkChannelCombo, qOverload<int>(&QComboBox::currentIndexChanged), this, &GuildEngagementPage::updateControls);
    connect(afkTimeoutCombo, qOverload<int>(&QComboBox::currentIndexChanged), this, &GuildEngagementPage::updateControls);
    connect(allMessagesRadio, &QRadioButton::toggled, this, &GuildEngagementPage::updateControls);

    connect(instance, &Core::ClientInstance::guildUpdated, this, [this](const Discord::Guild &updated) {
        if (updated.id.get() == this->guildId)
            onGuildUpdated();
    });
    auto refillChannels = [this](Core::Snowflake changedGuildId) {
        if (changedGuildId != this->guildId || !isLoaded())
            return;
        const Settings edited = editedSettings();
        fillChannelCombos();
        showSettings(edited);
    };
    connect(instance, &Core::ClientInstance::channelCreated, this, [refillChannels](const Discord::ChannelCreate &event) {
        refillChannels(event.channel->guildId.valueOr(Core::Snowflake()));
    });
    connect(instance, &Core::ClientInstance::channelUpdated, this, [refillChannels](const Discord::ChannelUpdate &update) {
        refillChannels(update.channel->guildId.valueOr(Core::Snowflake()));
    });
    connect(instance, &Core::ClientInstance::channelDeleted, this, [refillChannels](const Discord::ChannelDelete &event) {
        refillChannels(event.guildId.valueOr(Core::Snowflake()));
    });

    updatePermissions();
}

void GuildEngagementPage::updatePermissions()
{
    const bool canManage = hasPermission(Discord::Permission::MANAGE_GUILD);
    systemChannelCombo->setEnabled(canManage);
    allMessagesRadio->setEnabled(canManage);
    onlyMentionsRadio->setEnabled(canManage);
    afkChannelCombo->setEnabled(canManage);
    const bool hasSystemChannel = quint64(comboChannel(systemChannelCombo)) != 0;
    for (const FlagCheck &flagCheck : flagChecks)
        flagCheck.check->setEnabled(canManage && hasSystemChannel);
    afkTimeoutCombo->setEnabled(canManage && quint64(comboChannel(afkChannelCombo)) != 0);
}

void GuildEngagementPage::load()
{
    fillChannelCombos();
    showSaved();
}

void GuildEngagementPage::showSaved()
{
    baseline = savedSettings();
    showSettings(baseline);
}

void GuildEngagementPage::onGuildUpdated()
{
    if (!isLoaded() || saving)
        return;
    const Settings current = savedSettings();
    const Settings edited = rebased(editedSettings(), baseline, current);
    baseline = current;
    showSettings(edited);
}

GuildEngagementPage::Settings GuildEngagementPage::rebased(const Settings &edited, const Settings &base, Settings current)
{
    if (edited.systemChannelId != base.systemChannelId)
        current.systemChannelId = edited.systemChannelId;
    const Discord::SystemChannelFlags toggled = edited.systemChannelFlags ^ base.systemChannelFlags;
    current.systemChannelFlags = (current.systemChannelFlags & ~toggled) | (edited.systemChannelFlags & toggled);
    if (edited.defaultNotifications != base.defaultNotifications)
        current.defaultNotifications = edited.defaultNotifications;
    if (edited.afkChannelId != base.afkChannelId)
        current.afkChannelId = edited.afkChannelId;
    if (edited.afkTimeout != base.afkTimeout)
        current.afkTimeout = edited.afkTimeout;
    return current;
}

GuildEngagementPage::Settings GuildEngagementPage::savedSettings() const
{
    Settings settings;
    const auto guild = instance ? instance->getGuild(guildId) : std::nullopt;
    if (!guild)
        return settings;

    settings.systemChannelId = guild->systemChannelId.hasValue() ? guild->systemChannelId.get() : Core::Snowflake();
    settings.systemChannelFlags = guild->systemChannelFlags.valueOr(Discord::SystemChannelFlags());
    settings.defaultNotifications = guild->defaultMessageNotifications.valueOr(Discord::MessageNotificationLevel::ALL_MESSAGES);
    settings.afkChannelId = guild->afkChannelId.hasValue() ? guild->afkChannelId.get() : Core::Snowflake();
    settings.afkTimeout = guild->afkTimeout.valueOr(300);
    return settings;
}

GuildEngagementPage::Settings GuildEngagementPage::editedSettings() const
{
    Settings settings = savedSettings();
    const Core::Snowflake systemChannel = comboChannel(systemChannelCombo);
    settings.systemChannelId = quint64(systemChannel) != 0 ? systemChannel : Core::Snowflake();
    for (const FlagCheck &flagCheck : flagChecks)
        settings.systemChannelFlags.setFlag(flagCheck.flag, !flagCheck.check->isChecked());
    settings.defaultNotifications = onlyMentionsRadio->isChecked() ? Discord::MessageNotificationLevel::ONLY_MENTIONS
                                                                   : Discord::MessageNotificationLevel::ALL_MESSAGES;
    const Core::Snowflake afkChannel = comboChannel(afkChannelCombo);
    settings.afkChannelId = quint64(afkChannel) != 0 ? afkChannel : Core::Snowflake();
    settings.afkTimeout = afkTimeoutCombo->currentData().toInt();
    return settings;
}

void GuildEngagementPage::fillChannelCombos()
{
    QSignalBlocker systemBlocker(systemChannelCombo);
    QSignalBlocker afkBlocker(afkChannelCombo);

    systemChannelCombo->clear();
    systemChannelCombo->addItem(tr("No System Messages"), QVariant::fromValue<quint64>(0));
    const QIcon textIcon = Core::Theme::Icons::icon(Core::Theme::Icons::Name::Hash, Core::Theme::Token::PrimaryText);
    for (const auto &channel : channelsOfType(Discord::ChannelType::GUILD_TEXT))
        systemChannelCombo->addItem(textIcon, channel.name.valueOr(QString()), QVariant::fromValue<quint64>(channel.id.get()));

    afkChannelCombo->clear();
    afkChannelCombo->addItem(tr("No Inactive Channel"), QVariant::fromValue<quint64>(0));
    const QIcon voiceIcon = Core::Theme::Icons::icon(Core::Theme::Icons::Name::VolumeOn, Core::Theme::Token::PrimaryText);
    for (const auto &channel : channelsOfType(Discord::ChannelType::GUILD_VOICE))
        afkChannelCombo->addItem(voiceIcon, channel.name.valueOr(QString()), QVariant::fromValue<quint64>(channel.id.get()));
}

void GuildEngagementPage::showSettings(const Settings &settings)
{
    {
        QSignalBlocker systemBlocker(systemChannelCombo);
        QSignalBlocker afkBlocker(afkChannelCombo);
        QSignalBlocker timeoutBlocker(afkTimeoutCombo);
        QSignalBlocker allBlocker(allMessagesRadio);
        QSignalBlocker mentionsBlocker(onlyMentionsRadio);

        selectChannel(systemChannelCombo, settings.systemChannelId, instance);
        selectChannel(afkChannelCombo, settings.afkChannelId, instance);
        const int timeoutIndex = afkTimeoutCombo->findData(settings.afkTimeout);
        afkTimeoutCombo->setCurrentIndex(timeoutIndex >= 0 ? timeoutIndex : afkTimeoutCombo->findData(300));
        if (settings.defaultNotifications == Discord::MessageNotificationLevel::ONLY_MENTIONS)
            onlyMentionsRadio->setChecked(true);
        else
            allMessagesRadio->setChecked(true);
        for (const FlagCheck &flagCheck : flagChecks) {
            QSignalBlocker checkBlocker(flagCheck.check);
            flagCheck.check->setChecked(!settings.systemChannelFlags.testFlag(flagCheck.flag));
        }
    }
    updateControls();
}

void GuildEngagementPage::updateControls()
{
    updatePermissions();
    if (!saving)
        saveBar->setVisible(hasUnsavedChanges());
}

bool GuildEngagementPage::hasUnsavedChanges() const
{
    return !(editedSettings() == savedSettings());
}

void GuildEngagementPage::warnUnsavedChanges()
{
    saveBar->flash();
}

void GuildEngagementPage::save()
{
    if (!instance || saving)
        return;

    const Settings settings = editedSettings();
    Discord::GuildEdit edit;
    if (settings.afkChannelId.isValid())
        edit.afkChannelId = settings.afkChannelId;
    else
        edit.afkChannelId = nullptr;
    edit.afkTimeout = settings.afkTimeout;
    if (settings.systemChannelId.isValid())
        edit.systemChannelId = settings.systemChannelId;
    else
        edit.systemChannelId = nullptr;
    edit.defaultMessageNotifications = settings.defaultNotifications;
    edit.systemChannelFlags = settings.systemChannelFlags;

    saving = true;
    saveBar->setSaving(true);
    QPointer<GuildEngagementPage> self(this);
    instance->discord()->modifyGuild(guildId, edit, {}, [self](const Core::Result<void> &result) {
        if (!self)
            return;
        self->saving = false;
        if (!result.success()) {
            self->saveBar->showError(result.error);
            self->saveBar->flash();
            return;
        }
        self->saveBar->setSaving(false);
        self->showSaved();
    });
}

} // namespace UI
} // namespace Acheron
