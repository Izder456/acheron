#include "GuildSettingsPage.hpp"

#include <QFileDialog>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPainter>
#include <QPainterPath>
#include <QToolButton>

#include <algorithm>

#include "Core/ClientInstance.hpp"
#include "Core/Theme/Icons.hpp"

namespace Acheron {
namespace UI {

GuildSettingsPage::GuildSettingsPage(Core::ClientInstance *instance, Core::ImageManager *images,
                                     Core::Snowflake guildId, QWidget *parent)
    : QWidget(parent), instance(instance), images(images), guildId(guildId)
{
    if (!instance)
        return;
    shownPermissions = instance->permissions()->getGuildPermissions(instance->accountId(), guildId);
    connect(instance->permissions(), &Core::PermissionManager::guildPermissionsChanged, this, [this](Core::Snowflake changedGuildId) {
        if (changedGuildId != this->guildId || !this->instance)
            return;
        const Discord::Permissions current = this->instance->permissions()->getGuildPermissions(this->instance->accountId(), this->guildId);
        if (current == shownPermissions)
            return;
        shownPermissions = current;
        updatePermissions();
    });
}

bool GuildSettingsPage::isLoaded() const
{
    return loaded;
}

void GuildSettingsPage::activate()
{
    if (loaded)
        return;
    loaded = true;
    load();
}

Core::Snowflake GuildSettingsPage::selfId() const
{
    return instance ? instance->accountId() : Core::Snowflake();
}

bool GuildSettingsPage::hasPermission(Discord::Permissions permission) const
{
    return instance && instance->permissions()->hasGuildPermission(instance->accountId(), guildId, permission);
}

QList<Discord::Channel> GuildSettingsPage::channelsOfType(Discord::ChannelType type) const
{
    if (!instance)
        return {};

    const QList<Discord::Channel> all = instance->getGuildChannels(guildId);
    auto byPosition = [](const Discord::Channel &a, const Discord::Channel &b) {
        const int positionA = a.position.valueOr(0);
        const int positionB = b.position.valueOr(0);
        return positionA != positionB ? positionA < positionB : a.id.get() < b.id.get();
    };

    QList<Discord::Channel> categories;
    for (const auto &channel : all)
        if (channel.type.hasValue() && channel.type.get() == Discord::ChannelType::GUILD_CATEGORY)
            categories.append(channel);
    std::sort(categories.begin(), categories.end(), byPosition);

    auto channelsUnder = [&](Core::Snowflake parentId) {
        QList<Discord::Channel> children;
        for (const auto &channel : all) {
            if (!channel.type.hasValue() || channel.type.get() != type)
                continue;
            const Core::Snowflake parent = channel.parentId.hasValue() ? channel.parentId.get() : Core::Snowflake();
            if (parent == parentId)
                children.append(channel);
        }
        std::sort(children.begin(), children.end(), byPosition);
        return children;
    };

    QList<Discord::Channel> ordered = channelsUnder(Core::Snowflake());
    for (const auto &category : categories)
        ordered += channelsUnder(category.id.get());
    return ordered;
}

void GuildSettingsPage::showActionError(const QString &error)
{
    showError(this, error);
}

void GuildSettingsPage::showError(QWidget *parent, const QString &error)
{
    auto *box = new QMessageBox(QMessageBox::Warning, tr("Oops, something went wrong..."), error, QMessageBox::Ok, parent);
    box->setAttribute(Qt::WA_DeleteOnClose);
    box->open();
}

void GuildSettingsPage::chooseFiles(QWidget *parent, const QString &title, const QString &filter, bool multiple,
                                    std::function<void(const QStringList &)> chosen)
{
    auto *dialog = new QFileDialog(parent, title, QString(), filter);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setFileMode(multiple ? QFileDialog::ExistingFiles : QFileDialog::ExistingFile);
    QObject::connect(dialog, &QFileDialog::filesSelected, parent, [chosen = std::move(chosen)](const QStringList &files) {
        if (!files.isEmpty())
            chosen(files);
    });
    dialog->open();
}

QLabel *GuildSettingsPage::makeTitle(const QString &text, QWidget *parent)
{
    auto *label = new QLabel(text, parent);
    QFont font = label->font();
    font.setBold(true);
    font.setPointSizeF(font.pointSizeF() * 1.4);
    label->setFont(font);
    return label;
}

QLabel *GuildSettingsPage::makeDescription(const QString &text, QWidget *parent)
{
    auto *label = new QLabel(text, parent);
    label->setWordWrap(true);
    label->setForegroundRole(QPalette::PlaceholderText);
    return label;
}

QLabel *GuildSettingsPage::makeFieldLabel(const QString &text, QWidget *parent)
{
    auto *label = new QLabel(text, parent);
    QFont font = label->font();
    font.setBold(true);
    label->setFont(font);
    return label;
}

QToolButton *GuildSettingsPage::makeColorReadout(const QSize &swatchSize, QWidget *parent)
{
    auto *readout = new QToolButton(parent);
    readout->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    readout->setAutoRaise(true);
    readout->setIconSize(swatchSize);
    readout->setFocusPolicy(Qt::NoFocus);
    readout->setAttribute(Qt::WA_TransparentForMouseEvents);
    return readout;
}

QWidget *GuildSettingsPage::makePermissionNotice(const QString &text, QWidget *parent)
{
    auto *notice = new QWidget(parent);
    auto *layout = new QHBoxLayout(notice);
    layout->setContentsMargins(0, 4, 0, 4);
    layout->setSpacing(6);
    auto *lock = new QLabel(notice);
    const QColor muted = notice->palette().color(QPalette::PlaceholderText);
    lock->setPixmap(Core::Theme::Icons::pixmap(Core::Theme::Icons::Name::Lock, 14, muted, notice->devicePixelRatioF()));
    layout->addWidget(lock, 0, Qt::AlignTop);
    layout->addWidget(makeDescription(text, notice), 1);
    notice->hide();
    return notice;
}

QIcon GuildSettingsPage::colorDot(const QColor &color)
{
    QPixmap pixmap(32, 32);
    pixmap.setDevicePixelRatio(2.0);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(Qt::NoPen);
    painter.setBrush(color);
    painter.drawEllipse(QRectF(3, 3, 10, 10));
    return QIcon(pixmap);
}

QIcon GuildSettingsPage::colorSwatch(const QColor &color, const QSize &size)
{
    QPixmap pixmap(size * 2);
    pixmap.setDevicePixelRatio(2.0);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(Qt::NoPen);
    painter.setBrush(color);
    painter.drawRoundedRect(QRectF(QPointF(0, 0), QSizeF(size)), 4, 4);
    return QIcon(pixmap);
}

QPixmap GuildSettingsPage::roundedPixmap(const QPixmap &source, int size, qreal radius)
{
    const qreal dpr = source.devicePixelRatio() > 0 ? source.devicePixelRatio() : 1.0;
    QPixmap result(QSize(size, size) * dpr);
    result.setDevicePixelRatio(dpr);
    result.fill(Qt::transparent);
    if (source.isNull())
        return result;

    QPainter painter(&result);
    painter.setRenderHints(QPainter::Antialiasing | QPainter::SmoothPixmapTransform);
    QPainterPath clip;
    clip.addRoundedRect(QRectF(0, 0, size, size), radius, radius);
    painter.setClipPath(clip);
    const QPixmap scaled = source.scaled(QSize(size, size) * dpr, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
    const QSizeF logical = QSizeF(scaled.size()) / dpr;
    painter.drawPixmap(QPointF((size - logical.width()) / 2, (size - logical.height()) / 2), scaled);
    return result;
}

} // namespace UI
} // namespace Acheron
