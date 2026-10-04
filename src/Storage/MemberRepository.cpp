#include "MemberRepository.hpp"

#include <QJsonArray>
#include <QJsonDocument>

#include "DatabaseManager.hpp"
#include "Transaction.hpp"
#include "Core/Logging.hpp"

namespace Acheron {
namespace Storage {

namespace {

const QString MemberColumns = QStringLiteral("m.nick, m.avatar, m.roles, m.joined_at, m.premium_since, m.deaf, m.mute, "
                                             "m.flags, m.pending, m.communication_disabled_until");
constexpr int MemberColumnCount = 10;

} // namespace

MemberRepository::MemberRepository(Core::Snowflake accountId)
    : BaseRepository(DatabaseManager::getCacheConnectionName(accountId))
{
}

void MemberRepository::deleteMembersForGuild(Core::Snowflake guildId, QSqlDatabase &db)
{
    QSqlQuery q(db);
    q.prepare("DELETE FROM members WHERE guild_id = :guild_id");
    q.bindValue(":guild_id", static_cast<qint64>(guildId));

    execLogged(q, "MemberRepository: Delete members for guild");
}

void MemberRepository::deleteMember(Core::Snowflake guildId, Core::Snowflake userId)
{
    auto db = getDb();
    QSqlQuery q(db);
    q.prepare("DELETE FROM members WHERE guild_id = :guild_id AND user_id = :user_id");
    q.bindValue(":guild_id", static_cast<qint64>(guildId));
    q.bindValue(":user_id", static_cast<qint64>(userId));

    execLogged(q, "MemberRepository: Delete member");
}

QString MemberRepository::rolesToJson(const QList<Core::Snowflake> &roles)
{
    QJsonArray arr;
    for (const auto &role : roles)
        arr.append(QString::number(static_cast<quint64>(role)));

    return QJsonDocument(arr).toJson(QJsonDocument::Compact);
}

QList<Core::Snowflake> MemberRepository::rolesFromJson(const QString &json)
{
    QList<Core::Snowflake> roles;
    QJsonDocument doc = QJsonDocument::fromJson(json.toUtf8());
    if (!doc.isArray())
        return roles;

    for (const auto &val : doc.array())
        roles.append(static_cast<Core::Snowflake>(val.toString().toULongLong()));

    return roles;
}

void MemberRepository::saveMember(Core::Snowflake guildId, Core::Snowflake userId,
                                  const Discord::Member &member)
{
    auto db = getDb();
    saveMember(guildId, userId, member, db);
}

void MemberRepository::saveMember(Core::Snowflake guildId, Core::Snowflake userId,
                                  const Discord::Member &member, QSqlDatabase &db)
{
    QSqlQuery q(db);
    q.prepare(R"(
        INSERT OR REPLACE INTO members
        (user_id, guild_id, nick, avatar, roles, joined_at, premium_since,
         deaf, mute, flags, pending, communication_disabled_until)
        VALUES (:user_id, :guild_id, :nick, :avatar, :roles, :joined_at, :premium_since,
                :deaf, :mute, :flags, :pending, :communication_disabled_until)
    )");

    q.bindValue(":user_id", static_cast<qint64>(userId));
    q.bindValue(":guild_id", static_cast<qint64>(guildId));
    bindOptional(q, ":nick", member.nick);
    bindOptional(q, ":avatar", member.avatar);
    q.bindValue(":roles", member.roles.hasValue() ? rolesToJson(member.roles.get()) : QVariant());
    bindOptional(q, ":joined_at", member.joinedAt);
    bindOptional(q, ":premium_since", member.premiumSince);
    bindOptional(q, ":deaf", member.deaf);
    bindOptional(q, ":mute", member.mute);
    bindOptional(q, ":flags", member.flags);
    bindOptional(q, ":pending", member.pending);
    bindOptional(q, ":communication_disabled_until", member.communicationDisabledUntil);

    execLogged(q, "MemberRepository: Save member");
}

void MemberRepository::saveMembers(Core::Snowflake guildId, const QList<Discord::Member> &members)
{
    if (members.isEmpty())
        return;

    auto db = getDb();
    Transaction txn(db);
    for (const auto &member : members) {
        if (!member.user.hasValue() || !member.user->id.hasValue())
            continue;
        saveMember(guildId, member.user->id.get(), member, db);
    }
    txn.commit();
}

std::optional<Discord::Member> MemberRepository::getMember(Core::Snowflake guildId,
                                                           Core::Snowflake userId)
{
    auto db = getDb();
    QSqlQuery q(db);
    q.prepare("SELECT " + MemberColumns + " FROM members m WHERE m.user_id = :user_id AND m.guild_id = :guild_id");
    q.bindValue(":user_id", static_cast<qint64>(userId));
    q.bindValue(":guild_id", static_cast<qint64>(guildId));

    if (!q.exec() || !q.next()) {
        return std::nullopt;
    }
    return readMember(q, 0);
}

Discord::Member MemberRepository::readMember(const QSqlQuery &q, int firstColumn)
{
    auto column = [&q, firstColumn](int index) { return q.value(firstColumn + index); };
    Discord::Member member;
    if (!column(0).isNull())
        member.nick = column(0).toString();
    if (!column(1).isNull())
        member.avatar = column(1).toString();
    if (!column(2).isNull())
        member.roles = rolesFromJson(column(2).toString());
    if (!column(3).isNull())
        member.joinedAt = column(3).toDateTime();
    if (!column(4).isNull())
        member.premiumSince = column(4).toDateTime();
    if (!column(5).isNull())
        member.deaf = column(5).toBool();
    if (!column(6).isNull())
        member.mute = column(6).toBool();
    if (!column(7).isNull())
        member.flags = column(7).toInt();
    if (!column(8).isNull())
        member.pending = column(8).toBool();
    if (!column(9).isNull())
        member.communicationDisabledUntil = column(9).toDateTime();
    return member;
}

QList<Discord::Member> MemberRepository::getMembersWithUsers(Core::Snowflake guildId)
{
    QList<Discord::Member> members;
    auto db = getDb();
    QSqlQuery q(db);
    q.prepare("SELECT m.user_id, " + MemberColumns +
              ", u.username, u.global_name, u.avatar, u.bot FROM members m JOIN users u ON u.id = m.user_id WHERE m.guild_id = :guild_id");
    q.bindValue(":guild_id", static_cast<qint64>(guildId));

    if (!execLogged(q, "MemberRepository: Get members with users"))
        return members;

    constexpr int FirstUserColumn = 1 + MemberColumnCount;
    while (q.next()) {
        Discord::Member member = readMember(q, 1);
        Discord::User user;
        user.id = static_cast<Core::Snowflake>(q.value(0).toLongLong());
        member.userId = user.id.get();
        user.username = q.value(FirstUserColumn).toString();
        if (q.value(FirstUserColumn + 1).isNull())
            user.globalName = nullptr;
        else
            user.globalName = q.value(FirstUserColumn + 1).toString();
        if (q.value(FirstUserColumn + 2).isNull())
            user.avatar = nullptr;
        else
            user.avatar = q.value(FirstUserColumn + 2).toString();
        if (!q.value(FirstUserColumn + 3).isNull())
            user.bot = q.value(FirstUserColumn + 3).toBool();
        member.user = user;
        members.append(member);
    }
    return members;
}

} // namespace Storage
} // namespace Acheron
