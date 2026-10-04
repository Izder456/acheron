#pragma once

#include "Discord/Entities.hpp"
#include "GuildSettingsPage.hpp"
#include "UI/AvatarRequestTracker.hpp"

class QLabel;
class QPushButton;
class QStackedWidget;
class QTimer;
class QTreeWidget;
class QTreeWidgetItem;

namespace Acheron {
namespace UI {

class GuildInvitesPage : public GuildSettingsPage
{
    Q_OBJECT
public:
    GuildInvitesPage(Core::ClientInstance *instance, Core::ImageManager *images, Core::Snowflake guildId, QWidget *parent = nullptr);

protected:
    void load() override;
    void updatePermissions() override;
    void showEvent(QShowEvent *event) override;
    void hideEvent(QHideEvent *event) override;

private:
    [[nodiscard]] bool canManage() const;
    [[nodiscard]] bool canRevoke(const Discord::Invite &invite) const;
    [[nodiscard]] QList<Discord::Channel> inviteChannels() const;
    void addInvite(const Discord::Invite &invite);
    void showInvites();
    void fetchInvites();
    void rebuildTable();
    void applyAvatar(int row);
    void refreshCountdowns();
    void updatePauseButton();
    void revoke(const QString &code);
    void openPauseDialog();
    void openCreateDialog();
    void showContextMenu(const QPoint &pos);

    [[nodiscard]] static QString expiryText(const Discord::Invite &invite);

    QWidget *permissionNotice;
    QLabel *summaryLabel;
    QPushButton *pauseButton;
    QPushButton *createButton;
    QStackedWidget *states;
    QLabel *placeholder;
    QTreeWidget *table;
    QWidget *emptyState;
    QTimer *countdown;
    QList<Discord::Invite> invites;
    AvatarRequestTracker<int> avatarTracker;
    bool listFetched = false;
};

} // namespace UI
} // namespace Acheron
