#pragma once

#include <QPointer>
#include <QSet>
#include <QUrl>

#include "AuditLogFormatter.hpp"
#include "GuildSettingsPage.hpp"
#include "UI/AvatarRequestTracker.hpp"

class QComboBox;
class QLabel;
class QPushButton;
class QScrollArea;
class QStackedWidget;
class QVBoxLayout;

namespace Acheron {
namespace UI {

class AuditLogRowWidget;

class GuildAuditLogPage : public GuildSettingsPage
{
    Q_OBJECT
public:
    GuildAuditLogPage(Core::ClientInstance *instance, Core::ImageManager *images, Core::Snowflake guildId, QWidget *parent = nullptr);
    ~GuildAuditLogPage() override;

protected:
    void load() override;

private:
    [[nodiscard]] bool canViewLog() const;
    void reload();
    void fetchPage();
    void appendRows(const QList<AuditLogFormatter::Row> &rows);
    void clearRows();
    void fillUserFilter();
    void fillMoreIfShort();
    [[nodiscard]] bool rowsFillView() const;
    void toggle(AuditLogRowWidget *row);
    void showState(const QString &title, const QString &text);
    void onImageFetched(const QUrl &url, const QSize &size, const QPixmap &pixmap);

    QComboBox *userFilter;
    QComboBox *actionFilter;
    QStackedWidget *states;
    QScrollArea *scroll;
    QWidget *rowsContainer;
    QVBoxLayout *rowsLayout;
    QWidget *messageState;
    QLabel *stateTitle;
    QLabel *stateText;
    QPushButton *loadMoreButton;

    AuditLogFormatter formatter;
    QList<AuditLogRowWidget *> rows;
    QPointer<AuditLogRowWidget> expanded;
    AvatarRequestTracker<QPointer<QLabel>> avatarTracker;
    QSet<Core::Snowflake> authorIds;
    Core::Snowflake oldestEntryId;
    bool hasOlderLogs = false;
    bool loading = false;
    int generation = 0;
    int automaticFetches = 0;
};

} // namespace UI
} // namespace Acheron
