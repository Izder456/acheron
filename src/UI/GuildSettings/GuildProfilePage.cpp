#include "GuildProfilePage.hpp"

#include <QButtonGroup>
#include <QCheckBox>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPainter>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QStackedWidget>
#include <QTextDocument>
#include <QToolButton>
#include <QVBoxLayout>

#include "Core/ClientInstance.hpp"
#include "Core/ImageManager.hpp"
#include "Discord/CdnUrls.hpp"
#include "ImageUpload.hpp"
#include "UnsavedChangesBar.hpp"

namespace Acheron {
namespace UI {

namespace {

constexpr int IconPreviewSize = 96;
constexpr int NameMaxLength = 100;
constexpr int DescriptionMaxLength = 300;
constexpr int TraitMaxLength = 24;

constexpr int VisibilityPublic = 1;
constexpr int VisibilityRestricted = 2;
constexpr int VisibilityPublicWithRecruitment = 3;

struct BannerColor
{
    const char *name;
    const char *color;
};

constexpr BannerColor BannerColors[] = {
    { "Soul", "#ff1c90" },
    { "Volcano", "#e81d1e" },
    { "Marsh", "#e86e1d" },
    { "Thunder", "#e8c02f" },
    { "Rising", "#71368a" },
    { "Zephyr", "#029FFC" },
    { "Cascade", "#4fe2ca" },
    { "Earth", "#406601" },
    { "Boulder", "#272727" },
};

bool isPrivate(const QJsonValue &visibility)
{
    const int value = visibility.toInt();
    return value != VisibilityPublic && value != VisibilityPublicWithRecruitment;
}

QString asEdited(const QString &description)
{
    QTextDocument document;
    document.setPlainText(description);
    return document.toPlainText();
}

QToolButton *makeSwatch(const QString &tooltip, const QColor &color, QWidget *parent)
{
    auto *swatch = new QToolButton(parent);
    swatch->setCheckable(true);
    swatch->setFixedSize(32, 32);
    swatch->setToolTip(tooltip);
    swatch->setCursor(Qt::PointingHandCursor);
    const QString fill = color.isValid() ? color.name() : QStringLiteral("transparent");
    swatch->setStyleSheet(QStringLiteral("QToolButton { background-color: %1; border: 1px solid palette(mid); border-radius: 4px; }"
                                         "QToolButton:checked { border: 2px solid palette(highlight); }")
                                  .arg(fill));
    return swatch;
}

} // namespace

GuildProfilePage::GuildProfilePage(Core::ClientInstance *instance, Core::ImageManager *images,
                                   Core::Snowflake guildId, QWidget *parent)
    : GuildSettingsPage(instance, images, guildId, parent)
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(24, 20, 24, 16);

    layout->addWidget(makeTitle(tr("Server Profile"), this));
    layout->addWidget(makeDescription(tr("Customize how your server appears in invite links and, if enabled, in Server "
                                         "Discovery and Announcement Channel messages"),
                                      this));

    states = new QStackedWidget(this);
    auto *loadingLabel = new QLabel(tr("Loading…"), states);
    loadingLabel->setAlignment(Qt::AlignCenter);
    loadingState = loadingLabel;
    states->addWidget(loadingState);

    errorState = new QWidget(states);
    auto *errorLayout = new QVBoxLayout(errorState);
    errorLayout->addStretch();
    auto *errorTitle = makeFieldLabel(tr("Something went wrong"), errorState);
    errorTitle->setAlignment(Qt::AlignCenter);
    errorLayout->addWidget(errorTitle);
    auto *errorText = new QLabel(tr("We couldn't load the profile for this server."), errorState);
    errorText->setAlignment(Qt::AlignCenter);
    errorLayout->addWidget(errorText);
    auto *retryButton = new QPushButton(tr("Retry"), errorState);
    connect(retryButton, &QPushButton::clicked, this, &GuildProfilePage::fetchProfile);
    errorLayout->addWidget(retryButton, 0, Qt::AlignHCenter);
    errorLayout->addStretch();
    states->addWidget(errorState);

