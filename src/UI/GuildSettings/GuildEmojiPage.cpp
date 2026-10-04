#include "GuildEmojiPage.hpp"

#include <QClipboard>
#include <QDragEnterEvent>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QHeaderView>
#include <QImage>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QMimeData>
#include <QMimeDatabase>
#include <QPainter>
#include <QPushButton>
#include <QRegularExpression>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QStyledItemDelegate>
#include <QTimer>
#include <QToolButton>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <algorithm>

#include "Core/ClientInstance.hpp"
#include "Core/ImageManager.hpp"
#include "Core/Theme/Icons.hpp"
#include "Discord/ApiError.hpp"
#include "Discord/CdnUrls.hpp"
#include "ImageUpload.hpp"

namespace Acheron {
namespace UI {

namespace {

constexpr int IdRole = Qt::UserRole;
constexpr int NameColumn = 1;
constexpr int UploaderColumn = 2;
constexpr int DeleteColumn = 3;
constexpr QSize EmojiSize(32, 32);
constexpr QSize AvatarSize(20, 20);
constexpr qint64 RawUploadLimit = 2 * 1024 * 1024;
constexpr qint64 EmojiMaxBytes = 256 * 1024;
constexpr int MaxNameLength = 32;

QString sanitizeName(const QString &name)
{
    static const QRegularExpression invalid(QStringLiteral("[^a-zA-Z0-9_]"));
    QString sanitized = QString(name).remove(invalid).left(MaxNameLength);
    while (sanitized.size() < 2)
        sanitized += QLatin1Char('_');
    return sanitized;
}

struct EmojiFile
{
    QByteArray bytes;
    QString mimeType;
    QByteArray originalMd5;
    QString failure;
};

EmojiFile prepareEmojiFile(const QString &path)
{
    EmojiFile file;
    file.mimeType = QMimeDatabase().mimeTypeForFile(path, QMimeDatabase::MatchContent).name();
    QFile source(path);
    if (!file.mimeType.startsWith(QLatin1String("image/")) || !source.open(QIODevice::ReadOnly)) {
        file.failure = GuildEmojiPage::tr("Wrong file type");
        return file;
    }
    file.bytes = source.readAll();
    file.originalMd5 = ImageUpload::md5Hex(file.bytes);
    if (file.bytes.size() <= RawUploadLimit)
        return file;

    static const QStringList animatedTypes{ QStringLiteral("image/gif"), QStringLiteral("image/webp"), QStringLiteral("image/avif") };
    QImage image;
    if (animatedTypes.contains(file.mimeType) || !image.loadFromData(file.bytes)) {
        file.failure = GuildEmojiPage::tr("File too large");
        return file;
    }
    if (image.width() > 128 || image.height() > 128)
        image = image.scaled(128, 128, Qt::KeepAspectRatio, Qt::SmoothTransformation);

    const QByteArray png = ImageUpload::encodePng(image);
    if (png.size() > EmojiMaxBytes) {
        file.failure = GuildEmojiPage::tr("File too large");
        return file;
    }
    file.bytes = png;
    file.mimeType = QStringLiteral("image/png");
    return file;
}

QString uploadFailureReason(const Core::Result<Discord::Emoji> &result)
{
    switch (result.code) {
    case Discord::ApiError::TooManyEmojis:
        return GuildEmojiPage::tr("Too many emoji");
    case Discord::ApiError::TooManyAnimatedEmojis:
        return GuildEmojiPage::tr("Too many animated emoji");
    case Discord::ApiError::InvalidFormBody:
    case Discord::ApiError::FileTooLarge:
        return GuildEmojiPage::tr("File too large");
    case Discord::ApiError::CannotResizeAnimated:
        return GuildEmojiPage::tr("Cannot resize animated image");
    default:
        return result.error;
    }
}

QPixmap faded(const QPixmap &pixmap)
{
    QPixmap result(pixmap.size());
    result.setDevicePixelRatio(pixmap.devicePixelRatio());
    result.fill(Qt::transparent);
    QPainter painter(&result);
    painter.setOpacity(0.35);
    painter.drawPixmap(0, 0, pixmap);
    return result;
}

class EmojiNameDelegate : public QStyledItemDelegate
{
public:
    using QStyledItemDelegate::QStyledItemDelegate;

