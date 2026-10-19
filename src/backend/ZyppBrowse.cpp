#include <algorithm>
#include <map>
#include <set>
#include <string>
#include <vector>

#include <QRegularExpression>
#include <QString>
#include <QVariantList>
#include <QVariantMap>

#include <zypp/Locale.h>
#include <zypp/Package.h>
#include <zypp/ResPool.h>
#include <zypp/ResPoolProxy.h>
#include <zypp/ResTraits.h>
#include <zypp/sat/LocaleSupport.h>
#include <zypp/sat/Pool.h>
#include <zypp/sat/Solvable.h>
#include <zypp/ui/Selectable.h>

#include "ZyppManager.h"

namespace qZypper {
namespace {

/// グループ未設定のパッケージを表す仮想ノード名
constexpr char kUnspecifiedGroup[] = "Unspecified";

/**
 * @brief Selectableの代表オブジェクトからRPMグループ名を取り出す
 * @param sel 対象の選択可能オブジェクト
 * @return グループ名 (取得できない場合は空)
 */
std::string rpmGroupOf(const zypp::ui::Selectable::Ptr& sel)
{
    auto obj = sel->candidateObj() ? sel->candidateObj() : sel->installedObj();
    if (!obj)
        return {};

    auto pkg = zypp::asKind<zypp::Package>(obj);
    if (!pkg)
        return {};

    return pkg->group();
}

/**
 * @brief グループ名の全プレフィックスを浅い順に列挙する
 * @param group スラッシュ区切りのグループ名
 * @return "A"、"A/B"、"A/B/C" のようなパス一覧
 */
std::vector<std::string> groupPrefixes(const std::string& group)
{
    std::vector<std::string> prefixes;
    std::string current;

    std::size_t begin = 0;
    while (begin <= group.size()) {
        const std::size_t end = group.find('/', begin);
        const std::string segment = group.substr(
            begin, end == std::string::npos ? std::string::npos : end - begin);

        if (!segment.empty()) {
            if (!current.empty())
                current.push_back('/');
            current += segment;
            prefixes.push_back(current);
        }

        if (end == std::string::npos)
            break;
        begin = end + 1;
    }

    return prefixes;
}

/**
 * @brief ロケールコードとして受け付けられる形式かを判定する
 * @param code 検査するロケールコード
 * @return 言語[_国][@修飾子]の形式ならtrue
 */
bool isValidLocaleCode(const QString& code)
{
    static const QRegularExpression pattern(
        QStringLiteral("^[A-Za-z]{2,3}(_[A-Za-z0-9]{2,8})?(@[A-Za-z0-9]{1,16})?$"));
    return pattern.match(code).hasMatch();
}

} // namespace

/**
 * @brief RPMグループのツリーを件数付きで取得する
 *
 * 各パッケージのグループ名について全プレフィックスのノードを集計する
 *
 * @return path/label/depth/countを持つマップのリスト (path順、"Unspecified"は最後)
 */
QVariantList ZyppManager::getRpmGroups() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    QVariantList result;

    if (!m_initialized)
        return result;

    std::map<std::string, int> counts;
    auto pool = m_zypp->poolProxy();
    for (auto it = pool.byKindBegin<zypp::Package>();
         it != pool.byKindEnd<zypp::Package>(); ++it) {

        std::string group = rpmGroupOf(*it);
        if (group.empty())
            group = kUnspecifiedGroup;

        for (const auto& prefix : groupPrefixes(group))
            ++counts[prefix];
    }

    // std::mapはキー順に並ぶため、未設定ノードだけ後ろへ回す
    const auto appendNode = [&result](const std::string& path, int count) {
        const std::size_t slash = path.rfind('/');
        const std::string label =
            slash == std::string::npos ? path : path.substr(slash + 1);

        QVariantMap node;
        node.insert(QStringLiteral("path"), QString::fromStdString(path));
        node.insert(QStringLiteral("label"), QString::fromStdString(label));
        node.insert(QStringLiteral("depth"),
                    static_cast<int>(std::count(path.begin(), path.end(), '/')));
        node.insert(QStringLiteral("count"), count);
        result.append(node);
    };

    int unspecifiedCount = 0;
    for (const auto& entry : counts) {
        if (entry.first == kUnspecifiedGroup) {
            unspecifiedCount = entry.second;
            continue;
        }
        appendNode(entry.first, entry.second);
    }

    if (unspecifiedCount > 0)
        appendNode(kUnspecifiedGroup, unspecifiedCount);

    return result;
}

/**
 * @brief 指定RPMグループ配下のパッケージ一覧を取得する
 * @param path グループパス
 * @return グループが一致、または配下にあるパッケージ情報のベクタ
 */
