#pragma once

#include <QJsonValue>
#include <QUrl>

#include "GuildSettingsPage.hpp"

class QCheckBox;
class QLabel;
class QPushButton;
class QVBoxLayout;

namespace Acheron {
namespace UI {

class UnsavedChangesBar;

class GuildBoostPerksPage : public GuildSettingsPage
{
    Q_OBJECT
public:
    GuildBoostPerksPage(Core::ClientInstance *instance, Core::ImageManager *images, Core::Snowflake guildId, QWidget *parent = nullptr);

    [[nodiscard]] bool hasUnsavedChanges() const override;
    void warnUnsavedChanges() override;

protected:
    void load() override;
    void updatePermissions() override;

private:
    struct ImageSlot
    {
        QString md5Key;
        QString feature;
        QLabel *recommendation = nullptr;
        QLabel *preview = nullptr;
        QPushButton *uploadButton = nullptr;
        QPushButton *removeButton = nullptr;
        QLabel *lockedNote = nullptr;
        QJsonValue pending = QJsonValue(QJsonValue::Undefined);
        QByteArray pendingMd5;
    };

    [[nodiscard]] bool canManage() const;
    void addImageSlot(ImageSlot &slot, const QString &title, const QString &description, const QString &recommendation,
                      const QString &lockedNote, QWidget *parent, QVBoxLayout *form);
    void chooseImage(ImageSlot &slot);
    void showSaved();
    void onGuildUpdated(const Discord::Guild &guild);
    void updatePreviews();
    void updateSaveBar();
    [[nodiscard]] QString savedHash(const ImageSlot &slot) const;
    [[nodiscard]] QUrl savedUrl(const ImageSlot &slot) const;
    void save();

    QCheckBox *progressBarCheck;
    ImageSlot banner;
    ImageSlot splash;
    UnsavedChangesBar *saveBar;
    bool shownProgressBar = false;
    bool saving = false;
};

} // namespace UI
} // namespace Acheron
