#include "GuildAuditLogPage.hpp"

#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QStackedWidget>
#include <QVBoxLayout>

#include <algorithm>
#include <functional>

#include "Core/ClientInstance.hpp"
#include "Core/ImageManager.hpp"
#include "Core/Theme/Icons.hpp"
#include "Core/Theme/Manager.hpp"

namespace Acheron {
namespace UI {

namespace {

constexpr QSize AvatarSize(32, 32);
constexpr int MaxAutomaticFetches = 3;

constexpr Discord::Permission ModerationPermissions[] = {
    Discord::Permission::KICK_MEMBERS,
    Discord::Permission::BAN_MEMBERS,
    Discord::Permission::ADMINISTRATOR,
    Discord::Permission::MANAGE_CHANNELS,
    Discord::Permission::MANAGE_GUILD,
    Discord::Permission::MANAGE_MESSAGES,
    Discord::Permission::MANAGE_NICKNAMES,
    Discord::Permission::MANAGE_ROLES,
    Discord::Permission::MANAGE_WEBHOOKS,
    Discord::Permission::MANAGE_EXPRESSIONS,
    Discord::Permission::MOVE_MEMBERS,
    Discord::Permission::MUTE_MEMBERS,
    Discord::Permission::DEAFEN_MEMBERS,
};

bool isAutoModeration(Discord::AuditLogAction action)
{
    switch (action) {
    case Discord::AuditLogAction::AUTO_MODERATION_BLOCK_MESSAGE:
    case Discord::AuditLogAction::AUTO_MODERATION_FLAG_TO_CHANNEL:
    case Discord::AuditLogAction::AUTO_MODERATION_USER_COMMUNICATION_DISABLED:
    case Discord::AuditLogAction::AUTO_MODERATION_QUARANTINE_USER:
        return true;
    default:
        return false;
    }
}

QColor categoryColor(AuditLogFormatter::Category category)
{
    using Core::Theme::Token;
    switch (category) {
    case AuditLogFormatter::Category::Create:
        return Core::Theme::Manager::instance().color(Token::StatusOnline);
    case AuditLogFormatter::Category::Delete:
        return Core::Theme::Manager::instance().color(Token::ChatError);
    default:
        return Core::Theme::Manager::instance().color(Token::StatusIdle);
    }
}

QString linesHtml(const AuditLogFormatter::Row &row)
{
    const QString color = categoryColor(row.category).name();
    QString html;
    for (int i = 0; i < row.lines.size(); i++) {
        const AuditLogFormatter::Line &line = row.lines[i];
        html += QStringLiteral("<div style=\"margin-bottom:4px\"><span style=\"font-family:monospace; color:%1\">%2 —</span> %3")
                        .arg(color, QStringLiteral("%1").arg(i + 1, 2, 10, QLatin1Char('0')), line.html);
        if (!line.items.isEmpty()) {
            html += QStringLiteral("<ul style=\"margin-top:2px; margin-bottom:0px\">");
            for (const QString &item : line.items)
                html += QStringLiteral("<li>%1</li>").arg(item.toHtmlEscaped());
            html += QStringLiteral("</ul>");
        }
        html += QStringLiteral("</div>");
    }
    return html;
}

} // namespace

class AuditLogRowWidget : public QFrame
{
public:
    AuditLogRowWidget(const AuditLogFormatter::Row &row, QWidget *parent) : QFrame(parent), expandable(!row.lines.isEmpty())
    {
        setFrameShape(QFrame::StyledPanel);
        setCursor(expandable ? Qt::PointingHandCursor : Qt::ArrowCursor);

        auto *layout = new QVBoxLayout(this);
        layout->setContentsMargins(10, 8, 10, 8);
        layout->setSpacing(6);

        auto *header = new QHBoxLayout();
        header->setSpacing(10);
        avatar = new QLabel(this);
        avatar->setFixedSize(AvatarSize);
        header->addWidget(avatar, 0, Qt::AlignTop);

        auto *text = new QVBoxLayout();
        text->setSpacing(2);
        auto *title = new QLabel(row.titleHtml, this);
        title->setTextFormat(Qt::RichText);
        title->setWordWrap(true);
        title->setAttribute(Qt::WA_TransparentForMouseEvents);
        text->addWidget(title);
        auto *time = new QLabel(AuditLogFormatter::timeText(row.start, row.end), this);
        time->setForegroundRole(QPalette::PlaceholderText);
        QFont timeFont = time->font();
        timeFont.setPointSizeF(timeFont.pointSizeF() * 0.9);
        time->setFont(timeFont);
        time->setAttribute(Qt::WA_TransparentForMouseEvents);
        text->addWidget(time);
        header->addLayout(text, 1);

        chevron = new QLabel(this);
        chevron->setPixmap(Core::Theme::Icons::pixmap(Core::Theme::Icons::Name::ChevronRight, 16, Core::Theme::Token::PlaceholderText, devicePixelRatioF()));
        chevron->setVisible(expandable);
        header->addWidget(chevron, 0, Qt::AlignVCenter);
        layout->addLayout(header);

        details = new QLabel(linesHtml(row), this);
        details->setTextFormat(Qt::RichText);
        details->setWordWrap(true);
        details->setTextInteractionFlags(Qt::TextSelectableByMouse);
        details->setContentsMargins(AvatarSize.width() + 10, 4, 0, 0);
        details->hide();
        layout->addWidget(details);
    }