    formState = buildForm();
    states->addWidget(formState);
    layout->addWidget(states, 1);

    saveBar = new UnsavedChangesBar(this);
    saveBar->hide();
    connect(saveBar, &UnsavedChangesBar::resetClicked, this, [this]() {
        pendingIcon = QJsonValue(QJsonValue::Undefined);
        showFields(saved);
    });
    connect(saveBar, &UnsavedChangesBar::saveClicked, this, &GuildProfilePage::save);
    layout->addWidget(saveBar);

    connect(instance, &Core::ClientInstance::guildUpdated, this, &GuildProfilePage::onGuildUpdated);
    connect(images, &Core::ImageManager::imageFetched, this, [this](const QUrl &url, const QSize &size, const QPixmap &) {
        if (url == iconPreviewUrl && size == QSize(IconPreviewSize, IconPreviewSize))
            updateIconPreview();
    });

    updatePermissions();
}

bool GuildProfilePage::canManage() const
{
    return hasPermission(Discord::Permission::MANAGE_GUILD);
}

void GuildProfilePage::updatePermissions()
{
    const bool manage = canManage();
    nameEdit->setEnabled(manage);
    iconHint->setVisible(manage);
    changeIconButton->setVisible(manage);
    bannerPicker->setVisible(manage);
    bannerReadout->setVisible(!manage);

    bool anyTrait = false;
    for (QLineEdit *traitEdit : traitEdits) {
        traitEdit->setEnabled(manage);
        traitEdit->setVisible(manage || !traitEdit->text().isEmpty());
        anyTrait = anyTrait || !traitEdit->text().isEmpty();
    }
    traitsLabel->setVisible(manage || anyTrait);
    traitsHint->setVisible(manage);

    const bool hasDescription = !descriptionEdit->toPlainText().isEmpty();
    descriptionLabel->setVisible(manage || hasDescription);
    descriptionHint->setVisible(manage);
    descriptionEdit->setVisible(manage || hasDescription);
    descriptionEdit->setEnabled(manage);

    const auto guild = instance ? instance->getGuild(guildId) : std::nullopt;
    privateCheck->setEnabled(manage && !(guild && guild->hasFeature(QStringLiteral("DISCOVERABLE"))));
    updateIconPreview();
}

QWidget *GuildProfilePage::buildForm()
{
    auto *scroll = new QScrollArea(states);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);

    auto *content = new QWidget(scroll);
    content->setMaximumWidth(680);
    auto *form = new QVBoxLayout(content);
    form->setContentsMargins(0, 8, 12, 8);
    form->setSpacing(6);

    form->addWidget(makeFieldLabel(tr("Name"), content));
    nameEdit = new QLineEdit(content);
    nameEdit->setMaxLength(NameMaxLength);
    form->addWidget(nameEdit);
    form->addSpacing(12);

    form->addWidget(makeFieldLabel(tr("Icon"), content));
    iconHint = makeDescription(tr("We recommend an image of at least 512x512."), content);
    form->addWidget(iconHint);
    auto *iconRow = new QHBoxLayout();
    iconPreview = new QLabel(content);
    iconPreview->setFixedSize(IconPreviewSize, IconPreviewSize);
    iconRow->addWidget(iconPreview);
    changeIconButton = new QPushButton(tr("Change Server Icon"), content);
    iconRow->addWidget(changeIconButton, 0, Qt::AlignVCenter);
    removeIconButton = new QPushButton(tr("Remove Icon"), content);
    iconRow->addWidget(removeIconButton, 0, Qt::AlignVCenter);
    iconRow->addStretch();
    form->addLayout(iconRow);
    form->addSpacing(12);

