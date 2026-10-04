#pragma once

#include <QHash>

#include "Discord/Entities.hpp"
#include "GuildSettingsPage.hpp"
#include "UI/AvatarRequestTracker.hpp"

class QLabel;
class QPushButton;
class QTreeWidget;
class QTreeWidgetItem;

namespace Acheron {
namespace UI {

class GuildStickersPage : public GuildSettingsPage
{
    Q_OBJECT
public:
    GuildStickersPage(Core::ClientInstance *instance, Core::ImageManager *images, Core::Snowflake guildId, QWidget *parent = nullptr);

protected:
    void load() override;
    void updatePermissions() override;

private:
    void fetchStickers();
    void rebuildTable();
    void applyImages(QTreeWidgetItem *row, const Discord::Sticker &sticker);
    void openUploadDialog();
    void openEditDialog(Core::Snowflake stickerId);
    void deleteSticker(Core::Snowflake stickerId);
    void showContextMenu(const QPoint &pos);

    [[nodiscard]] int totalSlots() const;
    [[nodiscard]] bool canManage(const Discord::Sticker &sticker) const;
    [[nodiscard]] const Discord::Sticker *findSticker(Core::Snowflake stickerId) const;

    QLabel *uploadHint;
    QLabel *slotsLabel;
    QPushButton *uploadButton;
    QTreeWidget *table;
    QList<Discord::Sticker> stickers;
    QHash<Core::Snowflake, QTreeWidgetItem *> rowsById;
    AvatarRequestTracker<Core::Snowflake> imageTracker;
};

} // namespace UI
} // namespace Acheron
