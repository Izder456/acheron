#pragma once

#include <QDateTime>
#include <QHash>
#include <QJsonObject>
#include <QList>
#include <QPair>
#include <QPointer>
#include <QString>
#include <QStringList>

#include <optional>

#include "Core/Snowflake.hpp"
#include "Discord/Entities.hpp"

namespace Acheron {
namespace Core {
class ClientInstance;
} // namespace Core
namespace UI {

class AuditLogFormatter
{
public:
    enum class Category {
        Other,
        Create,
        Update,
        Delete,
    };

    struct Line
    {
        QString html;
        QStringList items;
    };

    struct Row
    {
        Discord::AuditLogAction action;
        Core::Snowflake userId;
        QString titleHtml;
        QDateTime start;
        QDateTime end;
        Category category = Category::Other;
        QList<Line> lines;
    };

    AuditLogFormatter(Core::ClientInstance *instance, Core::Snowflake guildId);

    [[nodiscard]] QList<Row> addPage(const Discord::AuditLog &page);

    [[nodiscard]] std::optional<Discord::User> user(Core::Snowflake userId) const;

    [[nodiscard]] static QString timeText(const QDateTime &start, const QDateTime &end);
    [[nodiscard]] static QList<QPair<Discord::AuditLogAction, QString>> actionFilters();

private:
    enum class Target;
    struct Change;
    struct Entry;

    [[nodiscard]] static Target targetOf(Discord::AuditLogAction action);
    [[nodiscard]] static Category categoryOf(Discord::AuditLogAction action);
    [[nodiscard]] static bool neverMerged(Discord::AuditLogAction action);

    void rememberDeletedName(const Entry &entry);
    [[nodiscard]] Row format(const QList<Entry> &merged);
    [[nodiscard]] QString titleFor(const Entry &entry, const QString &target, const QString &count, const QString &channel, const QString &subtarget) const;
    [[nodiscard]] QString targetName(const Entry &entry) const;
    [[nodiscard]] QString authorName(const Entry &entry) const;
    [[nodiscard]] QList<Change> transform(const Entry &entry) const;
    [[nodiscard]] std::optional<Line> describe(const Entry &entry, const Change &change) const;
    [[nodiscard]] QString channelName(Core::Snowflake channelId) const;
    [[nodiscard]] QString roleName(Core::Snowflake roleId) const;
    [[nodiscard]] QString userTag(Core::Snowflake userId) const;

    QPointer<Core::ClientInstance> instance;
    Core::Snowflake guildId;

    QHash<Core::Snowflake, Discord::User> users;
    QHash<Core::Snowflake, QJsonObject> integrations;
    QHash<Core::Snowflake, QJsonObject> webhooks;
    QHash<Core::Snowflake, QJsonObject> scheduledEvents;
    QHash<Core::Snowflake, QJsonObject> autoModerationRules;
    QHash<Core::Snowflake, QJsonObject> threads;
    QHash<Core::Snowflake, QJsonObject> applicationCommands;
    QHash<QPair<int, Core::Snowflake>, QString> deletedNames;
};

} // namespace UI
} // namespace Acheron