std::vector<PackageInfo> ZyppManager::getPackagesByRpmGroup(const std::string& path) const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    std::vector<PackageInfo> result;

    if (!m_initialized || path.empty())
        return result;

    auto pool = m_zypp->poolProxy();
    for (auto it = pool.byKindBegin<zypp::Package>();
         it != pool.byKindEnd<zypp::Package>(); ++it) {

        // 未指定グループは集計側と同じ節点名に寄せる
        std::string group = rpmGroupOf(*it);
        if (group.empty())
            group = kUnspecifiedGroup;
        const bool same = group == path;
        const bool child = group.size() > path.size()
            && group.compare(0, path.size(), path) == 0
            && group[path.size()] == '/';

        if (same || child)
            result.push_back(makePackageInfo(*it));
    }

    return result;
}

/**
 * @brief 利用可能・要求済みロケールの一覧を取得する
 * @return code/name/requested/availableを持つマップのリスト (code順)
 */
QVariantList ZyppManager::getLocales() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    QVariantList result;

    if (!m_initialized)
        return result;

    const zypp::LocaleSet available = zypp::sat::Pool::instance().getAvailableLocales();
    const zypp::LocaleSet requested = zypp::sat::Pool::instance().getRequestedLocales();

    // 両集合の和集合をコード順に並べる
    std::set<std::string> codes;
    for (const auto& locale : available)
        codes.insert(locale.code());
    for (const auto& locale : requested)
        codes.insert(locale.code());

    for (const auto& code : codes) {
        const zypp::Locale locale(code);

        QVariantMap entry;
        entry.insert(QStringLiteral("code"), QString::fromStdString(code));
        entry.insert(QStringLiteral("name"), QString::fromStdString(locale.name()));
        entry.insert(QStringLiteral("requested"), requested.count(locale) > 0);
        entry.insert(QStringLiteral("available"), available.count(locale) > 0);
        result.append(entry);
    }

    return result;
}

/**
 * @brief 指定ロケールの要求状態を変更する
 * @param code ロケールコード
 * @param requested 要求する場合はtrue
 * @return 変更後の状態が要求と一致する場合はtrue
 */
bool ZyppManager::setLocaleRequested(const std::string& code, bool requested)
{
    std::lock_guard<std::mutex> lock(m_mutex);

    if (!m_initialized) {
        m_lastError = "Backend not initialized";
        return false;
    }

    const QString qcode = QString::fromStdString(code);
    if (!isValidLocaleCode(qcode)) {
        m_lastError = "Invalid locale code: " + code;
        return false;
    }

    m_lastError.clear();
    m_problems.clear();

    zypp::sat::Pool pool = zypp::sat::Pool::instance();
    const zypp::Locale locale(code);

    if (requested)
        pool.addRequestedLocale(locale);
    else
        pool.eraseRequestedLocale(locale);

    if (pool.isRequestedLocale(locale) != requested) {
        m_lastError = "Failed to update locale: " + code;
        return false;
    }

    return true;
}

/**
 * @brief 指定ロケールに対応するパッケージ一覧を取得する
 * @param code ロケールコード
 * @return 重複を除いたパッケージ情報のベクタ
 */
std::vector<PackageInfo> ZyppManager::getPackagesByLocale(const std::string& code) const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    std::vector<PackageInfo> result;

    if (!m_initialized || code.empty())
        return result;

    const zypp::sat::LocaleSupport support{zypp::Locale(code)};
    std::set<std::string> seen;

    for (auto it = support.begin(); it != support.end(); ++it) {
        const zypp::sat::Solvable solvable = *it;
        if (!solvable.isKind<zypp::Package>())
            continue;

        auto sel = zypp::ui::Selectable::get(solvable);
        if (!sel || !seen.insert(sel->ident().asString()).second)
            continue;

        result.push_back(makePackageInfo(sel));
    }

    return result;
}

/**
 * @brief 保存済み状態との差分として未反映のロケール変更を取得する
 * @return code/name/actionを持つマップのリスト (code順)
 */
QVariantList ZyppManager::getPendingLocaleChanges() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    QVariantList result;

    if (!m_initialized)
        return result;

    const zypp::LocaleSet& current = zypp::sat::Pool::instance().getRequestedLocales();

    std::map<std::string, QString> actions;
    for (const auto& locale : current) {
        if (!m_savedRequestedLocales.count(locale))
            actions[locale.code()] = QStringLiteral("add");
    }
    for (const auto& locale : m_savedRequestedLocales) {
        if (!current.count(locale))
            actions[locale.code()] = QStringLiteral("remove");
    }

    for (const auto& entry : actions) {
        QVariantMap change;
        change.insert(QStringLiteral("code"), QString::fromStdString(entry.first));
        change.insert(QStringLiteral("name"),
                      QString::fromStdString(zypp::Locale(entry.first).name()));
        change.insert(QStringLiteral("action"), entry.second);
        result.append(change);
    }

    return result;
}

} // namespace qZypper
