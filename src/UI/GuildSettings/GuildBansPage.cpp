#include "GuildBansPage.hpp"

#include <QClipboard>
#include <QDialog>
#include <QDialogButtonBox>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QPushButton>
#include <QRegularExpression>
#include <QStackedWidget>
#include <QTreeWidget>
#include <QVBoxLayout>

#include "Core/ClientInstance.hpp"
#include "Core/ImageManager.hpp"

namespace Acheron {
namespace UI {

namespace {

constexpr int UserIdRole = Qt::UserRole;
constexpr int ReasonColumn = 2;
constexpr QSize AvatarSize(32, 32);
constexpr int BatchSize = 1000;

bool looksLikeUserId(const QString &text)
{
    static const QRegularExpression pattern(QStringLiteral("^\\d{17,19}$"));
    return pattern.match(text).hasMatch();
}

} // namespace

GuildBansPage::GuildBansPage(Core::ClientInstance *instance, Core::ImageManager *images, Core::Snowflake guildId,
                             QWidget *parent)
    : GuildSettingsPage(instance, images, guildId, parent)
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(24, 20, 24, 16);
    layout->addWidget(makeTitle(tr("Server Ban List"), this));
    layout->addWidget(makeDescription(tr("Bans by default are by account and IP. A user can circumvent an IP ban by using a "
                                         "proxy. Ban circumvention can be made very hard by enabling phone verification in Moderation."),
                                      this));
    permissionNotice = makePermissionNotice(tr("Showing the bans made while you've been connected. The full ban list needs the Ban Members permission."), this);
    layout->addWidget(permissionNotice);

    auto *searchRow = new QHBoxLayout();
    searchEdit = new QLineEdit(this);
    searchEdit->setPlaceholderText(tr("Search Bans by User Id or Username"));
    searchEdit->setClearButtonEnabled(true);
    searchRow->addWidget(searchEdit, 1);
    searchButton = new QPushButton(tr("Search"), this);
    searchRow->addWidget(searchButton);
    layout->addLayout(searchRow);

    states = new QStackedWidget(this);
    list = new QTreeWidget(states);
    list->setColumnCount(3);
    list->setHeaderLabels({ tr("User"), tr("Username"), tr("Reason") });
    list->setRootIsDecorated(false);
    list->setUniformRowHeights(true);
    list->setIconSize(AvatarSize);
    list->setContextMenuPolicy(Qt::CustomContextMenu);
    list->header()->setSectionResizeMode(2, QHeaderView::Stretch);
    list->setColumnWidth(0, 220);
    list->setColumnWidth(1, 160);
    states->addWidget(list);

    emptyState = new QWidget(states);
    auto *emptyLayout = new QVBoxLayout(emptyState);
    emptyLayout->addStretch();
    emptyTitle = makeFieldLabel(QString(), emptyState);
    emptyTitle->setAlignment(Qt::AlignCenter);
    emptyLayout->addWidget(emptyTitle);
    emptyText = makeDescription(QString(), emptyState);
    emptyText->setAlignment(Qt::AlignCenter);
    emptyLayout->addWidget(emptyText);
    emptyLayout->addStretch();
    states->addWidget(emptyState);
    layout->addWidget(states, 1);

    loadMoreButton = new QPushButton(tr("Load more"), this);
    loadMoreButton->hide();
    layout->addWidget(loadMoreButton, 0, Qt::AlignHCenter);

    connect(searchEdit, &QLineEdit::textChanged, this, &GuildBansPage::rebuildList);
    connect(searchEdit, &QLineEdit::returnPressed, this, &GuildBansPage::searchServer);
    connect(searchButton, &QPushButton::clicked, this, &GuildBansPage::searchServer);
    connect(loadMoreButton, &QPushButton::clicked, this, &GuildBansPage::fetchBatch);
    connect(list, &QTreeWidget::itemActivated, this, [this](QTreeWidgetItem *item) { showDetails(Core::Snowflake(item->data(0, UserIdRole).toULongLong())); });
    connect(list, &QTreeWidget::customContextMenuRequested, this, &GuildBansPage::showContextMenu);