    form->addWidget(makeFieldLabel(tr("Banner"), content));
    bannerReadout = makeColorReadout(QSize(24, 24), content);
    form->addWidget(bannerReadout, 0, Qt::AlignLeft);
    bannerPicker = new QWidget(content);
    auto *swatchRow = new QHBoxLayout(bannerPicker);
    swatchRow->setContentsMargins(0, 0, 0, 0);
    swatchRow->setSpacing(6);
    bannerColors = new QButtonGroup(bannerPicker);
    bannerColors->setExclusive(true);
    QToolButton *iconColor = makeSwatch(tr("Server Icon Color"), QColor(), bannerPicker);
    iconColor->setText(tr("Icon"));
    iconColor->setFixedWidth(48);
    bannerColors->addButton(iconColor);
    swatchRow->addWidget(iconColor);
    for (const BannerColor &preset : BannerColors) {
        QToolButton *swatch = makeSwatch(QString::fromLatin1(preset.name), QColor(QString::fromLatin1(preset.color)), bannerPicker);
        swatch->setProperty("brandColor", QString::fromLatin1(preset.color));
        bannerColors->addButton(swatch);
        swatchRow->addWidget(swatch);
    }
    swatchRow->addStretch();
    form->addWidget(bannerPicker);
    form->addSpacing(12);

    traitsLabel = makeFieldLabel(tr("Traits"), content);
    form->addWidget(traitsLabel);
    traitsHint = makeDescription(tr("Add up to 5 traits to show off your server’s interests and personality."), content);
    form->addWidget(traitsHint);
    auto *traitsGrid = new QGridLayout();
    for (int i = 0; i < Discord::GuildProfileEdit::TraitSlots; i++) {
        auto *traitEdit = new QLineEdit(content);
        traitEdit->setMaxLength(TraitMaxLength);
        traitsGrid->addWidget(traitEdit, i / 3, i % 3);
        traitEdits.append(traitEdit);
        connect(traitEdit, &QLineEdit::textChanged, this, &GuildProfilePage::updateSaveBar);
    }
    form->addLayout(traitsGrid);
    form->addSpacing(12);

    descriptionLabel = makeFieldLabel(tr("Description"), content);
    form->addWidget(descriptionLabel);
    descriptionHint = makeDescription(tr("How did your server get started? Why should people join?"), content);
    form->addWidget(descriptionHint);
    descriptionEdit = new QPlainTextEdit(content);
    descriptionEdit->setPlaceholderText(tr("Tell the world a bit about this server."));
    descriptionEdit->setFixedHeight(96);
    form->addWidget(descriptionEdit);
    form->addSpacing(12);

    privateCheck = new QCheckBox(tr("Private Profile"), content);
    form->addWidget(privateCheck);
    form->addWidget(makeDescription(tr("When enabled, only server members can view profile content. Non-members won't be "
                                       "able to see this content unless they have an invite."),
                                    content));
    form->addStretch();

    connect(nameEdit, &QLineEdit::textChanged, this, &GuildProfilePage::updateSaveBar);
    connect(descriptionEdit, &QPlainTextEdit::textChanged, this, [this]() {
        if (descriptionEdit->toPlainText().size() > DescriptionMaxLength) {
            QSignalBlocker blocker(descriptionEdit);
            descriptionEdit->setPlainText(descriptionEdit->toPlainText().left(DescriptionMaxLength));
            descriptionEdit->moveCursor(QTextCursor::End);
        }
        updateSaveBar();
    });
    connect(bannerColors, qOverload<QAbstractButton *>(&QButtonGroup::buttonClicked), this, &GuildProfilePage::updateSaveBar);
    connect(privateCheck, &QCheckBox::toggled, this, &GuildProfilePage::updateSaveBar);
    connect(changeIconButton, &QPushButton::clicked, this, &GuildProfilePage::chooseIcon);
    connect(removeIconButton, &QPushButton::clicked, this, [this]() {
        pendingIcon = QJsonValue(QJsonValue::Null);
        updateIconPreview();
        updateSaveBar();
    });

    scroll->setWidget(content);
    return scroll;
}

void GuildProfilePage::load()
{
    fetchProfile();
}

