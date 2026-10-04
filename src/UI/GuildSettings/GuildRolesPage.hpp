#pragma once

#include <QDateTime>
#include <QHash>
#include <QPair>
#include <QPixmap>
#include <QUrl>

#include <functional>
#include <optional>

#include "Core/RoleHierarchy.hpp"
#include "Discord/Entities.hpp"
#include "Discord/GuildRequests.hpp"
#include "GuildSettingsPage.hpp"
#include "UI/AvatarRequestTracker.hpp"

class QCheckBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QListWidgetItem;
class QPushButton;
class QStackedWidget;
class QTabWidget;
class QTimer;
class QToolButton;
class QTreeWidget;

namespace Acheron {
namespace UI {

class UnsavedChangesBar;

class GuildRolesPage : public GuildSettingsPage
{
    Q_OBJECT
public:
    GuildRolesPage(Core::ClientInstance *instance, Core::ImageManager *images, Core::Snowflake guildId, QWidget *parent = nullptr);

    [[nodiscard]] bool hasUnsavedChanges() const override;
    void warnUnsavedChanges() override;

protected:
    void load() override;
    void updatePermissions() override;
    void showEvent(QShowEvent *event) override;

private:
    struct PermissionRow
    {
        Discord::Permission permission;
        QString title;
        QString description;
        QCheckBox *check;
        int section;
        bool changeable = false;
        bool matchesSearch = true;
    };

    struct PermissionSection
    {
        QWidget *block;
        QCheckBox *toggle;
    };

    struct ColorSwatch
    {
        QToolButton *button;
        int color;
    };

    struct IconChange
    {
        QString dataUri;
        QPixmap preview;
    };

    struct RoleMemberList
    {
        QList<Core::Snowflake> userIds;
        QDateTime fetchedAt;
    };

    QWidget *buildListPane();
    QWidget *buildDisplayTab();
    QWidget *buildPermissionsTab();
    QWidget *buildMembersTab();

    void refreshRoles();
    void rebaseDrafts(const QList<Discord::Role> &previous);
    [[nodiscard]] QList<Core::Snowflake> mergedOrder(const QList<Core::Snowflake> &order) const;
    void rebuildList();
    void fillListItem(QListWidgetItem *item, Core::Snowflake roleId);
    void updateListItem(Core::Snowflake roleId);
    void onListReordered();
    void filterList();
    void selectRole(Core::Snowflake roleId);
    void createRole();
    void duplicateRole(Core::Snowflake roleId);
    void confirmDeleteRole(Core::Snowflake roleId);
    void showRoleMenu(Core::Snowflake roleId, const QPoint &globalPos);
    void fetchMemberCounts();
    void adjustMemberCount(Core::Snowflake roleId, int delta);

    void updateEditor();
    void updateDisplayTab(const Discord::Role &role, bool editable);
    void updatePermissionsTab(const Discord::Role &role, bool editable);
    void updateSectionToggles();
    void updateMembersTabLabel();
    void filterPermissions();
    void toggleSection(int section);
    void editSelected(const std::function<void(Discord::Role &)> &change);
    void setSelectedColor(int color);
    void chooseCustomColor();
    void chooseIcon();
    void removeIcon();

    void loadRoleMembers();
    void rebuildMemberList();
    void removeMember(Core::Snowflake userId, bool skipConfirm);
    void openAddMembersDialog();
    void onMembersUpdated(Core::Snowflake changedGuildId, const QList<Core::Snowflake> &userIds);
    void onMemberRemoved(Core::Snowflake changedGuildId, Core::Snowflake userId);
    void onImageFetched(const QUrl &url, const QSize &size, const QPixmap &pixmap);

    void updateSaveBar();
    void resetChanges();
    void save();
    void saveNextRole(QList<Core::Snowflake> queue);
    void finishSave(const QString &error);