    connect(instance->discord(), &Discord::Client::guildBanAdded, this, [this](const Discord::GuildBanEvent &event) {
        if (event.guildId.get() != this->guildId)
            return;
        Discord::Ban ban;
        ban.user = event.user.get();
        ban.reason = nullptr;
        addBans({ ban });
        rebuildList();
    });
    connect(instance->discord(), &Discord::Client::guildBanRemoved, this, [this](const Discord::GuildBanEvent &event) {
        if (event.guildId.get() != this->guildId)
            return;
        const Core::Snowflake userId = event.user->id.get();
        bans.remove(userId);
        order.removeAll(userId);
        rebuildList();
    });
    connect(images, &Core::ImageManager::imageFetched, this, [this](const QUrl &url, const QSize &size, const QPixmap &) {
        if (size != AvatarSize)
            return;
        avatarTracker.notify(url, [this](Core::Snowflake userId) {
            QTreeWidgetItem *item = rows.value(userId);
            const auto ban = bans.constFind(userId);
            if (item && ban != bans.constEnd())
                applyAvatar(item, ban->user.get());
        });
    });

    updatePermissions();
}

void GuildBansPage::load()
{
    if (canViewBans()) {
        fetchBatch();
        return;
    }
    addBans(instance ? instance->observedBans(guildId) : QList<Discord::Ban>());
    rebuildList();
}

void GuildBansPage::updatePermissions()
{
    const bool canView = canViewBans();
    permissionNotice->setVisible(!canView);
    searchButton->setVisible(canView);
    if (!canView)
        loadMoreButton->hide();
    list->setColumnHidden(ReasonColumn, !canView);
    emptyTitle->setText(canView ? tr("No Bans") : tr("No Bans Seen"));
    emptyText->setText(canView ? tr("You haven't banned anybody...") : tr("Nobody has been banned since you connected."));
    if (canView && isLoaded() && !listFetched)
        fetchBatch();
}

bool GuildBansPage::canViewBans() const
{
    return hasPermission(Discord::Permission::BAN_MEMBERS);
}

void GuildBansPage::fetchBatch()
{
    if (!instance || loading || !canViewBans())
        return;

    loading = true;
    loadMoreButton->setEnabled(false);
    QPointer<GuildBansPage> self(this);
    instance->discord()->fetchBans(guildId, lastBatchUserId, [self](const Core::Result<QList<Discord::Ban>> &result) {
        if (!self)
            return;
        self->loading = false;
        self->loadMoreButton->setEnabled(true);
        if (!result.success()) {
            self->showActionError(result.error);
            return;
        }
        self->listFetched = true;
        if (!result.value->isEmpty())
            self->lastBatchUserId = result.value->last().user->id.get();
        self->addBans(*result.value);
        self->loadMoreButton->setVisible(result.value->size() == BatchSize);
        self->rebuildList();
    });
}

void GuildBansPage::searchServer()
{
    const QString text = searchEdit->text().trimmed();
    if (!instance || text.isEmpty() || !canViewBans())
        return;

    QList<Core::Snowflake> userIds;
    QString query;
    for (const QString &term : text.split(QLatin1Char(','))) {
        const QString trimmed = term.trimmed();
        if (looksLikeUserId(trimmed))
            userIds.append(Core::Snowflake(trimmed.toULongLong()));
        else if (query.isEmpty())
            query = trimmed;
    }

    QPointer<GuildBansPage> self(this);
    instance->discord()->searchBans(guildId, query, userIds, [self](const Core::Result<QList<Discord::Ban>> &result) {
        if (!self)
            return;
        if (!result.success()) {
            self->showActionError(result.error);
            return;
        }
        self->addBans(*result.value);
        self->rebuildList();
    });
}

void GuildBansPage::addBans(const QList<Discord::Ban> &added)
{
    for (const auto &ban : added) {
        const Core::Snowflake userId = ban.user->id.get();
        if (!userId.isValid())
            continue;
        if (!bans.contains(userId))
            order.append(userId);
        bans.insert(userId, ban);
        if (instance)
            instance->users()->saveUser(ban.user.get());
    }
}

bool GuildBansPage::matchesFilter(const Discord::Ban &ban) const
{
    const QString text = searchEdit->text().trimmed();
    if (text.isEmpty())
        return true;

    for (const QString &term : text.split(QLatin1Char(','))) {
        const QString trimmed = term.trimmed();
        if (trimmed.isEmpty())
            continue;
        if (QString::number(quint64(ban.user->id.get())) == trimmed ||
            ban.user->username->contains(trimmed, Qt::CaseInsensitive) ||
            (ban.user->globalName.hasValue() && ban.user->globalName->contains(trimmed, Qt::CaseInsensitive)))
            return true;
    }
    return false;
}

void GuildBansPage::rebuildList()
{
    list->clear();
    rows.clear();
    avatarTracker.clear();
    if (!instance)
        return;

    for (const Core::Snowflake userId : order) {
        const Discord::Ban &ban = bans[userId];
        if (!matchesFilter(ban))
            continue;

        auto *item = new QTreeWidgetItem(list);
        rows.insert(userId, item);
        item->setData(0, UserIdRole, QVariant::fromValue<quint64>(userId));
        item->setText(0, instance->users()->getAuthorDisplayName(ban.user.get(), guildId));
        QFont bold = item->font(0);
        bold.setBold(true);
        item->setFont(0, bold);
        item->setText(1, ban.user->tag());
        item->setText(ReasonColumn, ban.reason.hasValue() && !ban.reason->isEmpty() ? ban.reason.get() : QString());
        item->setToolTip(ReasonColumn, item->text(ReasonColumn));
        applyAvatar(item, ban.user.get());
    }
    states->setCurrentWidget(bans.isEmpty() ? emptyState : list);
}

void GuildBansPage::applyAvatar(QTreeWidgetItem *item, const Discord::User &user)
{
    if (!instance)
        return;
    const QUrl url = instance->users()->getAvatarUrl(user, Core::Snowflake(), 64);
    const QPixmap avatar = avatarTracker.fetch(images, url, AvatarSize, user.id.get(), instance->accountId());
    item->setIcon(0, QIcon(roundedPixmap(avatar, AvatarSize.width(), AvatarSize.width() / 2.0)));
}

void GuildBansPage::showDetails(Core::Snowflake userId)
{
    const auto found = bans.constFind(userId);
    if (found == bans.constEnd() || !canViewBans())
        return;
    const Discord::Ban ban = found.value();

    auto *dialog = new QDialog(this);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setWindowTitle(ban.user->tag());
    auto *layout = new QVBoxLayout(dialog);
    QLabel *title = makeTitle(ban.user->tag(), dialog);
    title->setTextFormat(Qt::PlainText);
    layout->addWidget(title);
    layout->addWidget(makeFieldLabel(tr("Ban Reason"), dialog));
    auto *reason = new QLabel(ban.reason.hasValue() && !ban.reason->isEmpty() ? ban.reason.get() : tr("No reason provided"), dialog);
    reason->setTextFormat(Qt::PlainText);
    reason->setWordWrap(true);
    reason->setTextInteractionFlags(Qt::TextSelectableByMouse);
    layout->addWidget(reason);

    auto *buttons = new QDialogButtonBox(dialog);
    QPushButton *revokeButton = buttons->addButton(tr("Revoke Ban"), QDialogButtonBox::DestructiveRole);
    QPushButton *doneButton = buttons->addButton(tr("Done"), QDialogButtonBox::AcceptRole);
    doneButton->setDefault(true);
    connect(doneButton, &QPushButton::clicked, dialog, &QDialog::accept);
    connect(revokeButton, &QPushButton::clicked, dialog, [this, dialog, userId]() {
        revoke(userId);
        dialog->accept();
    });
    layout->addWidget(buttons);
    dialog->open();
}

void GuildBansPage::revoke(Core::Snowflake userId)
{
    if (!instance)
        return;
    QPointer<GuildBansPage> self(this);
    instance->discord()->unbanMember(guildId, userId, [self, userId](const Core::Result<void> &result) {
        if (!self)
            return;
        if (!result.success()) {
            self->showActionError(result.error);
            return;
        }
        self->bans.remove(userId);
        self->order.removeAll(userId);
        self->rebuildList();
    });
}

void GuildBansPage::showContextMenu(const QPoint &pos)
{
    QTreeWidgetItem *item = list->itemAt(pos);
    if (!item)
        return;
    const Core::Snowflake userId(item->data(0, UserIdRole).toULongLong());

    QMenu menu(this);
    if (canViewBans()) {
        menu.addAction(tr("View Ban"), this, [this, userId]() { showDetails(userId); });
        menu.addAction(tr("Revoke Ban"), this, [this, userId]() { revoke(userId); });
        menu.addSeparator();
    }
    menu.addAction(tr("Copy User ID"), this, [userId]() { QGuiApplication::clipboard()->setText(QString::number(quint64(userId))); });
    menu.exec(list->viewport()->mapToGlobal(pos));
}

} // namespace UI
} // namespace Acheron
