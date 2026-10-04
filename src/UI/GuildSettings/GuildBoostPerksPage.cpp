#include "GuildBoostPerksPage.hpp"

#include <QCheckBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QVBoxLayout>

#include "Core/ClientInstance.hpp"
#include "Core/ImageManager.hpp"
#include "Discord/CdnUrls.hpp"
#include "ImageUpload.hpp"
#include "UnsavedChangesBar.hpp"

namespace Acheron {
namespace UI {

namespace {

constexpr QSize PreviewSize(320, 180);
constexpr qint64 MaxImageBytes = 10 * 1024 * 1024;

} // namespace

GuildBoostPerksPage::GuildBoostPerksPage(Core::ClientInstance *instance, Core::ImageManager *images,
                                         Core::Snowflake guildId, QWidget *parent)
    : GuildSettingsPage(instance, images, guildId, parent)
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(24, 20, 24, 16);
    layout->addWidget(makeTitle(tr("Boost Perks"), this));

    auto *scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    auto *content = new QWidget(scroll);
    content->setMaximumWidth(680);
    auto *form = new QVBoxLayout(content);
    form->setContentsMargins(0, 8, 12, 8);
    form->setSpacing(6);

    progressBarCheck = new QCheckBox(tr("Show Boost progress bar"), content);
    form->addWidget(progressBarCheck);
    form->addWidget(makeDescription(tr("Servers with the Boost progress bar enabled get more Boosts. This progress bar will "
                                       "display in your channel list, attached to your server name (or server banner if you have one set)."),
                                    content));
    form->addSpacing(16);

    banner.md5Key = QStringLiteral("GUILD_BANNER");
    banner.feature = QStringLiteral("BANNER");
    addImageSlot(banner, tr("Server Banner Background"), tr("This image will display at the top of your channels list."),
                 tr("The recommended minimum size is 960x540 and recommended aspect ratio is 16:9."),
                 tr("Unlocks at Boost Level 2."), content, form);
    form->addSpacing(16);

    splash.md5Key = QStringLiteral("GUILD_INVITE_SPLASH");
    splash.feature = QStringLiteral("INVITE_SPLASH");
    addImageSlot(splash, tr("Server Invite Background"),
                 tr("This image will display when your server invite is viewed in a browser, as well as in invite confirmation screens and Server Onboarding."),
                 tr("The recommended minimum size is 1920x1080 and recommended aspect ratio is 16:9."), tr("Unlocks at Boost Level 1."), content, form);
    form->addStretch();

    scroll->setWidget(content);
    layout->addWidget(scroll, 1);

    saveBar = new UnsavedChangesBar(this);
    saveBar->hide();
    connect(saveBar, &UnsavedChangesBar::resetClicked, this, &GuildBoostPerksPage::showSaved);
    connect(saveBar, &UnsavedChangesBar::saveClicked, this, &GuildBoostPerksPage::save);
    layout->addWidget(saveBar);

    connect(progressBarCheck, &QCheckBox::toggled, this, &GuildBoostPerksPage::updateSaveBar);
    connect(instance, &Core::ClientInstance::guildUpdated, this, &GuildBoostPerksPage::onGuildUpdated);
    connect(images, &Core::ImageManager::imageFetched, this, [this](const QUrl &url, const QSize &, const QPixmap &) {
        if (url == savedUrl(banner) || url == savedUrl(splash))
            updatePreviews();
    });

