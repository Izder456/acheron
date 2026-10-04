#include "GuildStickersPage.hpp"

#include <QClipboard>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFileInfo>
#include <QFormLayout>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QImage>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMimeDatabase>
#include <QPushButton>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <algorithm>
#include <functional>

#include "Core/ClientInstance.hpp"
#include "Core/ImageManager.hpp"
#include "Core/Theme/Icons.hpp"
#include "Core/Theme/Manager.hpp"
#include "Discord/CdnUrls.hpp"
#include "ImageUpload.hpp"

namespace Acheron {
namespace UI {

namespace {

constexpr int IdRole = Qt::UserRole;
constexpr int UploaderColumn = 4;
constexpr int PreviewPx = 48;
constexpr QSize PreviewSize(PreviewPx, PreviewPx);
constexpr QSize AvatarSize(20, 20);
constexpr qint64 MaxStickerBytes = 512 * 1024;
constexpr int NameMin = 2;
constexpr int NameMax = 30;
constexpr int DescriptionMax = 100;

bool isApng(const QByteArray &bytes)
{
    const int actl = bytes.indexOf("acTL");
    const int idat = bytes.indexOf("IDAT");
    return actl >= 0 && (idat < 0 || actl < idat);
}

struct StickerFile
{
    Discord::FileUpload upload;
    QByteArray originalMd5;
    QString error;
};

StickerFile prepareStickerFile(const QString &path, bool allowLottie)
{
    StickerFile result;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        result.error = GuildStickersPage::tr("Unsupported file format");
        return result;
    }
    QByteArray bytes = file.readAll();
    result.originalMd5 = ImageUpload::md5Hex(bytes);
    QString mimeType = QMimeDatabase().mimeTypeForFileNameAndData(path, bytes).name();
    if (mimeType == QLatin1String("image/vnd.mozilla.apng"))
        mimeType = QStringLiteral("image/apng");

    static const QStringList accepted{ QStringLiteral("application/json"), QStringLiteral("image/png"),
                                       QStringLiteral("image/apng"), QStringLiteral("image/gif"),
                                       QStringLiteral("image/jpeg") };
    if (!accepted.contains(mimeType) || (mimeType == QLatin1String("application/json") && !allowLottie)) {
        result.error = GuildStickersPage::tr("Unsupported file format");
        return result;
    }

    const bool convert = mimeType == QLatin1String("image/jpeg") ||
                         (mimeType == QLatin1String("image/png") && bytes.size() > MaxStickerBytes && !isApng(bytes));
    if (convert) {
        QImage image;
        if (!image.loadFromData(bytes)) {
            result.error = GuildStickersPage::tr("Invalid sticker file");
            return result;
        }
        bytes = ImageUpload::encodePng(image.scaled(320, 320, Qt::KeepAspectRatio, Qt::SmoothTransformation));
        mimeType = QStringLiteral("image/png");
    }

    if (bytes.size() > MaxStickerBytes) {
        result.error = GuildStickersPage::tr("That sticker was too big! Stickers must be under 512KB.");
        return result;
    }

    result.upload.filename = QFileInfo(path).fileName();
    result.upload.data = bytes;
    result.upload.mimeType = mimeType;
    return result;
}

class StickerDialog : public QDialog
{
public:
    StickerDialog(bool editing, bool allowLottie, QWidget *parent)
        : QDialog(parent), allowLottie(allowLottie), editing(editing)
    {
        setWindowTitle(editing ? tr("Edit Sticker") : tr("Upload a file"));
        auto *layout = new QVBoxLayout(this);
        layout->addWidget(new QLabel(allowLottie ? tr("File should be APNG, JPG, PNG, Lottie, or GIF (512KB max)")
                                                 : tr("File should be APNG, JPG, PNG, or GIF (512KB max)"),
                                     this));

        auto *form = new QFormLayout();
        auto *fileRow = new QHBoxLayout();
        fileLabel = new QLabel(editing ? tr("This cannot be edited") : tr("Choose a file"), this);
        fileRow->addWidget(fileLabel, 1);
        auto *browse = new QPushButton(tr("Browse"), this);
        browse->setEnabled(!editing);
        fileRow->addWidget(browse);
        form->addRow(tr("File"), fileRow);

        emojiEdit = new QLineEdit(this);
        emojiEdit->setPlaceholderText(tr("An emoji, e.g. 😺"));
        form->addRow(tr("Related Emoji"), emojiEdit);

        nameEdit = new QLineEdit(this);
        nameEdit->setMaxLength(NameMax);
        nameEdit->setPlaceholderText(tr("ex: cat hug"));
        form->addRow(tr("Sticker Name"), nameEdit);

        descriptionEdit = new QLineEdit(this);
        descriptionEdit->setMaxLength(DescriptionMax);
        descriptionEdit->setPlaceholderText(tr("See our Help Center for tips on writing sticker descriptions."));
        form->addRow(tr("Description"), descriptionEdit);
        layout->addLayout(form);

        errorLabel = new QLabel(this);
        errorLabel->setWordWrap(true);
        errorLabel->setStyleSheet(QStringLiteral("color: %1;").arg(Core::Theme::Manager::instance().color(Core::Theme::Token::ChatError).name()));
        errorLabel->hide();
        layout->addWidget(errorLabel);

        buttons = new QDialogButtonBox(this);
        submit = buttons->addButton(editing ? tr("Update") : tr("Upload"), QDialogButtonBox::AcceptRole);
        buttons->addButton(tr("Never Mind"), QDialogButtonBox::RejectRole);
        layout->addWidget(buttons);

        connect(browse, &QPushButton::clicked, this, [this]() {
            GuildSettingsPage::chooseFiles(this, tr("Upload a file"), tr("Stickers (*.png *.apng *.gif *.jpg *.jpeg *.json)"),
                                           false, [this](const QStringList &paths) {
                                               file = prepareStickerFile(paths.first(), this->allowLottie);
                                               fileLabel->setText(file.error.isEmpty() ? file.upload.filename : file.error);
                                               updateSubmit();
                                           });
        });
        connect(buttons, &QDialogButtonBox::accepted, this, [this]() {
            errorLabel->hide();
            setBusy(true);
            onSubmit();
        });
        connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
        for (QLineEdit *edit : { emojiEdit, nameEdit, descriptionEdit })
            connect(edit, &QLineEdit::textChanged, this, [this]() { updateSubmit(); });
        updateSubmit();
    }