void GuildProfilePage::fetchProfile()
{
    if (!instance)
        return;

    states->setCurrentWidget(loadingState);
    QPointer<GuildProfilePage> self(this);
    instance->discord()->fetchGuildProfile(guildId, [self](const Core::Result<Discord::GuildProfileEdit> &result) {
        if (!self)
            return;
        if (!result.success()) {
            self->states->setCurrentWidget(self->errorState);
            return;
        }
        self->applyProfile(*result.value);
        self->states->setCurrentWidget(self->formState);
    });
}

void GuildProfilePage::applyProfile(const Discord::GuildProfileEdit &profile)
{
    saved = profile;
    pendingIcon = QJsonValue(QJsonValue::Undefined);
    profileLoaded = true;
    showFields(saved);
}

void GuildProfilePage::showFields(const Discord::GuildProfileEdit &edit)
{
    {
        QSignalBlocker nameBlocker(nameEdit);
        QSignalBlocker descriptionBlocker(descriptionEdit);
        QSignalBlocker privateBlocker(privateCheck);
        nameEdit->setText(edit.name);
        descriptionEdit->setPlainText(edit.description);
        privateCheck->setChecked(isPrivate(edit.visibility));
        for (int i = 0; i < traitEdits.size() && i < edit.traits.size(); i++) {
            QSignalBlocker traitBlocker(traitEdits[i]);
            traitEdits[i]->setText(edit.traits[i].label);
        }

        const QString brandColor = edit.brandColorPrimary.toString();
        bannerColors->setExclusive(false);
        for (QAbstractButton *button : bannerColors->buttons())
            button->setChecked(button->property("brandColor").toString().compare(brandColor, Qt::CaseInsensitive) == 0);
        bannerColors->setExclusive(true);
    }
    showBannerColor(edit.brandColorPrimary.toString());
    updatePermissions();
    updateSaveBar();
}

void GuildProfilePage::showBannerColor(const QString &brandColor)
{
    if (brandColor.isEmpty()) {
        bannerReadout->setIcon(QIcon());
        bannerReadout->setText(tr("Server Icon Color"));
        return;
    }
    QString name = brandColor.toUpper();
    for (const BannerColor &preset : BannerColors)
        if (brandColor.compare(QLatin1String(preset.color), Qt::CaseInsensitive) == 0)
            name = QString::fromLatin1(preset.name);
    bannerReadout->setIcon(colorSwatch(QColor(brandColor), bannerReadout->iconSize()));
    bannerReadout->setText(name);
}

Discord::GuildProfileEdit GuildProfilePage::editedProfile() const
{
    Discord::GuildProfileEdit edit = saved;
    edit.name = nameEdit->text();
    if (descriptionEdit->toPlainText() != asEdited(saved.description))
        edit.description = descriptionEdit->toPlainText();
    if (!pendingIcon.isUndefined())
        edit.icon = pendingIcon;

    if (QAbstractButton *checked = bannerColors->checkedButton()) {
        const QString brandColor = checked->property("brandColor").toString();
        edit.brandColorPrimary = brandColor.isEmpty() ? QJsonValue(QJsonValue::Null) : QJsonValue(brandColor);
    }

    if (privateCheck->isChecked() != isPrivate(saved.visibility))
        edit.visibility = privateCheck->isChecked() ? VisibilityRestricted : VisibilityPublic;

    for (int i = 0; i < traitEdits.size() && i < edit.traits.size(); i++)
        if (traitEdits[i]->text().trimmed() != edit.traits[i].label.trimmed())
            edit.traits[i].label = traitEdits[i]->text().trimmed();
    return edit;
}

bool GuildProfilePage::hasUnsavedChanges() const
{
    return profileLoaded && editedProfile().toJson().toBytes() != saved.toJson().toBytes();
}

void GuildProfilePage::warnUnsavedChanges()
{
    saveBar->flash();
}

void GuildProfilePage::updateSaveBar()
{
    if (!saving)
        saveBar->setVisible(hasUnsavedChanges());
}