    updatePermissions();
}

void GuildBoostPerksPage::updatePermissions()
{
    const bool manage = canManage();
    const auto guild = instance ? instance->getGuild(guildId) : std::nullopt;
    progressBarCheck->setEnabled(manage);
    for (ImageSlot *slot : { &banner, &splash }) {
        const bool unlocked = guild && guild->hasFeature(slot->feature);
        slot->recommendation->setVisible(manage);
        slot->uploadButton->setVisible(manage);
        slot->uploadButton->setEnabled(unlocked);
        slot->lockedNote->setVisible(!unlocked);
    }
    updatePreviews();
}

void GuildBoostPerksPage::onGuildUpdated(const Discord::Guild &guild)
{
    if (guild.id.get() != guildId)
        return;
    updatePermissions();
    if (saving)
        return;
    const bool saved = guild.premiumProgressBarEnabled.valueOr(false);
    if (progressBarCheck->isChecked() == shownProgressBar) {
        QSignalBlocker blocker(progressBarCheck);
        progressBarCheck->setChecked(saved);
    }
    shownProgressBar = saved;
    updateSaveBar();
}

bool GuildBoostPerksPage::canManage() const
{
    return hasPermission(Discord::Permission::MANAGE_GUILD);
}

void GuildBoostPerksPage::addImageSlot(ImageSlot &slot, const QString &title, const QString &description,
                                       const QString &recommendation, const QString &lockedNote, QWidget *parent,
                                       QVBoxLayout *form)
{
    form->addWidget(makeFieldLabel(title, parent));
    form->addWidget(makeDescription(description, parent));
    slot.recommendation = makeDescription(recommendation, parent);
    form->addWidget(slot.recommendation);

    slot.preview = new QLabel(parent);
    slot.preview->setFixedSize(PreviewSize);
    slot.preview->setAlignment(Qt::AlignCenter);
    slot.preview->setFrameShape(QFrame::StyledPanel);
    form->addWidget(slot.preview);

    auto *buttons = new QHBoxLayout();
    slot.uploadButton = new QPushButton(tr("Upload Image"), parent);
    buttons->addWidget(slot.uploadButton);
    slot.removeButton = new QPushButton(tr("Remove"), parent);
    buttons->addWidget(slot.removeButton);
    buttons->addStretch();
    form->addLayout(buttons);
    slot.lockedNote = makeDescription(lockedNote, parent);
    form->addWidget(slot.lockedNote);

    connect(slot.uploadButton, &QPushButton::clicked, this, [this, &slot]() { chooseImage(slot); });
    connect(slot.removeButton, &QPushButton::clicked, this, [this, &slot]() {
        slot.pending = QJsonValue(QJsonValue::Null);
        slot.pendingMd5.clear();
        updatePreviews();
        updateSaveBar();
    });
}

void GuildBoostPerksPage::load()
{
    showSaved();
}

QString GuildBoostPerksPage::savedHash(const ImageSlot &slot) const
{
    const auto guild = instance ? instance->getGuild(guildId) : std::nullopt;
    if (!guild)
        return {};
    return &slot == &banner ? guild->banner.valueOr(QString()) : guild->splash.valueOr(QString());
}

QUrl GuildBoostPerksPage::savedUrl(const ImageSlot &slot) const
{
    const QString hash = savedHash(slot);
    return &slot == &banner ? Discord::Cdn::guildBanner(guildId, hash, 480) : Discord::Cdn::guildSplash(guildId, hash, 480);
}

void GuildBoostPerksPage::showSaved()
{
    banner.pending = QJsonValue(QJsonValue::Undefined);
    splash.pending = QJsonValue(QJsonValue::Undefined);
    banner.pendingMd5.clear();
    splash.pendingMd5.clear();

    const auto guild = instance ? instance->getGuild(guildId) : std::nullopt;
    shownProgressBar = guild && guild->premiumProgressBarEnabled.valueOr(false);
    {
        QSignalBlocker blocker(progressBarCheck);
        progressBarCheck->setChecked(shownProgressBar);
    }
    updatePreviews();
    updateSaveBar();
}

void GuildBoostPerksPage::updatePreviews()
{
    for (ImageSlot *slot : { &banner, &splash }) {
        QPixmap pixmap;
        bool loading = false;
        const QString pending = slot->pending.toString();
        if (pending.startsWith(QLatin1String("data:"))) {
            pixmap.loadFromData(QByteArray::fromBase64(pending.mid(pending.indexOf(',') + 1).toLatin1()));
        } else if (slot->pending.isUndefined() && instance) {
            const QUrl url = savedUrl(*slot);
            loading = !url.isEmpty() && !images->isCached(url, PreviewSize);
            if (!url.isEmpty()) {
                const QPixmap fetched = images->get(url, PreviewSize, instance->accountId());
                if (!loading)
                    pixmap = fetched;
            }
        }

        if (pixmap.isNull()) {
            slot->preview->setText(loading ? tr("Loading…") : tr("No image"));
        } else {
            const qreal dpr = slot->preview->devicePixelRatioF();
            QPixmap fitted = pixmap.scaled(PreviewSize * dpr, Qt::KeepAspectRatio, Qt::SmoothTransformation);
            fitted.setDevicePixelRatio(dpr);
            slot->preview->setPixmap(fitted);
        }
        slot->removeButton->setVisible(canManage() && (slot->pending.isUndefined() ? !savedHash(*slot).isEmpty() : !slot->pending.isNull()));
    }
}

void GuildBoostPerksPage::chooseImage(ImageSlot &slot)
{
    ImageSlot *target = &slot;
    chooseFiles(this, tr("Upload Image"), ImageUpload::fileFilter(), false, [this, target](const QStringList &paths) {
        const bool isBanner = target == &banner;
        const ImageUpload::PreparedImage image = isBanner ? ImageUpload::prepareCropped(paths.first(), 2400, 1350)
                                                          : ImageUpload::prepareRaw(paths.first(), MaxImageBytes);
        if (image.dataUri.isEmpty()) {
            showActionError(image.error);
            return;
        }

        const auto guild = instance ? instance->getGuild(guildId) : std::nullopt;
        if (isBanner && image.animated && !(guild && guild->hasFeature(QStringLiteral("ANIMATED_BANNER")))) {
            showActionError(tr("Animated banners unlock at Boost Level 3."));
            return;
        }

        target->pending = image.dataUri;
        target->pendingMd5 = image.originalMd5;
        updatePreviews();
        updateSaveBar();
    });
}

bool GuildBoostPerksPage::hasUnsavedChanges() const
{
    const auto guild = instance ? instance->getGuild(guildId) : std::nullopt;
    const bool savedProgressBar = guild && guild->premiumProgressBarEnabled.valueOr(false);
    return !banner.pending.isUndefined() || !splash.pending.isUndefined() || progressBarCheck->isChecked() != savedProgressBar;
}

void GuildBoostPerksPage::warnUnsavedChanges()
{
    saveBar->flash();
}

void GuildBoostPerksPage::updateSaveBar()
{
    if (!saving)
        saveBar->setVisible(hasUnsavedChanges());
}

void GuildBoostPerksPage::save()
{
    if (!instance || saving)
        return;

    auto imageValue = [this](const ImageSlot &slot) -> QString { return slot.pending.isUndefined() ? savedHash(slot) : slot.pending.toString(); };

    Discord::GuildEdit edit;
    const QString splashValue = imageValue(splash);
    const QString bannerValue = imageValue(banner);
    if (splashValue.isEmpty())
        edit.splash = nullptr;
    else
        edit.splash = splashValue;
    if (bannerValue.isEmpty())
        edit.banner = nullptr;
    else
        edit.banner = bannerValue;
    edit.premiumProgressBarEnabled = progressBarCheck->isChecked();

    QMap<QString, QByteArray> md5s;
    for (const ImageSlot *slot : { &banner, &splash })
        if (!slot->pendingMd5.isEmpty())
            md5s.insert(slot->md5Key, slot->pendingMd5);

    saving = true;
    saveBar->setSaving(true);
    QPointer<GuildBoostPerksPage> self(this);
    instance->discord()->modifyGuild(guildId, edit, md5s, [self](const Core::Result<void> &result) {
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
