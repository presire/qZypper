#include "ZyppManager.h"
#include "SearchOptions.h"

#include <zypp/PoolQuery.h>
#include <zypp/ResKind.h>
#include <zypp/sat/SolvAttr.h>

#include <algorithm>

namespace qZypper {

namespace {

constexpr int kMaxSearchFlags = 127; ///< SearchFlag に定義された全ビットの和

/**
 * @brief PoolQuery に照合方式を設定する。
 * @param query 対象クエリ
 * @param mode 照合方式
 */
void applyMatchMode(zypp::PoolQuery &query, SearchMatchMode mode)
{
    switch (mode) {
        case SearchMatchMode::Substring: query.setMatchSubstring(); break;
        case SearchMatchMode::Exact:     query.setMatchExact();     break;
        case SearchMatchMode::Glob:      query.setMatchGlob();      break;
        case SearchMatchMode::Regex:     query.setMatchRegex();     break;
    }
}

/**
 * @brief 1 つの SearchFlag に対応する属性検索をクエリへ追加する。
 * @param query 対象クエリ
 * @param flag 追加する検索フィールド
 * @param value 検索文字列
 */
void addFlagField(zypp::PoolQuery &query, SearchFlag flag, const std::string &value)
{
    using zypp::sat::SolvAttr;
    switch (flag) {
        case SearchFlag::Name:        query.addAttribute(SolvAttr::name, value);        break;
        case SearchFlag::Keywords:    query.addAttribute(SolvAttr::keywords, value);    break;
        case SearchFlag::Summary:     query.addAttribute(SolvAttr::summary, value);     break;
        case SearchFlag::Description: query.addAttribute(SolvAttr::description, value); break;
        case SearchFlag::Requires:    query.addDependency(SolvAttr::dep_requires, value); break;
        case SearchFlag::Provides:    query.addDependency(SolvAttr::dep_provides, value); break;
        case SearchFlag::FileList:    query.addAttribute(SolvAttr::filelist, value);    break;
    }
}

/**
 * @brief オプションから PoolQuery を構築する。
 * @param query 構築先
 * @param options 検証済みオプション
 */
void buildQuery(zypp::PoolQuery &query, const SearchOptions &options)
{
    applyMatchMode(query, options.match);
    query.setCaseSensitive(false);
    query.addKind(zypp::ResKind::package);

    const std::string value = options.query.toStdString();
    for (int bit = 0; bit <= 6; ++bit) {
        const int mask = 1 << bit;
        if (options.flags & mask)
            addFlagField(query, static_cast<SearchFlag>(mask), value);
    }

    if (options.installedOnly)
        query.setInstalledOnly();
    if (!options.repoAlias.isEmpty())
        query.addRepo(options.repoAlias.toStdString());
}

} // namespace

/**
 * @brief パッケージを検索する (既存 D-Bus シグネチャ互換)。
 *
 * SearchPackagesEx の既定オプション (部分一致・件数制限なし) へ委譲する。
 * @param query 検索クエリ文字列 (256 バイト超は拒否)
 * @param flags 検索フラグ (SearchFlag のビットマスク)
 * @return 一致したパッケージ情報のベクタ
 */
std::vector<PackageInfo> ZyppManager::searchPackages(const std::string &query, int flags) const
{
    if (query.size() > 256 || flags < 1 || flags > kMaxSearchFlags) {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_lastError = "Invalid search query or flags";
        return {};
    }

    SearchOptions options;
    options.query = QString::fromStdString(query);
    options.flags = flags;
    options.match = SearchMatchMode::Substring;

    return searchPackagesEx(options);
}

/**
 * @brief PoolQuery を用いた拡張パッケージ検索。
 *
 * 照合方式 (部分/完全/グロブ/正規表現)、ページング、インストール済み限定、
 * リポジトリ限定に対応する。
 * @param options 検証済み検索オプション
 * @return 一致したパッケージ情報のベクタ
 */
std::vector<PackageInfo> ZyppManager::searchPackagesEx(const SearchOptions &options) const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    std::vector<PackageInfo> result;

    if (!m_initialized)
        return result;

    if (options.query.toUtf8().size() > 256 || options.flags < 1
        || options.flags > kMaxSearchFlags) {
        m_lastError = "Invalid search query or flags";
        return result;
    }

    try {
        zypp::PoolQuery query;
        buildQuery(query, options);

        for (auto it = query.selectableBegin(); it != query.selectableEnd(); ++it) {
            const zypp::ui::Selectable::Ptr &sel = *it;
            if (sel)
                result.push_back(makePackageInfo(sel));
        }
    } catch (const zypp::MatchInvalidRegexException &e) {
        m_lastError = std::string("Invalid search regex: ") + e.msg();
        return {};
    } catch (const zypp::Exception &e) {
        m_lastError = "Search error: " + e.asString();
        return {};
    } catch (const std::exception &e) {
        m_lastError = std::string("Search error: ") + e.what();
        return {};
    }

    // ページングを決定的にするため、名前→バージョンの順に安定ソートする。
    std::sort(result.begin(), result.end(),
              [](const PackageInfo &lhs, const PackageInfo &rhs) {
                  if (lhs.name != rhs.name)
                      return lhs.name < rhs.name;
                  return lhs.version < rhs.version;
              });

    if (options.offset >= result.size())
        return {};
    if (options.offset > 0)
        result.erase(result.begin(),
                     result.begin() + static_cast<std::ptrdiff_t>(options.offset));
    if (options.limit > 0 && result.size() > options.limit)
        result.resize(options.limit);

    return result;
}

} // namespace qZypper