    void setValues(const QString &name, const QString &tags, const QString &description)
    {
        nameEdit->setText(name);
        emojiEdit->setText(tags);
        descriptionEdit->setText(description);
    }

    void showError(const QString &error)
    {
        errorLabel->setText(error);
        errorLabel->show();
        setBusy(false);
    }

    void setBusy(bool busy)
    {
        this->busy = busy;
        updateSubmit();
    }

    [[nodiscard]] QString name() const { return nameEdit->text().trimmed(); }
    [[nodiscard]] QString tags() const { return emojiEdit->text().trimmed(); }
    [[nodiscard]] QString description() const { return descriptionEdit->text().trimmed(); }
    [[nodiscard]] const StickerFile &chosenFile() const { return file; }

    std::function<void()> onSubmit;

private:
    void updateSubmit()
    {
        const int descriptionLength = description().size();
        const bool hasFile = editing || (!file.upload.data.isEmpty() && file.error.isEmpty());
        submit->setEnabled(!busy && name().size() >= NameMin && !tags().isEmpty() && hasFile && (descriptionLength == 0 || descriptionLength >= 2));
    }

    bool allowLottie;
    bool editing;
    bool busy = false;
    StickerFile file;
    QLabel *fileLabel;
    QLineEdit *emojiEdit;
    QLineEdit *nameEdit;
    QLineEdit *descriptionEdit;
    QLabel *errorLabel;
    QDialogButtonBox *buttons;
    QPushButton *submit;
};

} // namespace

GuildStickersPage::GuildStickersPage(Core::ClientInstance *instance, Core::ImageManager *images,
                                     Core::Snowflake guildId, QWidget *parent)
    : GuildSettingsPage(instance, images, guildId, parent)
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(24, 20, 24, 16);
    layout->addWidget(makeTitle(tr("Stickers"), this));
    uploadHint = makeDescription(tr("Stickers can be static (JPG, PNG) or animated (APNG, GIF). Stickers must be exactly "
                                    "320 x 320 pixels and no larger than 512KB. We will automatically resize static JPG, "
                                    "PNG and animated GIF stickers for you."),
                                 this);
    layout->addWidget(uploadHint);

    auto *actions = new QHBoxLayout();
    uploadButton = new QPushButton(tr("Upload Sticker"), this);
    actions->addWidget(uploadButton);
    slotsLabel = new QLabel(this);
    slotsLabel->setForegroundRole(QPalette::PlaceholderText);
    actions->addWidget(slotsLabel, 1);
    layout->addLayout(actions);

    table = new QTreeWidget(this);
    table->setColumnCount(5);
    table->setHeaderLabels({ tr("Sticker"), tr("Name"), tr("Related Emoji"), tr("Description"), tr("Uploaded By") });
    table->setRootIsDecorated(false);
    table->setUniformRowHeights(true);
    table->setIconSize(QSize(PreviewPx, PreviewPx));
    table->setContextMenuPolicy(Qt::CustomContextMenu);
    table->header()->setSectionResizeMode(3, QHeaderView::Stretch);
    table->header()->setStretchLastSection(false);
    table->setColumnWidth(0, PreviewPx + 24);
    layout->addWidget(table, 1);