    QLabel *avatarLabel() const { return avatar; }

    void setExpanded(bool open)
    {
        if (!expandable)
            return;
        details->setVisible(open);
        chevron->setPixmap(Core::Theme::Icons::pixmap(open ? Core::Theme::Icons::Name::ChevronDown : Core::Theme::Icons::Name::ChevronRight,
                                                      16, Core::Theme::Token::PlaceholderText, devicePixelRatioF()));
    }

    [[nodiscard]] bool isExpanded() const { return details->isVisible(); }

    std::function<void()> onClicked;

protected:
    void mousePressEvent(QMouseEvent *event) override
    {
        if (event->button() == Qt::LeftButton && expandable && onClicked)
            onClicked();
        QFrame::mousePressEvent(event);
    }

private:
    bool expandable;
    QLabel *avatar;
    QLabel *chevron;
    QLabel *details;
};

GuildAuditLogPage::GuildAuditLogPage(Core::ClientInstance *instance, Core::ImageManager *images, Core::Snowflake guildId,
                                     QWidget *parent)
    : GuildSettingsPage(instance, images, guildId, parent), formatter(instance, guildId)
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(24, 20, 24, 16);

    auto *header = new QHBoxLayout();
    header->addWidget(makeTitle(tr("Audit Log"), this), 1);
    header->addWidget(makeFieldLabel(tr("Filter by User"), this));
    userFilter = new QComboBox(this);
    userFilter->setMinimumWidth(160);
    header->addWidget(userFilter);
    header->addSpacing(8);
    header->addWidget(makeFieldLabel(tr("Filter by Action"), this));
    actionFilter = new QComboBox(this);
    actionFilter->setMinimumWidth(180);
    actionFilter->setMaxVisibleItems(20);
    actionFilter->addItem(tr("All Actions"));
    for (const auto &action : AuditLogFormatter::actionFilters())
        actionFilter->addItem(action.second, static_cast<int>(action.first));
    header->addWidget(actionFilter);
    layout->addLayout(header);

    states = new QStackedWidget(this);
    scroll = new QScrollArea(states);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    rowsContainer = new QWidget(scroll);
    rowsLayout = new QVBoxLayout(rowsContainer);
    rowsLayout->setContentsMargins(0, 8, 8, 8);
    rowsLayout->setSpacing(6);
    loadMoreButton = new QPushButton(tr("Load More"), rowsContainer);
    loadMoreButton->hide();
    rowsLayout->addWidget(loadMoreButton, 0, Qt::AlignHCenter);
    rowsLayout->addStretch();
    scroll->setWidget(rowsContainer);
    states->addWidget(scroll);

    messageState = new QWidget(states);
    auto *messageLayout = new QVBoxLayout(messageState);
    messageLayout->addStretch();
    stateTitle = makeFieldLabel(QString(), messageState);
    stateTitle->setAlignment(Qt::AlignCenter);
    messageLayout->addWidget(stateTitle);
    stateText = makeDescription(QString(), messageState);
    stateText->setAlignment(Qt::AlignCenter);
    messageLayout->addWidget(stateText);
    messageLayout->addStretch();
    states->addWidget(messageState);
    layout->addWidget(states, 1);