    [[nodiscard]] Discord::Role currentRole(Core::Snowflake roleId) const;
    [[nodiscard]] std::optional<Discord::Role> savedRole(Core::Snowflake roleId) const;
    [[nodiscard]] QList<Core::Snowflake> savedOrder() const;
    [[nodiscard]] bool isEveryone(Core::Snowflake roleId) const { return roleId == guildId; }
    [[nodiscard]] bool canManageRoles() const;
    [[nodiscard]] bool isLocked(const Discord::Role &role) const;
    [[nodiscard]] bool isOutranked(const Discord::Role &role) const;
    [[nodiscard]] QString lockReason(const Discord::Role &role) const;
    [[nodiscard]] QString managedReason(const Discord::Role &role) const;
    [[nodiscard]] bool affectsSelf(const Discord::Role &role) const;
    [[nodiscard]] Discord::Permissions selfPermissionsWith(Core::Snowflake roleId, Discord::Permissions permissions) const;
    [[nodiscard]] Discord::Permissions clearablePermissions() const;
    [[nodiscard]] bool isRoleDirty(Core::Snowflake roleId) const;
    [[nodiscard]] QList<Core::Snowflake> dirtyRoles() const;
    [[nodiscard]] bool orderDirty() const;
    [[nodiscard]] QList<QPair<Core::Snowflake, int>> changedPositions(const QList<Core::Snowflake> &order) const;
    [[nodiscard]] Discord::RoleEdit editFor(Core::Snowflake roleId) const;

    QLineEdit *searchEdit;
    QPushButton *createRoleButton;
    QPushButton *emptyCreateButton;
    QPushButton *defaultPermissionsCard;
    QLabel *listHint;
    QLabel *rolesHeader;
    QLabel *membersHeader;
    QLabel *emptyRolesTitle;
    QStackedWidget *listStates;
    QListWidget *roleList;
    QWidget *listNoMatch;
    QWidget *listEmpty;

    QStackedWidget *editorStates;
    QWidget *editorPlaceholder;
    QWidget *editor;
    QLabel *editorTitle;
    QToolButton *moreButton;
    QLabel *editorBanner;
    QTabWidget *tabs;
    QWidget *displayTab;
    QWidget *permissionsTab;
    QWidget *membersTab;

    QLineEdit *nameEdit;
    QWidget *colorPicker;
    QToolButton *colorReadout;
    QList<ColorSwatch> colorSwatches;
    QToolButton *customColorSwatch;
    QWidget *iconSection;
    QLabel *iconDescription;
    QLabel *iconPreview;
    QPushButton *chooseIconButton;
    QPushButton *removeIconButton;
    QLabel *iconError;
    QCheckBox *hoistCheck;
    QCheckBox *mentionableCheck;

    QLineEdit *permissionSearch;
    QPushButton *clearPermissionsButton;
    QLabel *noPermissionsLabel;
    QList<PermissionRow> permissionRows;
    QList<PermissionSection> permissionSections;

    QLineEdit *memberSearch;
    QPushButton *addMembersButton;
    QLabel *membersNote;
    QStackedWidget *memberStates;
    QTreeWidget *memberList;
    QLabel *membersEmpty;

    UnsavedChangesBar *saveBar;
    QTimer *refreshTimer;
    QTimer *memberSearchDebounce;
    QTimer *memberRebuildTimer;

    QList<Discord::Role> savedRoles;
    std::optional<Core::RoleHierarchy> hierarchy;
    QHash<Core::Snowflake, Discord::Role> drafts;
    QHash<Core::Snowflake, IconChange> iconChanges;
    QList<Core::Snowflake> draftOrder;
    Core::Snowflake selectedRoleId;
    QHash<Core::Snowflake, int> memberCounts;
    QDateTime memberCountsFetchedAt;
    QHash<Core::Snowflake, RoleMemberList> roleMembers;
    AvatarRequestTracker<Core::Snowflake> avatarTracker;
    QUrl roleIconUrl;
    bool saving = false;
    bool managingRoles = false;
    bool fetchingMemberCounts = false;
};

} // namespace UI
} // namespace Acheron
