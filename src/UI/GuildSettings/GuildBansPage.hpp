#pragma once

#include <QHash>

#include "Discord/Entities.hpp"
#include "GuildSettingsPage.hpp"
#include "UI/AvatarRequestTracker.hpp"

class QLabel;
class QLineEdit;
class QPushButton;
class QStackedWidget;
class QTreeWidget;
class QTreeWidgetItem;

namespace Acheron {
namespace UI {

class GuildBansPage : public GuildSettingsPage
{
    Q_OBJECT
public:
    GuildBansPage(Core::ClientInstance *instance, Core::ImageManager *images, Core::Snowflake guildId, QWidget *parent = nullptr);

protected:
    void load() override;
    void updatePermissions() override;

private:
    [[nodiscard]] bool canViewBans() const;
    void fetchBatch();
    void searchServer();
    void addBans(const QList<Discord::Ban> &bans);
    void rebuildList();
    void applyAvatar(QTreeWidgetItem *item, const Discord::User &user);
    void showDetails(Core::Snowflake userId);
    void revoke(Core::Snowflake userId);
    void showContextMenu(const QPoint &pos);

    [[nodiscard]] bool matchesFilter(const Discord::Ban &ban) const;

    QWidget *permissionNotice;
    QLineEdit *searchEdit;
    QPushButton *searchButton;
    QPushButton *loadMoreButton;
    QStackedWidget *states;
    QTreeWidget *list;
    QWidget *emptyState;
    QLabel *emptyTitle;
    QLabel *emptyText;

    QList<Core::Snowflake> order;
    QHash<Core::Snowflake, Discord::Ban> bans;
    QHash<Core::Snowflake, QTreeWidgetItem *> rows;
    AvatarRequestTracker<Core::Snowflake> avatarTracker;
    Core::Snowflake lastBatchUserId;
    bool loading = false;
    bool listFetched = false;
};

} // namespace UI
} // namespace Acheron
