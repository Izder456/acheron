#pragma once

#include <QHash>
#include <QStringList>

#include "Discord/Entities.hpp"
#include "GuildSettingsPage.hpp"
#include "UI/AvatarRequestTracker.hpp"

class QLabel;
class QPushButton;
class QTimer;
class QTreeWidget;
class QTreeWidgetItem;

namespace Acheron {
namespace UI {

class GuildEmojiPage : public GuildSettingsPage
{
    Q_OBJECT
public:
    GuildEmojiPage(Core::ClientInstance *instance, Core::ImageManager *images, Core::Snowflake guildId, QWidget *parent = nullptr);

protected:
    void load() override;
    void updatePermissions() override;
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dropEvent(QDropEvent *event) override;

private:
    void fetchEmojis();
    void refetchThrottled();
    void rebuildTables();
    void fillTable(QTreeWidget *table, const QList<Discord::Emoji> &list);
    void applyImages(QTreeWidgetItem *row, const Discord::Emoji &emoji);
    void chooseUploads();
    void upload(const QStringList &paths);
    void uploadNext();
    void finishUploads();
    void startRename(QTreeWidgetItem *row);
    void onRenameEnded();
    void onItemChanged(QTreeWidgetItem *item, int column);
    void deleteEmoji(Core::Snowflake emojiId);
    void showContextMenu(QTreeWidget *table, const QPoint &pos);

    [[nodiscard]] bool canUpload() const;
    [[nodiscard]] bool canManage(const Discord::Emoji &emoji) const;
    [[nodiscard]] bool isPremiumEmoji(const Discord::Emoji &emoji) const;
    [[nodiscard]] int slotsPerType() const;
    [[nodiscard]] Discord::Emoji *findEmoji(Core::Snowflake emojiId);

    QLabel *descriptionLabel;
    QWidget *uploadArea;
    QLabel *statusLabel;
    QPushButton *uploadButton;
    QLabel *staticHeader;
    QLabel *animatedHeader;
    QTreeWidget *staticTable;
    QTreeWidget *animatedTable;
    QTimer *refetchThrottle;

    QList<Discord::Emoji> emojis;
    QHash<Core::Snowflake, QTreeWidgetItem *> rowsById;
    AvatarRequestTracker<Core::Snowflake> imageTracker;
    QStringList uploadQueue;
    QStringList uploadFailures;
    int uploadedCount = 0;
    bool uploading = false;
    bool listFetched = false;
    bool refetchPending = false;
    bool renaming = false;
    bool rebuildAfterRename = false;
};

} // namespace UI
} // namespace Acheron