    connect(userFilter, qOverload<int>(&QComboBox::currentIndexChanged), this, &GuildAuditLogPage::reload);
    connect(actionFilter, qOverload<int>(&QComboBox::currentIndexChanged), this, &GuildAuditLogPage::reload);
    connect(loadMoreButton, &QPushButton::clicked, this, &GuildAuditLogPage::fetchPage);
    connect(scroll->verticalScrollBar(), &QScrollBar::valueChanged, this, [this](int value) {
        if (hasOlderLogs && !loading && value > 0 && value == scroll->verticalScrollBar()->maximum())
            fetchPage();
    });
    connect(images, &Core::ImageManager::imageFetched, this, &GuildAuditLogPage::onImageFetched);
}

GuildAuditLogPage::~GuildAuditLogPage() = default;

void GuildAuditLogPage::load()
{
    fillUserFilter();
    reload();
}

bool GuildAuditLogPage::canViewLog() const
{
    return hasPermission(Discord::Permission::VIEW_AUDIT_LOG);
}

void GuildAuditLogPage::fillUserFilter()
{
    if (!instance)
        return;

    const Core::Snowflake current(userFilter->currentData().toULongLong());
    QSet<Core::Snowflake> moderatorRoles;
    for (const Discord::Role &role : instance->getRolesForGuild(guildId))
        for (Discord::Permission permission : ModerationPermissions)
            if (role.permissions->testFlag(permission))
                moderatorRoles.insert(role.id.get());
    auto moderates = [&moderatorRoles](const QList<Core::Snowflake> &roleIds) {
        return std::any_of(roleIds.cbegin(), roleIds.cend(),
                           [&moderatorRoles](Core::Snowflake roleId) { return moderatorRoles.contains(roleId); });
    };

    QList<QPair<QString, Core::Snowflake>> people;
    QSet<Core::Snowflake> listed;
    for (const Discord::Member &member : instance->users()->getKnownMembers(guildId)) {
        if (!member.user.hasValue() || !moderates(member.roles.valueOr({})))
            continue;
        const Core::Snowflake userId = member.user->id.get();
        listed.insert(userId);
        people.append({ member.user->tag(), userId });
    }
    QSet<Core::Snowflake> others = authorIds;
    if (const auto guild = instance->getGuild(guildId))
        others.insert(guild->ownerId.get());
    if (quint64(current) != 0)
        others.insert(current);
    for (Core::Snowflake userId : others) {
        if (listed.contains(userId))
            continue;
        auto user = formatter.user(userId);
        if (!user)
            user = instance->users()->getUser(userId);
        if (user)
            people.append({ user->tag(), userId });
    }
    std::sort(people.begin(), people.end(), [](const auto &a, const auto &b) { return a.first.localeAwareCompare(b.first) < 0; });

    QSignalBlocker blocker(userFilter);
    userFilter->clear();
    userFilter->addItem(tr("All Users"), QVariant::fromValue<quint64>(0));
    for (const auto &person : people)
        userFilter->addItem(person.first, QVariant::fromValue<quint64>(person.second));
    userFilter->setCurrentIndex(qMax(0, userFilter->findData(QVariant::fromValue<quint64>(current))));
}

void GuildAuditLogPage::reload()
{
    generation++;
    loading = false;
    automaticFetches = 0;
    oldestEntryId = Core::Snowflake();
    hasOlderLogs = false;
    clearRows();
    showState(tr("Loading…"), QString());
    fetchPage();
}

void GuildAuditLogPage::clearRows()
{
    expanded = nullptr;
    avatarTracker.clear();
    for (AuditLogRowWidget *row : rows)
        delete row;
    rows.clear();
    loadMoreButton->hide();
}

void GuildAuditLogPage::fetchPage()
{
    if (!instance || loading || !canViewLog())
        return;

    Discord::Client::AuditLogQuery query;
    if (oldestEntryId.isValid())
        query.before = oldestEntryId;
    const quint64 userId = userFilter->currentData().toULongLong();
    if (userId != 0)
        query.userId = Core::Snowflake(userId);
    if (actionFilter->currentData().isValid())
        query.actionType = static_cast<Discord::AuditLogAction>(actionFilter->currentData().toInt());

    loading = true;
    loadMoreButton->setEnabled(false);
    const int requestGeneration = generation;
    const bool firstPage = !oldestEntryId.isValid();
    QPointer<GuildAuditLogPage> self(this);
    instance->discord()->fetchAuditLog(guildId, query, [self, requestGeneration, firstPage](const Core::Result<Discord::AuditLog> &result) {
        if (!self || requestGeneration != self->generation)
            return;
        self->loading = false;
        self->loadMoreButton->setEnabled(true);

        if (!result.success()) {
            if (firstPage)
                self->showState(tr("This is Awkward"), tr("We broke something. Come back later."));
            return;
        }

        const Discord::AuditLog &page = *result.value;
        self->hasOlderLogs = page.entries.size() >= Discord::Client::AuditLogQuery::PageSize;
        if (!page.entries.isEmpty())
            self->oldestEntryId = page.entries.last().id.get();
        for (const Discord::AuditLogEntry &entry : page.entries)
            if (entry.userId.hasValue())
                self->authorIds.insert(entry.userId.get());

        self->appendRows(self->formatter.addPage(page));
        if (self->rows.isEmpty() && !self->hasOlderLogs) {
            self->showState(tr("No Logs Yet"), tr("Once moderators begin moderating, you can moderate the moderation here."));
            return;
        }
        self->states->setCurrentWidget(self->scroll);
        self->loadMoreButton->setVisible(self->hasOlderLogs);
        if (firstPage)
            self->fillUserFilter();
        self->fillMoreIfShort();
    });
}

void GuildAuditLogPage::fillMoreIfShort()
{
    if (!hasOlderLogs || loading || automaticFetches >= MaxAutomaticFetches)
        return;
    if (!rows.isEmpty() && rowsFillView())
        return;
    automaticFetches++;
    fetchPage();
}

bool GuildAuditLogPage::rowsFillView() const
{
    const QSize view = scroll->viewport()->size();
    const int height = rowsLayout->hasHeightForWidth() ? rowsLayout->heightForWidth(view.width()) : rowsLayout->sizeHint().height();
    return height > view.height();
}

void GuildAuditLogPage::appendRows(const QList<AuditLogFormatter::Row> &newRows)
{
    const int insertAt = rowsLayout->indexOf(loadMoreButton);
    int offset = 0;
    for (const AuditLogFormatter::Row &row : newRows) {
        auto *widget = new AuditLogRowWidget(row, rowsContainer);
        widget->onClicked = [this, widget]() { toggle(widget); };
        rowsLayout->insertWidget(insertAt + offset++, widget);
        rows.append(widget);

        QLabel *avatar = widget->avatarLabel();
        if (isAutoModeration(row.action)) {
            avatar->setPixmap(Core::Theme::Icons::pixmap(Core::Theme::Icons::Name::ShieldAlert, AvatarSize.width(),
                                                         Core::Theme::Token::PrimaryText, devicePixelRatioF()));
            continue;
        }
        const auto author = row.userId.isValid() ? formatter.user(row.userId) : std::nullopt;
        if (!author || !instance)
            continue;
        const QUrl url = instance->users()->getAvatarUrl(*author, guildId, 64);
        const QPixmap pixmap = avatarTracker.fetch(images, url, AvatarSize, QPointer<QLabel>(avatar), instance->accountId());
        avatar->setPixmap(roundedPixmap(pixmap, AvatarSize.width(), AvatarSize.width() / 2.0));
    }
}

void GuildAuditLogPage::toggle(AuditLogRowWidget *row)
{
    const bool open = !row->isExpanded();
    if (expanded && expanded != row)
        expanded->setExpanded(false);
    row->setExpanded(open);
    expanded = open ? row : nullptr;
}

void GuildAuditLogPage::showState(const QString &title, const QString &text)
{
    stateTitle->setText(title);
    stateText->setText(text);
    stateText->setVisible(!text.isEmpty());
    states->setCurrentWidget(messageState);
}

void GuildAuditLogPage::onImageFetched(const QUrl &url, const QSize &size, const QPixmap &pixmap)
{
    if (size != AvatarSize)
        return;
    const QPixmap rounded = roundedPixmap(pixmap, AvatarSize.width(), AvatarSize.width() / 2.0);
    avatarTracker.notify(url, [&rounded](const QPointer<QLabel> &avatar) {
        if (avatar)
            avatar->setPixmap(rounded);
    });
}

} // namespace UI
} // namespace Acheron
