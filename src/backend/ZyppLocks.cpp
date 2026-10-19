#include <algorithm>
#include <iterator>
#include <set>
#include <string>
#include <zypp/Locks.h>
#include <zypp/PoolQuery.h>
#include <zypp/Edition.h>
#include <zypp/ResKind.h>
#include <zypp/ResPool.h>
#include <zypp/ui/Selectable.h>
#include <zypp/sat/SolvAttr.h>
#include "ZyppLocks.h"
#include "ZyppManager.h"
#include "RepoPolicy.h"

namespace qZypper {

namespace {

/**
 * @brief ロックで許可するリソース種別の集合を返す
 * @return 許可される種別名 (小文字)
 */
const std::set<std::string> &allowedKinds()
{
    static const std::set<std::string> kinds = {
        "package", "pattern", "patch", "product", "application",
        "srcpackage", "language", "filesystem"
    };
    return kinds;
}

/**
 * @brief ロックで許可するマッチ方式の集合を返す
 * @return 許可されるマッチ方式名 (小文字)
 */
const std::set<std::string> &allowedMatches()
{
    static const std::set<std::string> matches = {
        "exact", "substring", "glob", "regex", "word"
    };
    return matches;
}

/**
 * @brief 文字列に制御文字やパス区切りなどの禁止文字が含まれるか検査する
 * @param value 検査対象
 * @return 禁止文字を含む場合はtrue
 */
bool hasForbiddenChars(const QString &value)
{
    for (const QChar &ch : value) {
        const ushort code = ch.unicode();
        if (code < 0x20 || code == 0x7f) {
            return true;
        }
    }
    return value.contains(QLatin1Char('/')) || value.contains(QLatin1Char('\\'));
}

/**
 * @brief LockInfoからzypp::PoolQueryを構築する
 * @param info 正規化済みのロック情報
 * @param query 構築結果の出力先
 * @param error 失敗時のエラーメッセージ
 * @return 構築に成功した場合はtrue
 */
bool buildLockQuery(const LockInfo &info, zypp::PoolQuery &query, std::string &error)
{
    query.addKind(zypp::ResKind(info.kind.toStdString()));
    query.addAttribute(zypp::sat::SolvAttr::name, info.name.toStdString());

    if (!info.arch.isEmpty()) {
        query.addAttribute(zypp::sat::SolvAttr::arch, info.arch.toStdString());
    }

    if (!info.repoAlias.isEmpty()) {
        query.addRepo(info.repoAlias.toStdString());
    }

    if (!info.version.isEmpty()) {
        query.setEdition(zypp::Edition(info.version.toStdString()));
    }

    if (!info.comment.isEmpty()) {
        query.setComment(info.comment.toStdString());
    }

    const QString match = info.match;
    if (match == QLatin1String("glob")) {
        query.setMatchGlob();
    }
    else if (match == QLatin1String("regex")) {
        query.setMatchRegex();
    }
    else if (match == QLatin1String("word")) {
        query.setMatchWord();
    }
    else if (match == QLatin1String("substring")) {
        query.setMatchSubstring();
    }
    else {
        query.setMatchExact();
    }

    error.clear();
    return true;
}

/**
 * @brief ロック情報の指定項目がプールクエリと一致するか判定する
 * @param info 削除対象として指定されたロック情報
 * @param query 既存ロックのプールクエリ
 * @return 指定項目がすべて一致する場合はtrue
 */
bool lockMatches(const LockInfo &info, const zypp::PoolQuery &query)
{
    if (!info.kind.isEmpty()) {
        const std::string kind = info.kind.toStdString();
        bool found = false;

        for (const zypp::ResKind &candidate : query.kinds()) {
            if (candidate.asString() == kind) {
                found = true;
                break;
            }
        }

        if (!found) {
            return false;
        }
    }

    if (!info.name.isEmpty()
        && query.attribute(zypp::sat::SolvAttr::name).count(info.name.toStdString()) == 0) {
        return false;
    }

    if (!info.arch.isEmpty()
        && query.attribute(zypp::sat::SolvAttr::arch).count(info.arch.toStdString()) == 0) {
        return false;
    }

    if (!info.repoAlias.isEmpty()
        && query.repos().count(info.repoAlias.toStdString()) == 0) {
        return false;
    }

    if (!info.version.isEmpty()
        && query.edition().asString() != info.version.toStdString()) {
        return false;
    }

    return true;
}

/**
 * @brief PoolQueryのマッチ方式を文字列へ変換する
 * @param query 対象クエリ
 * @return マッチ方式名 (exact/substring/glob/regex/word)
 */
QString matchModeName(const zypp::PoolQuery &query)
{
    if (query.matchGlob()) {
        return QStringLiteral("glob");
    }

    if (query.matchRegex()) {
        return QStringLiteral("regex");
    }

    if (query.matchWord()) {
        return QStringLiteral("word");
    }

    if (query.matchSubstring()) {
        return QStringLiteral("substring");
    }

    return QStringLiteral("exact");
}

} // namespace

/**
 * @brief QVariantMapへ変換する
 * @return D-Bus転送用マップ
 */
QVariantMap LockInfo::toVariantMap() const
{
    QVariantMap map;
    map.insert(QStringLiteral("name"), name);
    map.insert(QStringLiteral("kind"), kind);
    map.insert(QStringLiteral("match"), match);
    map.insert(QStringLiteral("repoAlias"), repoAlias);
    map.insert(QStringLiteral("arch"), arch);
    map.insert(QStringLiteral("version"), version);

    if (!comment.isEmpty()) {
        map.insert(QStringLiteral("comment"), comment);
    }

    map.insert(QStringLiteral("matchCount"), matchCount);
    return map;
}

/**
 * @brief ロック用オプションマップを検証し正規化する
 * @param map D-Busから受け取ったロックマップ
 * @param out 正規化したロック情報 (成功時のみ有効)
 * @param error 失敗時のエラーメッセージ (英語)
 * @return 有効であればtrue
 */
bool validateLockMap(const QVariantMap &map, LockInfo &out, std::string &error)
{
    const QString name = map.value(QStringLiteral("name")).toString().trimmed();
    if (name.isEmpty()) {
        error = "lock 'name' is required";
        return false;
    }

    std::string nameError;

    if (!RepoPolicy::validateName(name.toStdString(), nameError)) {
        error = "invalid lock name: " + nameError;
        return false;
    }

    if (hasForbiddenChars(name)) {
        error = "invalid lock name";
        return false;
    }

    LockInfo info;
    info.name = name;
    info.kind = map.value(QStringLiteral("kind")).toString().trimmed().toLower();

    if (info.kind.isEmpty()) {
        info.kind = QStringLiteral("package");
    }

    if (allowedKinds().count(info.kind.toStdString()) == 0) {
        error = "invalid lock kind: " + info.kind.toStdString();
        return false;
    }

    info.match = map.value(QStringLiteral("match"),
                           map.value(QStringLiteral("glob"))).toString().trimmed().toLower();
    if (info.match.isEmpty()) {
        info.match = (name.contains(QLatin1Char('*')) || name.contains(QLatin1Char('?')))
                         ? QStringLiteral("glob")
                         : QStringLiteral("exact");
    }

    if (allowedMatches().count(info.match.toStdString()) == 0) {
        error = "invalid lock match mode: " + info.match.toStdString();
        return false;
    }

    info.arch = map.value(QStringLiteral("arch")).toString().trimmed();
    if (hasForbiddenChars(info.arch)) {
        error = "invalid lock arch";
        return false;
    }

    info.repoAlias = map.value(QStringLiteral("repoAlias")).toString().trimmed();
    if (!info.repoAlias.isEmpty()) {
        std::string aliasError;
        if (!RepoPolicy::validateAlias(info.repoAlias.toStdString(), aliasError)) {
            error = "invalid lock repoAlias: " + aliasError;
            return false;
        }
    }

    info.version = map.value(QStringLiteral("version"), map.value(QStringLiteral("edition"))).toString().trimmed();
    if (hasForbiddenChars(info.version)) {
        error = "invalid lock version";
        return false;
    }

    info.comment = map.value(QStringLiteral("comment")).toString();
    if (hasForbiddenChars(info.comment)) {
        error = "invalid lock comment";
        return false;
    }

    info.matchCount = -1;
    out = info;
    error.clear();
    return true;
}

/**
 * @brief 現在のロック一覧を取得する
 * @return ロック情報の一覧
 */
std::vector<LockInfo> ZyppManager::getLocks() const
{
    std::lock_guard<std::mutex> guard(m_mutex);
    std::vector<LockInfo> result;

    if (!m_initialized) {
        m_lastError = "libzypp not initialized";
        return result;
    }

    zypp::Locks &locks = zypp::Locks::instance();
    locks.read();  // /etc/zypp/locksを最新化 (存在しない場合は空)

    for (const zypp::PoolQuery &query : locks) {
        LockInfo info;
        info.kind = QStringLiteral("package");

        if (!query.kinds().empty()) {
            info.kind = QString::fromStdString(query.kinds().begin()->asString());
        }

        const auto &names = query.attribute(zypp::sat::SolvAttr::name);
        if (!names.empty()) {
            info.name = QString::fromStdString(*names.begin());
        }

        const auto &archs = query.attribute(zypp::sat::SolvAttr::arch);
        if (!archs.empty()) {
            info.arch = QString::fromStdString(*archs.begin());
        }

        if (!query.repos().empty()) {
            info.repoAlias = QString::fromStdString(*query.repos().begin());
        }

        const std::string edition = query.edition().asString();
        if (!edition.empty()) {
            info.version = QString::fromStdString(edition);
        }

        if (!query.comment().empty()) {
            info.comment = QString::fromStdString(query.comment());
        }

        info.match = matchModeName(query);
        info.matchCount = static_cast<int>(std::distance(query.begin(), query.end()));
        result.push_back(info);
    }
    return result;
}

/**
 * @brief ロックを追加する
 * @param info 追加するロック情報
 * @return 追加に成功した場合はtrue
 */
bool ZyppManager::addLock(const LockInfo &info)
{
    std::lock_guard<std::mutex> guard(m_mutex);

    if (!m_initialized) {
        m_lastError = "libzypp not initialized";
        return false;
    }

    zypp::PoolQuery query;
    if (!buildLockQuery(info, query, m_lastError)) {
        return false;
    }

    zypp::Locks &locks = zypp::Locks::instance();
    locks.read();
    locks.addLock(query);
    locks.save();
    locks.apply();
    return true;
}

/**
 * @brief 条件に一致するロックを削除する
 * @param info 削除対象を特定するロック情報
 * @return 削除に成功した場合はtrue
 */
bool ZyppManager::removeLock(const LockInfo &info)
{
    std::lock_guard<std::mutex> guard(m_mutex);
    if (!m_initialized) {
        m_lastError = "libzypp not initialized";
        return false;
    }

    zypp::Locks &locks = zypp::Locks::instance();
    locks.read();
    zypp::Locks::LockList toRemove;

    for (const zypp::PoolQuery &query : locks) {
        if (lockMatches(info, query)) {
            toRemove.push_back(query);
        }
    }

    if (toRemove.empty()) {
        m_lastError = "no matching lock found";
        return false;
    }

    // ロックが設定したtaboo状態を解除する (手動のprotectedは保持)
    for (const zypp::PoolQuery &query : toRemove) {
        for (auto it = query.begin(); it != query.end(); ++it) {
            const zypp::ui::Selectable::Ptr selectable = zypp::ui::Selectable::get(*it);

            if (selectable && selectable->status() == zypp::ui::S_Taboo) {
                selectable->setStatus(zypp::ui::S_NoInst);
            }
        }
    }

    for (const zypp::PoolQuery &query : toRemove) {
        locks.removeLock(query);
    }

    locks.save();
    locks.apply();
    return true;
}

} // namespace qZypper