    connect(uploadButton, &QPushButton::clicked, this, &GuildStickersPage::openUploadDialog);
    connect(table, &QTreeWidget::customContextMenuRequested, this, &GuildStickersPage::showContextMenu);
    connect(table, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem *item) {
        openEditDialog(Core::Snowflake(item->data(0, IdRole).toULongLong()));
    });

    connect(instance->discord(), &Discord::Client::guildStickersUpdated, this, [this](const Discord::GuildStickersUpdate &event) {
        if (event.guildId.get() != this->guildId)
            return;
        QList<Discord::Sticker> updated = event.stickers.get();
        for (auto &sticker : updated) {
            const Discord::Sticker *known = findSticker(sticker.id.get());
            if (!sticker.user.hasValue() && known && known->user.hasValue())
                sticker.user = known->user.get();
        }
        stickers = updated;
        rebuildTable();
    });
    connect(images, &Core::ImageManager::imageFetched, this, [this](const QUrl &url, const QSize &size, const QPixmap &) {
        if (size != PreviewSize && size != AvatarSize)
            return;
        imageTracker.notify(url, [this](Core::Snowflake stickerId) {
            QTreeWidgetItem *row = rowsById.value(stickerId);
            const Discord::Sticker *sticker = findSticker(stickerId);
            if (row && sticker)
                applyImages(row, *sticker);
        });
    });

    updatePermissions();
}

void GuildStickersPage::load()
{
    fetchStickers();
}

void GuildStickersPage::updatePermissions()
{
    const bool canUpload = hasPermission(Discord::Permission::CREATE_EXPRESSIONS);
    uploadHint->setVisible(canUpload);
    uploadButton->setVisible(canUpload);
    rebuildTable();
    if (isLoaded())
        fetchStickers();
}

void GuildStickersPage::fetchStickers()
{
    if (!instance)
        return;

    QPointer<GuildStickersPage> self(this);
    instance->discord()->fetchGuildStickers(guildId, [self](const Core::Result<QList<Discord::Sticker>> &result) {
        if (!self)
            return;
        if (!result.success()) {
            self->showActionError(result.error);
            return;
        }
        self->stickers = *result.value;
        self->rebuildTable();
    });
}

int GuildStickersPage::totalSlots() const
{
    const auto guild = instance ? instance->getGuild(guildId) : std::nullopt;
    if (!guild)
        return 5;
    if (guild->hasFeature(QStringLiteral("MORE_STICKERS")))
        return 120;
    switch (guild->premiumTier.valueOr(Discord::PremiumTier::NONE)) {
    case Discord::PremiumTier::TIER_1:
        return 15;
    case Discord::PremiumTier::TIER_2:
        return 30;
    case Discord::PremiumTier::TIER_3:
        return 60;
    default:
        return 5;
    }
}

bool GuildStickersPage::canManage(const Discord::Sticker &sticker) const
{
    if (hasPermission(Discord::Permission::MANAGE_EXPRESSIONS))
        return true;
    return hasPermission(Discord::Permission::CREATE_EXPRESSIONS) && sticker.user.hasValue() && sticker.user->id.get() == selfId();
}

const Discord::Sticker *GuildStickersPage::findSticker(Core::Snowflake stickerId) const
{
    for (const auto &sticker : stickers)
        if (sticker.id.get() == stickerId)
            return &sticker;
    return nullptr;
}

void GuildStickersPage::rebuildTable()
{
    const int slotCount = totalSlots();
    const int available = qMax(0, slotCount - static_cast<int>(stickers.size()));
    slotsLabel->setText(tr("%1 of %2 slots available").arg(available).arg(slotCount));
    uploadButton->setEnabled(available > 0 && hasPermission(Discord::Permission::CREATE_EXPRESSIONS));

    table->clear();
    rowsById.clear();
    imageTracker.clear();
    for (const auto &sticker : stickers) {
        const Core::Snowflake stickerId = sticker.id.get();
        auto *item = new QTreeWidgetItem(table);
        item->setData(0, IdRole, QVariant::fromValue<quint64>(stickerId));
        if (sticker.formatType.get() == Discord::StickerFormatType::LOTTIE)
            item->setText(0, tr("Lottie"));
        item->setText(1, sticker.name.get());
        item->setText(2, sticker.tags.valueOr(QString()));
        item->setText(3, sticker.description.valueOr(QString()));
        if (sticker.user.hasValue())
            item->setText(UploaderColumn, sticker.user->tag());
        rowsById.insert(stickerId, item);
        applyImages(item, sticker);
    }
    table->setColumnHidden(UploaderColumn, std::none_of(stickers.cbegin(), stickers.cend(), [](const Discord::Sticker &sticker) {
                               return sticker.user.hasValue();
                           }));
}

