#pragma once

#include <QJsonValue>
#include <QUrl>

#include "Discord/GuildRequests.hpp"
#include "GuildSettingsPage.hpp"

class QButtonGroup;
class QCheckBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QStackedWidget;
class QToolButton;

namespace Acheron {
namespace UI {

class UnsavedChangesBar;

class GuildProfilePage : public GuildSettingsPage
{
    Q_OBJECT
public:
    GuildProfilePage(Core::ClientInstance *instance, Core::ImageManager *images, Core::Snowflake guildId, QWidget *parent = nullptr);

    [[nodiscard]] bool hasUnsavedChanges() const override;
    void warnUnsavedChanges() override;

protected:
    void load() override;
    void updatePermissions() override;

private:
    [[nodiscard]] bool canManage() const;
    QWidget *buildForm();
    void fetchProfile();
    void applyProfile(const Discord::GuildProfileEdit &profile);
    void showFields(const Discord::GuildProfileEdit &edit);
    void showBannerColor(const QString &brandColor);
    [[nodiscard]] Discord::GuildProfileEdit editedProfile() const;
    void updateSaveBar();
    void updateIconPreview();
    void chooseIcon();
    void save();
    void onGuildUpdated(const Discord::Guild &guild);

    QStackedWidget *states;
    QWidget *loadingState;
    QWidget *errorState;
    QWidget *formState;
    QLineEdit *nameEdit;
    QLabel *iconHint;
    QLabel *iconPreview;
    QPushButton *changeIconButton;
    QPushButton *removeIconButton;
    QWidget *bannerPicker;
    QToolButton *bannerReadout;
    QButtonGroup *bannerColors;
    QLabel *traitsLabel;
    QLabel *traitsHint;
    QList<QLineEdit *> traitEdits;
    QLabel *descriptionLabel;
    QLabel *descriptionHint;
    QPlainTextEdit *descriptionEdit;
    QCheckBox *privateCheck;
    UnsavedChangesBar *saveBar;

    Discord::GuildProfileEdit saved;
    QJsonValue pendingIcon = QJsonValue(QJsonValue::Undefined);
    QUrl iconPreviewUrl;
    bool profileLoaded = false;
    bool saving = false;
};

} // namespace UI
} // namespace Acheron
