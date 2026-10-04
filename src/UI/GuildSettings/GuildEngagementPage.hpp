#pragma once

#include "Discord/Entities.hpp"
#include "GuildSettingsPage.hpp"

class QCheckBox;
class QComboBox;
class QRadioButton;

namespace Acheron {
namespace UI {

class UnsavedChangesBar;

class GuildEngagementPage : public GuildSettingsPage
{
    Q_OBJECT
public:
    GuildEngagementPage(Core::ClientInstance *instance, Core::ImageManager *images, Core::Snowflake guildId, QWidget *parent = nullptr);

    [[nodiscard]] bool hasUnsavedChanges() const override;
    void warnUnsavedChanges() override;

protected:
    void load() override;
    void updatePermissions() override;

private:
    struct Settings
    {
        Core::Snowflake systemChannelId;
        Discord::SystemChannelFlags systemChannelFlags;
        Discord::MessageNotificationLevel defaultNotifications = Discord::MessageNotificationLevel::ALL_MESSAGES;
        Core::Snowflake afkChannelId;
        int afkTimeout = 300;

        bool operator==(const Settings &other) const;
    };

    [[nodiscard]] Settings savedSettings() const;
    [[nodiscard]] Settings editedSettings() const;
    [[nodiscard]] static Settings rebased(const Settings &edited, const Settings &base, Settings current);
    void showSaved();
    void showSettings(const Settings &settings);
    void onGuildUpdated();
    void fillChannelCombos();
    void updateControls();
    void save();

    struct FlagCheck
    {
        QCheckBox *check;
        Discord::SystemChannelFlag flag;
    };
    QList<FlagCheck> flagChecks;
    QComboBox *systemChannelCombo;
    QRadioButton *allMessagesRadio;
    QRadioButton *onlyMentionsRadio;
    QComboBox *afkChannelCombo;
    QComboBox *afkTimeoutCombo;
    UnsavedChangesBar *saveBar;
    Settings baseline;
    bool saving = false;
};

} // namespace UI
} // namespace Acheron