void GuildStickersPage::applyImages(QTreeWidgetItem *row, const Discord::Sticker &sticker)
{
    if (!instance)
        return;

    const Core::Snowflake stickerId = sticker.id.get();
    const QUrl stillUrl = Discord::Cdn::stickerStill(stickerId, sticker.formatType.get(), Discord::Cdn::stickerAssetPx(PreviewPx, devicePixelRatioF()));
    if (!stillUrl.isEmpty())
        row->setIcon(0, QIcon(imageTracker.fetch(images, stillUrl, PreviewSize, stickerId, instance->accountId())));
    if (sticker.user.hasValue()) {
        const QUrl avatarUrl = instance->users()->getAvatarUrl(sticker.user.get(), guildId, 32);
        const QPixmap avatar = imageTracker.fetch(images, avatarUrl, AvatarSize, stickerId, instance->accountId());
        row->setIcon(UploaderColumn, QIcon(roundedPixmap(avatar, AvatarSize.width(), AvatarSize.width() / 2.0)));
    }
}

void GuildStickersPage::openUploadDialog()
{
    if (!instance)
        return;
    const auto guild = instance->getGuild(guildId);
    const bool allowLottie = guild && (guild->hasFeature(QStringLiteral("PARTNERED")) || guild->hasFeature(QStringLiteral("VERIFIED")));

    auto *dialog = new StickerDialog(false, allowLottie, this);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    QPointer<StickerDialog> dialogGuard(dialog);
    QPointer<GuildStickersPage> self(this);
    dialog->onSubmit = [this, self, dialogGuard]() {
        if (!dialogGuard || !instance)
            return;
        instance->discord()->createGuildSticker(
                guildId, dialogGuard->name(), dialogGuard->tags(), dialogGuard->description(),
                dialogGuard->chosenFile().upload, dialogGuard->chosenFile().originalMd5,
                [self, dialogGuard](const Core::Result<Discord::Sticker> &result) {
                    if (!result.success()) {
                        if (dialogGuard)
                            dialogGuard->showError(result.error);
                        return;
                    }
                    if (dialogGuard)
                        dialogGuard->accept();
                    if (self)
                        self->fetchStickers();
                });
    };
    dialog->open();
}

void GuildStickersPage::openEditDialog(Core::Snowflake stickerId)
{
    const Discord::Sticker *sticker = findSticker(stickerId);
    if (!sticker || !canManage(*sticker) || !instance)
        return;

    auto *dialog = new StickerDialog(true, false, this);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setValues(sticker->name.get(), sticker->tags.valueOr(QString()), sticker->description.valueOr(QString()));
    QPointer<StickerDialog> dialogGuard(dialog);
    dialog->onSubmit = [this, stickerId, dialogGuard]() {
        if (!dialogGuard || !instance)
            return;
        instance->discord()->modifyGuildSticker(guildId, stickerId, dialogGuard->name(), dialogGuard->tags(),
                                                dialogGuard->description(), [dialogGuard](const Core::Result<void> &result) {
                                                    if (!dialogGuard)
                                                        return;
                                                    if (result.success())
                                                        dialogGuard->accept();
                                                    else
                                                        dialogGuard->showError(result.error);
                                                });
    };
    dialog->open();
}

void GuildStickersPage::deleteSticker(Core::Snowflake stickerId)
{
    if (!instance)
        return;
    QPointer<GuildStickersPage> self(this);
    instance->discord()->deleteGuildSticker(guildId, stickerId, [self](const Core::Result<void> &result) {
        if (self && !result.success())
            self->showActionError(result.error);
    });
}

void GuildStickersPage::showContextMenu(const QPoint &pos)
{
    QTreeWidgetItem *item = table->itemAt(pos);
    if (!item)
        return;
    const Core::Snowflake stickerId(item->data(0, IdRole).toULongLong());
    const Discord::Sticker *sticker = findSticker(stickerId);
    if (!sticker)
        return;

    QMenu menu(this);
    if (canManage(*sticker)) {
        menu.addAction(tr("Edit"), this, [this, stickerId]() { openEditDialog(stickerId); });
        QAction *remove = menu.addAction(tr("Remove"), this, [this, stickerId]() { deleteSticker(stickerId); });
        remove->setIcon(Core::Theme::Icons::icon(Core::Theme::Icons::Name::Trash, Core::Theme::Token::ChatError));
        menu.addSeparator();
    }
    menu.addAction(tr("Copy Sticker ID"), this, [stickerId]() { QGuiApplication::clipboard()->setText(QString::number(quint64(stickerId))); });
    menu.exec(table->viewport()->mapToGlobal(pos));
}

} // namespace UI
} // namespace Acheron
