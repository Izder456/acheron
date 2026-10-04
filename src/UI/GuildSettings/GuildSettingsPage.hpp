#pragma once

#include <QColor>
#include <QIcon>
#include <QPointer>
#include <QWidget>

#include <functional>

#include "Core/Snowflake.hpp"
#include "Discord/Entities.hpp"

class QLabel;
class QToolButton;

namespace Acheron {
namespace Core {
class ClientInstance;
class ImageManager;
} // namespace Core
namespace UI {

class GuildSettingsPage : public QWidget
{
    Q_OBJECT
public:
    GuildSettingsPage(Core::ClientInstance *instance, Core::ImageManager *images, Core::Snowflake guildId, QWidget *parent = nullptr);

    void activate();

    [[nodiscard]] virtual bool hasUnsavedChanges() const { return false; }
    virtual void warnUnsavedChanges() {}

    static QIcon colorDot(const QColor &color);
    static QIcon colorSwatch(const QColor &color, const QSize &size);

    static void showError(QWidget *parent, const QString &error);
    static void chooseFiles(QWidget *parent, const QString &title, const QString &filter, bool multiple, std::function<void(const QStringList &)> chosen);

protected:
    virtual void load() {}
    virtual void updatePermissions() {}
    [[nodiscard]] bool isLoaded() const;

    [[nodiscard]] Core::Snowflake selfId() const;
    [[nodiscard]] bool hasPermission(Discord::Permissions permission) const;
    [[nodiscard]] QList<Discord::Channel> channelsOfType(Discord::ChannelType type) const;
    void showActionError(const QString &error);

    static QLabel *makeTitle(const QString &text, QWidget *parent);
    static QLabel *makeDescription(const QString &text, QWidget *parent);
    static QLabel *makeFieldLabel(const QString &text, QWidget *parent);
    static QWidget *makePermissionNotice(const QString &text, QWidget *parent);
    static QToolButton *makeColorReadout(const QSize &swatchSize, QWidget *parent);
    static QPixmap roundedPixmap(const QPixmap &source, int size, qreal radius);

    QPointer<Core::ClientInstance> instance;
    Core::ImageManager *images;
    Core::Snowflake guildId;

private:
    bool loaded = false;
    Discord::Permissions shownPermissions;
};

} // namespace UI
} // namespace Acheron