void GuildProfilePage::updateIconPreview()
{
    QPixmap pixmap;
    const QJsonValue icon = pendingIcon.isUndefined() ? saved.icon : pendingIcon;
    const QString iconValue = icon.toString();

    iconPreviewUrl.clear();
    if (iconValue.startsWith(QLatin1String("data:"))) {
        const QByteArray base64 = iconValue.mid(iconValue.indexOf(',') + 1).toLatin1();
        pixmap.loadFromData(QByteArray::fromBase64(base64));
    } else if (!iconValue.isEmpty() && instance) {
        iconPreviewUrl = Discord::Cdn::guildIcon(guildId, iconValue, IconPreviewSize);
        const QSize size(IconPreviewSize, IconPreviewSize);
        if (images->isCached(iconPreviewUrl, size))
            pixmap = images->get(iconPreviewUrl, size, instance->accountId());
        else
            images->get(iconPreviewUrl, size, instance->accountId());
    }

    if (pixmap.isNull()) {
        pixmap = QPixmap(IconPreviewSize, IconPreviewSize);
        pixmap.fill(Qt::transparent);
        QPainter painter(&pixmap);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(Qt::NoPen);
        painter.setBrush(palette().color(QPalette::Button));
        painter.drawEllipse(pixmap.rect());
        painter.setPen(palette().color(QPalette::ButtonText));
        QStringList initials;
        for (const QString &word : nameEdit->text().split(QLatin1Char(' '), Qt::SkipEmptyParts))
            initials << word.left(1);
        painter.drawText(pixmap.rect(), Qt::AlignCenter, initials.join(QString()).left(3));
    }

    iconPreview->setPixmap(roundedPixmap(pixmap, IconPreviewSize, IconPreviewSize / 2.0));
    removeIconButton->setVisible(canManage() && !iconValue.isEmpty());
}

void GuildProfilePage::chooseIcon()
{
    chooseFiles(this, tr("Change Server Icon"), ImageUpload::fileFilter(), false, [this](const QStringList &paths) {
        const ImageUpload::PreparedImage image = ImageUpload::prepareCropped(paths.first(), 1024, 1024);
        if (image.dataUri.isEmpty()) {
            showActionError(image.error);
            return;
        }

        pendingIcon = image.dataUri;
        updateIconPreview();
        updateSaveBar();
    });
}

void GuildProfilePage::save()
{
    if (!instance || saving)
        return;

    saving = true;
    saveBar->setSaving(true);
    QPointer<GuildProfilePage> self(this);
    instance->discord()->modifyGuildProfile(guildId, editedProfile(), [self](const Core::Result<Discord::GuildProfileEdit> &result) {
        if (!self)
            return;
        self->saving = false;
        if (!result.success()) {
            self->saveBar->showError(result.error);
            self->saveBar->flash();
            return;
        }
        self->saveBar->setSaving(false);
        self->applyProfile(*result.value);
    });
}

void GuildProfilePage::onGuildUpdated(const Discord::Guild &guild)
{
    if (guild.id.get() != guildId)
        return;
    updatePermissions();
    if (!profileLoaded)
        return;

    const Discord::GuildProfileEdit before = saved;
    const bool edited = hasUnsavedChanges();
    saved.name = guild.name.get();
    saved.icon = guild.icon->isEmpty() ? QJsonValue(QJsonValue::Null) : QJsonValue(guild.icon.get());
    saved.description = guild.description.valueOr(QString());
    if (!edited) {
        showFields(saved);
        return;
    }

    if (nameEdit->text() == before.name) {
        QSignalBlocker blocker(nameEdit);
        nameEdit->setText(saved.name);
    }
    if (descriptionEdit->toPlainText() == asEdited(before.description)) {
        QSignalBlocker blocker(descriptionEdit);
        descriptionEdit->setPlainText(saved.description);
    }
    updatePermissions();
    updateSaveBar();
}

} // namespace UI
} // namespace Acheron