    QWidget *createEditor(QWidget *parent, const QStyleOptionViewItem &option, const QModelIndex &index) const override
    {
        if (index.column() != NameColumn)
            return nullptr;
        QWidget *editor = QStyledItemDelegate::createEditor(parent, option, index);
        if (auto *line = qobject_cast<QLineEdit *>(editor))
            line->setMaxLength(MaxNameLength);
        return editor;
    }
};

QTreeWidget *makeTable(QWidget *parent)
{
    auto *table = new QTreeWidget(parent);
    table->setColumnCount(4);
    table->setHeaderLabels({ GuildEmojiPage::tr("Image"), GuildEmojiPage::tr("Name"), GuildEmojiPage::tr("Uploaded By"), QString() });
    table->setRootIsDecorated(false);
    table->setUniformRowHeights(true);
    table->setIconSize(EmojiSize);
    table->setSelectionMode(QAbstractItemView::SingleSelection);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setItemDelegate(new EmojiNameDelegate(table));
    table->setContextMenuPolicy(Qt::CustomContextMenu);
    table->header()->setStretchLastSection(false);
    table->header()->setSectionResizeMode(0, QHeaderView::Fixed);
    table->header()->setSectionResizeMode(NameColumn, QHeaderView::Stretch);
    table->header()->setSectionResizeMode(UploaderColumn, QHeaderView::Stretch);
    table->header()->setSectionResizeMode(DeleteColumn, QHeaderView::Fixed);
    table->setColumnWidth(0, 56);
    table->setColumnWidth(DeleteColumn, 40);
    table->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    return table;
}

} // namespace

GuildEmojiPage::GuildEmojiPage(Core::ClientInstance *instance, Core::ImageManager *images, Core::Snowflake guildId,
                               QWidget *parent)
    : GuildSettingsPage(instance, images, guildId, parent)
{
    setAcceptDrops(true);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(24, 20, 24, 16);
    layout->addWidget(makeTitle(tr("Emoji"), this));
    descriptionLabel = makeDescription(QString(), this);
    layout->addWidget(descriptionLabel);

    uploadArea = new QWidget(this);
    auto *uploadLayout = new QVBoxLayout(uploadArea);
    uploadLayout->setContentsMargins(0, 0, 0, 0);
    auto *uploadRow = new QHBoxLayout();
    uploadButton = new QPushButton(tr("Upload Emoji"), uploadArea);
    uploadRow->addWidget(uploadButton);
    statusLabel = new QLabel(uploadArea);
    statusLabel->setForegroundRole(QPalette::PlaceholderText);
    uploadRow->addWidget(statusLabel, 1);
    uploadLayout->addLayout(uploadRow);
    uploadLayout->addWidget(makeDescription(tr("Emojis are named after their files. Pick several files at once, or drop "
                                               "them onto this page, to upload them together."),
                                            uploadArea));
    layout->addWidget(uploadArea);

    auto *scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    auto *content = new QWidget(scroll);
    auto *tables = new QVBoxLayout(content);
    tables->setContentsMargins(0, 8, 12, 8);

    staticHeader = makeFieldLabel(tr("Emoji"), content);
    tables->addWidget(staticHeader);
    staticTable = makeTable(content);
    tables->addWidget(staticTable);
    tables->addSpacing(16);
    animatedHeader = makeFieldLabel(tr("Animated Emoji"), content);
    tables->addWidget(animatedHeader);
    animatedTable = makeTable(content);
    tables->addWidget(animatedTable);
    tables->addStretch();
    scroll->setWidget(content);
    layout->addWidget(scroll, 1);

    refetchThrottle = new QTimer(this);
    refetchThrottle->setSingleShot(true);
    refetchThrottle->setInterval(1000);
    connect(refetchThrottle, &QTimer::timeout, this, [this]() {
        if (!refetchPending)
            return;
        refetchPending = false;
        refetchThrottled();
    });

    connect(uploadButton, &QPushButton::clicked, this, &GuildEmojiPage::chooseUploads);
    for (QTreeWidget *table : { staticTable, animatedTable }) {
        connect(table, &QTreeWidget::itemChanged, this, &GuildEmojiPage::onItemChanged);
        connect(table, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem *row, int column) {
            if (column == NameColumn)
                startRename(row);
        });
        connect(table->itemDelegate(), &QAbstractItemDelegate::closeEditor, this, &GuildEmojiPage::onRenameEnded);
        connect(table, &QTreeWidget::customContextMenuRequested, this, [this, table](const QPoint &pos) { showContextMenu(table, pos); });
    }

    connect(instance->discord(), &Discord::Client::guildEmojisUpdated, this, [this](const Discord::GuildEmojisUpdate &event) {
        if (event.guildId.get() == this->guildId && isLoaded())
            refetchThrottled();
    });
    connect(images, &Core::ImageManager::imageFetched, this, [this](const QUrl &url, const QSize &size, const QPixmap &) {
        if (size != EmojiSize && size != AvatarSize)
            return;
        imageTracker.notify(url, [this](Core::Snowflake emojiId) {
            QTreeWidgetItem *row = rowsById.value(emojiId);
            const Discord::Emoji *emoji = findEmoji(emojiId);
            if (row && emoji)
                applyImages(row, *emoji);
        });
    });

    updatePermissions();
}

void GuildEmojiPage::load()
{
    fetchEmojis();
}

void GuildEmojiPage::updatePermissions()
{
    uploadArea->setVisible(canUpload() || uploading);
    if (!uploading)
        uploadButton->setEnabled(canUpload());
    for (QTreeWidget *table : { staticTable, animatedTable })
        table->setColumnHidden(DeleteColumn, !canUpload());
    if (listFetched)
        rebuildTables();
    if (isLoaded())
        fetchEmojis();
}

void GuildEmojiPage::fetchEmojis()
{
    if (!instance)
        return;

    QPointer<GuildEmojiPage> self(this);
    instance->discord()->fetchGuildEmojis(guildId, [self](const Core::Result<QList<Discord::Emoji>> &result) {
        if (!self)
            return;
        if (!result.success()) {
            self->showActionError(result.error);
            return;
        }
        self->listFetched = true;
        self->emojis = QList<Discord::Emoji>(result.value->crbegin(), result.value->crend());
        self->rebuildTables();
    });
}

void GuildEmojiPage::refetchThrottled()
{
    if (refetchThrottle->isActive()) {
        refetchPending = true;
        return;
    }
    fetchEmojis();
    refetchThrottle->start();
}

int GuildEmojiPage::slotsPerType() const
{
    const auto guild = instance ? instance->getGuild(guildId) : std::nullopt;
    if (!guild)
        return 50;

    int additional = 0;
    if (guild->premiumFeatures.hasValue() && guild->premiumFeatures->additionalEmojiSlots.hasValue()) {
        additional = guild->premiumFeatures->additionalEmojiSlots.get();
    } else {
        switch (guild->premiumTier.valueOr(Discord::PremiumTier::NONE)) {
        case Discord::PremiumTier::TIER_1:
            additional = 50;
            break;
        case Discord::PremiumTier::TIER_2:
            additional = 100;
            break;
        case Discord::PremiumTier::TIER_3:
            additional = 200;
            break;
        default:
            break;
        }
    }
    return qMax(guild->hasFeature(QStringLiteral("MORE_EMOJI")) ? 200 : 50, 50 + additional);
}

bool GuildEmojiPage::isPremiumEmoji(const Discord::Emoji &emoji) const
{
    if (!emoji.roles.hasValue() || emoji.roles->isEmpty() || !instance)
        return false;
    for (const auto &role : instance->getRolesForGuild(guildId))
        if (role.tags.hasValue() && role.tags->subscriptionListingId.hasValue() && emoji.roles->contains(role.id.get()))
            return true;
    return false;
}

bool GuildEmojiPage::canUpload() const
{
    return hasPermission(Discord::Permission::CREATE_EXPRESSIONS) || hasPermission(Discord::Permission::MANAGE_EXPRESSIONS);
}

bool GuildEmojiPage::canManage(const Discord::Emoji &emoji) const
{
    if (hasPermission(Discord::Permission::MANAGE_EXPRESSIONS))
        return true;
    return hasPermission(Discord::Permission::CREATE_EXPRESSIONS) && emoji.user.hasValue() && emoji.user->id.get() == selfId();
}

Discord::Emoji *GuildEmojiPage::findEmoji(Core::Snowflake emojiId)
{
    for (auto &emoji : emojis)
        if (emoji.id.hasValue() && emoji.id.get() == emojiId)
            return &emoji;
    return nullptr;
}

void GuildEmojiPage::rebuildTables()
{
    if (renaming) {
        rebuildAfterRename = true;
        return;
    }

    QList<Discord::Emoji> still;
    QList<Discord::Emoji> animated;
    for (const auto &emoji : emojis) {
        if (isPremiumEmoji(emoji))
            continue;
        (emoji.isAnimated() ? animated : still).append(emoji);
    }

    const int slotCount = slotsPerType();
    descriptionLabel->setText(tr("Add up to %1 custom emoji that anyone can use in this server. Animated GIF emoji may be used by members with Discord Nitro.")
                                      .arg(slotCount));

    auto slotText = [](int available) {
        if (available <= 0)
            return tr("no slots available");
        if (available == 1)
            return tr("1 slot available");
        return tr("%1 slots available").arg(available);
    };
    staticHeader->setText(tr("Emoji — %1").arg(slotText(slotCount - int(still.size()))));
    animatedHeader->setText(tr("Animated Emoji — %1").arg(slotText(slotCount - int(animated.size()))));

    rowsById.clear();
    imageTracker.clear();
    fillTable(staticTable, still);
    fillTable(animatedTable, animated);

    const bool anyUploader = std::any_of(emojis.cbegin(), emojis.cend(), [](const Discord::Emoji &emoji) { return emoji.user.hasValue(); });
    for (QTreeWidget *table : { staticTable, animatedTable })
        table->setColumnHidden(UploaderColumn, !anyUploader);
}

void GuildEmojiPage::fillTable(QTreeWidget *table, const QList<Discord::Emoji> &list)
{
    QSignalBlocker blocker(table);
    table->clear();

    auto fitHeight = [table]() {
        const int rows = table->topLevelItemCount();
        const int rowHeight = rows > 0 ? table->sizeHintForRow(0) : 0;
        table->setFixedHeight(table->header()->sizeHint().height() + rows * rowHeight + 2 * table->frameWidth());
    };

    if (list.isEmpty()) {
        auto *item = new QTreeWidgetItem(table, { QString(), tr("None") });
        item->setFlags(Qt::NoItemFlags);
        fitHeight();
        return;
    }

    for (const auto &emoji : list) {
        const Core::Snowflake emojiId = emoji.id.get();
        auto *item = new QTreeWidgetItem(table);
        item->setData(0, IdRole, QVariant::fromValue<quint64>(emojiId));
        item->setText(NameColumn, emoji.name.get());
        if (emoji.available.hasValue() && !emoji.available.get())
            item->setToolTip(0, tr("Requires higher Server Boost Level"));
        if (emoji.user.hasValue())
            item->setText(UploaderColumn, emoji.user->tag());

        const bool manageable = canManage(emoji);
        Qt::ItemFlags flags = Qt::ItemIsEnabled | Qt::ItemIsSelectable;
        if (manageable)
            flags |= Qt::ItemIsEditable;
        item->setFlags(flags);

        rowsById.insert(emojiId, item);
        applyImages(item, emoji);

        if (manageable) {
            auto *deleteButton = new QToolButton(table);
            deleteButton->setAutoRaise(true);
            deleteButton->setIcon(Core::Theme::Icons::icon(Core::Theme::Icons::Name::Trash, Core::Theme::Token::ChatError));
            deleteButton->setToolTip(tr("Delete Emoji"));
            connect(deleteButton, &QToolButton::clicked, this, [this, emojiId]() { deleteEmoji(emojiId); });
            table->setItemWidget(item, DeleteColumn, deleteButton);
        }
    }
    fitHeight();
}

void GuildEmojiPage::applyImages(QTreeWidgetItem *row, const Discord::Emoji &emoji)
{
    if (!instance)
        return;

    const Core::Snowflake emojiId = emoji.id.get();
    const QPixmap still = imageTracker.fetch(images, Discord::Cdn::emoji(emojiId, 64), EmojiSize, emojiId, instance->accountId());
    const bool unavailable = emoji.available.hasValue() && !emoji.available.get();
    row->setIcon(0, QIcon(unavailable ? faded(still) : still));

    if (emoji.user.hasValue()) {
        const QUrl avatarUrl = instance->users()->getAvatarUrl(emoji.user.get(), guildId, 32);
        const QPixmap avatar = imageTracker.fetch(images, avatarUrl, AvatarSize, emojiId, instance->accountId());
        row->setIcon(UploaderColumn, QIcon(roundedPixmap(avatar, AvatarSize.width(), AvatarSize.width() / 2.0)));
    }
}

void GuildEmojiPage::startRename(QTreeWidgetItem *row)
{
    if (!row->flags().testFlag(Qt::ItemIsEditable))
        return;
    renaming = true;
    row->treeWidget()->editItem(row, NameColumn);
}

void GuildEmojiPage::onRenameEnded()
{
    renaming = false;
    if (!rebuildAfterRename)
        return;
    rebuildAfterRename = false;
    rebuildTables();
}

void GuildEmojiPage::onItemChanged(QTreeWidgetItem *item, int column)
{
    if (column != NameColumn || !instance)
        return;

    const Core::Snowflake emojiId(item->data(0, IdRole).toULongLong());
    Discord::Emoji *emoji = findEmoji(emojiId);
    if (!emoji)
        return;

    const QString previous = emoji->name.get();
    const QString typed = item->text(NameColumn);
    const QString name = typed.isEmpty() ? previous : sanitizeName(typed);
    {
        QSignalBlocker blocker(item->treeWidget());
        item->setText(NameColumn, name);
    }
    if (name == previous)
        return;
    emoji->name = name;

    QPointer<GuildEmojiPage> self(this);
    instance->discord()->renameGuildEmoji(guildId, emojiId, name, [self](const Core::Result<void> &result) {
        if (!self || result.success())
            return;
        self->showActionError(result.error);
        self->fetchEmojis();
    });
}

void GuildEmojiPage::deleteEmoji(Core::Snowflake emojiId)
{
    if (!instance)
        return;

    emojis.erase(std::remove_if(emojis.begin(), emojis.end(),
                                [emojiId](const Discord::Emoji &emoji) { return emoji.id.hasValue() && emoji.id.get() == emojiId; }),
                 emojis.end());
    rebuildTables();

    QPointer<GuildEmojiPage> self(this);
    instance->discord()->deleteGuildEmoji(guildId, emojiId, [self](const Core::Result<void> &result) {
        if (!self || result.success())
            return;
        self->showActionError(result.error);
        self->fetchEmojis();
    });
}

void GuildEmojiPage::showContextMenu(QTreeWidget *table, const QPoint &pos)
{
    QTreeWidgetItem *item = table->itemAt(pos);
    if (!item)
        return;
    const Discord::Emoji *emoji = findEmoji(Core::Snowflake(item->data(0, IdRole).toULongLong()));
    if (!emoji)
        return;

    const Core::Snowflake emojiId = emoji->id.get();
    const bool animated = emoji->isAnimated();
    QMenu menu(this);
    if (canManage(*emoji)) {
        menu.addAction(tr("Rename"), this, [this, emojiId]() {
            if (QTreeWidgetItem *row = rowsById.value(emojiId))
                startRename(row);
        });
        menu.addAction(tr("Delete Emoji"), this, [this, emojiId]() { deleteEmoji(emojiId); });
        menu.addSeparator();
    }
    menu.addAction(tr("Copy Emoji URL"), this, [emojiId, animated]() {
        QGuiApplication::clipboard()->setText(Discord::Cdn::emoji(emojiId, 128, animated).toString());
    });
    menu.addAction(tr("Copy Emoji ID"), this, [emojiId]() { QGuiApplication::clipboard()->setText(QString::number(quint64(emojiId))); });
    menu.exec(table->viewport()->mapToGlobal(pos));
}

void GuildEmojiPage::chooseUploads()
{
    chooseFiles(this, tr("Upload Emoji"), ImageUpload::fileFilter(), true, [this](const QStringList &paths) { upload(paths); });
}

void GuildEmojiPage::dragEnterEvent(QDragEnterEvent *event)
{
    if (event->mimeData()->hasUrls() && canUpload())
        event->acceptProposedAction();
}

void GuildEmojiPage::dropEvent(QDropEvent *event)
{
    QStringList paths;
    for (const QUrl &url : event->mimeData()->urls())
        if (url.isLocalFile())
            paths.append(url.toLocalFile());
    upload(paths);
    event->acceptProposedAction();
}

void GuildEmojiPage::upload(const QStringList &paths)
{
    if (paths.isEmpty())
        return;
    uploadQueue += paths;
    if (uploading)
        return;

    uploading = true;
    uploadFailures.clear();
    uploadedCount = 0;
    uploadButton->setEnabled(false);
    uploadNext();
}

void GuildEmojiPage::uploadNext()
{
    if (uploadQueue.isEmpty() || !instance) {
        finishUploads();
        return;
    }

    const QString path = uploadQueue.takeFirst();
    const QString fileName = QFileInfo(path).fileName();
    statusLabel->setText(tr("Uploading %1…").arg(fileName));

    const EmojiFile file = prepareEmojiFile(path);
    if (!file.failure.isEmpty()) {
        uploadFailures.append(QStringLiteral("%1: %2").arg(fileName, file.failure));
        uploadNext();
        return;
    }

    const QString name = sanitizeName(fileName.section(QLatin1Char('.'), 0, 0));
    QPointer<GuildEmojiPage> self(this);
    instance->discord()->createGuildEmoji(guildId, name, ImageUpload::dataUri(file.bytes, file.mimeType), file.originalMd5,
                                          [self, fileName](const Core::Result<Discord::Emoji> &result) {
                                              if (!self)
                                                  return;
                                              if (result.success())
                                                  self->uploadedCount++;
                                              else
                                                  self->uploadFailures.append(QStringLiteral("%1: %2").arg(fileName, uploadFailureReason(result)));
                                              self->uploadNext();
                                          });
}

void GuildEmojiPage::finishUploads()
{
    uploading = false;
    uploadButton->setEnabled(canUpload());
    uploadArea->setVisible(canUpload());
    statusLabel->setText(uploadedCount > 0 ? tr("Emoji uploaded") : QString());

    if (!uploadFailures.isEmpty()) {
        auto *box = new QMessageBox(QMessageBox::Warning, tr("Failed to upload files"), uploadFailures.join(QLatin1Char('\n')), QMessageBox::NoButton, this);
        box->addButton(tr("Got It"), QMessageBox::AcceptRole);
        box->setAttribute(Qt::WA_DeleteOnClose);
        box->open();
    }
    if (uploadedCount > 0)
        fetchEmojis();
}

} // namespace UI
} // namespace Acheron
