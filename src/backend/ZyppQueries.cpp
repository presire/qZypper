#include <algorithm>
#include <zypp/sat/Pool.h>
#include <zypp/PoolQuery.h>
#include <zypp/ResObject.h>
#include "ZyppManager.h"

namespace qZypper {

/**
 * @brief パッケージの詳細情報を取得する
 *
 * 依存関係、ファイル一覧、利用可能バージョン等を含む
 *
 * @param name パッケージ名
 * @return パッケージ詳細情報
 */
PackageDetails ZyppManager::getPackageDetails(const std::string& name) const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    PackageDetails details;

    if (!m_initialized)
        return details;

    auto pool = m_zypp->poolProxy();
    for (auto it = pool.byKindBegin<zypp::Package>();
         it != pool.byKindEnd<zypp::Package>(); ++it) {

        const auto& sel = *it;
        if (sel->name() != name)
            continue;

        details.basic = makePackageInfo(sel);

        // 候補またはインストール済みオブジェクトからの詳細取得
        auto obj = sel->candidateObj() ? sel->candidateObj() : sel->installedObj();
        if (!obj)
            break;

        auto pkg = zypp::asKind<zypp::Package>(obj);
        if (!pkg)
            break;

        details.description = QString::fromStdString(pkg->description());
        details.license     = QString::fromStdString(pkg->license());
        details.group       = QString::fromStdString(pkg->group());
        details.vendor      = QString::fromStdString(pkg->vendor());
        details.buildHost   = QString::fromStdString(pkg->buildhost());
        details.sourceRpm   = QString::fromStdString(pkg->sourcePkgName() + "-" + pkg->sourcePkgEdition().asString());
        details.url         = QString::fromStdString(pkg->url());

        // 依存関係
        for (const auto& cap : pkg->dep(zypp::Dep::PROVIDES))
            details.provides << QString::fromStdString(cap.asString());
        for (const auto& cap : pkg->dep(zypp::Dep::REQUIRES))
            details.requires << QString::fromStdString(cap.asString());
        for (const auto& cap : pkg->dep(zypp::Dep::CONFLICTS))
            details.conflicts << QString::fromStdString(cap.asString());
        for (const auto& cap : pkg->dep(zypp::Dep::OBSOLETES))
            details.obsoletes << QString::fromStdString(cap.asString());
        for (const auto& cap : pkg->dep(zypp::Dep::RECOMMENDS))
            details.recommends << QString::fromStdString(cap.asString());
        for (const auto& cap : pkg->dep(zypp::Dep::SUGGESTS))
            details.suggests << QString::fromStdString(cap.asString());

        // ファイル一覧 (インストール済みパッケージのみ)
        if (auto instPkg = zypp::asKind<zypp::Package>(sel->installedObj())) {
            for (const auto& file : instPkg->filelist())
                details.fileList << QString::fromStdString(file);
        }

        // 利用可能バージョン
        for (auto avail = sel->availableBegin(); avail != sel->availableEnd(); ++avail) {
            QVariantMap ver;
            // インストール済みの全版 (kernel等の複数版同時インストール対応) のいずれかと同一か
            // identicalInstalled()はedition・archに加えてvendor・buildtimeも比較するため、
            // 同一版の別アーキテクチャや別ビルドをインストール済みと誤判定しない
            ver["installed"] = sel->identicalInstalled(*avail);
            ver["version"]   = QString::fromStdString(avail->edition().asString());
            ver["arch"]      = QString::fromStdString(avail->arch().asString());
            ver["repoAlias"] = QString::fromStdString(avail->repoInfo().alias());
            ver["repoName"]  = QString::fromStdString(avail->repoInfo().name());
            details.availableVersions.append(ver);
        }

        break;
    }

    return details;
}

/**
 * @brief 指定リポジトリに属するパッケージ一覧を取得する
 *
 * 利用可能なバージョンのいずれかが指定リポジトリにあるパッケージを返す (YaSTと同じ)
 * 候補バージョンのリポジトリでは判定しないため、ユーザが別リポジトリのバージョンを選択しても一覧から外れない
 *
 * @param repoAlias リポジトリエイリアス
 * @return パッケージ情報のベクタ
 */
std::vector<PackageInfo> ZyppManager::getPackagesByRepo(const std::string& repoAlias) const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    std::vector<PackageInfo> result;

    if (!m_initialized)
        return result;

    auto pool = m_zypp->poolProxy();
    for (auto it = pool.byKindBegin<zypp::Package>();
         it != pool.byKindEnd<zypp::Package>(); ++it) {

        const auto& sel = *it;
        for (auto avail = sel->availableBegin(); avail != sel->availableEnd(); ++avail) {
            if (avail->repoInfo().alias() == repoAlias) {
                result.push_back(makePackageInfo(sel));
                break;
            }
        }
    }

    return result;
}

// --- パターン・パッチ ---

/**
 * @brief ユーザ可視パターンの一覧を取得する
 *
 * 非表示パターンは除外し、orderでソートして返す
 *
 * @return PatternInfoのベクタ
 */
std::vector<PatternInfo> ZyppManager::getPatterns() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    std::vector<PatternInfo> result;

    if (!m_initialized)
        return result;

    auto pool = m_zypp->poolProxy();
    for (auto it = pool.byKindBegin<zypp::Pattern>();
         it != pool.byKindEnd<zypp::Pattern>(); ++it) {

        const auto& sel = *it;
        auto obj = sel->candidateObj() ? sel->candidateObj() : sel->installedObj();
        if (!obj)
            continue;

        auto pat = zypp::asKind<zypp::Pattern>(obj);
        if (!pat)
            continue;

        // 非表示パターンを除外 (YaSTと同様)
        if (!pat->userVisible())
            continue;

        PatternInfo info;
        info.name        = QString::fromStdString(sel->name());
        info.summary     = QString::fromStdString(pat->summary());
        info.description = QString::fromStdString(pat->description());
        info.category    = QString::fromStdString(pat->category());
        info.icon        = QString::fromStdString(pat->icon().asString());
        info.status      = fromZyppStatus(sel->status());

        try {
            info.order = std::stoi(pat->order());
        }
        catch (...) {
            info.order = 0;
        }

        result.push_back(std::move(info));
    }

    // orderでソート
    std::sort(result.begin(), result.end(), [](const PatternInfo& a, const PatternInfo& b) { return a.order < b.order; });

    return result;
}

/**
 * @brief 指定パターンに属するパッケージ一覧を取得する
 * @param patternName パターン名
 * @return パッケージ情報のベクタ
 */
std::vector<PackageInfo> ZyppManager::getPackagesByPattern(const std::string& patternName) const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    std::vector<PackageInfo> result;

    if (!m_initialized)
        return result;

    // パターンを検索
    auto pool = m_zypp->poolProxy();
    for (auto it = pool.byKindBegin<zypp::Pattern>();
         it != pool.byKindEnd<zypp::Pattern>(); ++it) {

        const auto& sel = *it;
        if (sel->name() != patternName)
            continue;

        auto obj = sel->candidateObj() ? sel->candidateObj() : sel->installedObj();
        if (!obj)
            break;

        auto pat = zypp::asKind<zypp::Pattern>(obj);
        if (!pat)
            break;

        // パターンの内容 (パッケージ一覧) を取得 (YaSTのYQPkgPatternList::filter()と同様)
        zypp::Pattern::Contents contents(pat->contents());
        for (auto cit = contents.selectableBegin();
             cit != contents.selectableEnd(); ++cit) {

            auto pkgSel = *cit;
            if (!pkgSel)
                continue;

            // パッケージのみ取得
            auto pkgObj = pkgSel->candidateObj() ? pkgSel->candidateObj() : pkgSel->installedObj();
            if (!pkgObj)
                continue;

            if (pkgObj->kind() == zypp::ResKind::package)
                result.push_back(makePackageInfo(pkgSel));
        }
        break;
    }

    return result;
}

/**
 * @brief パッチ一覧を取得する
 * @param category パッチカテゴリフィルタ (PatchCategory、-1で全件)
 * @return PatchInfoのベクタ
 */
std::vector<PatchInfo> ZyppManager::getPatches(int category) const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    std::vector<PatchInfo> result;

    if (!m_initialized)
        return result;

    auto pool = m_zypp->poolProxy();
    for (auto it = pool.byKindBegin<zypp::Patch>();
         it != pool.byKindEnd<zypp::Patch>(); ++it) {

        const auto& sel = *it;
        auto obj = sel->candidateObj() ? sel->candidateObj() : sel->installedObj();
        if (!obj)
            continue;

        auto patch = zypp::asKind<zypp::Patch>(obj);
        if (!patch)
            continue;

        // カテゴリフィルタ
        if (category >= 0 && category != static_cast<int>(PatchCategory::All)) {
            std::string cat = patch->category();
            bool match = false;

            switch (static_cast<PatchCategory>(category)) {
                case PatchCategory::Security:    match = (cat == "security"); break;
                case PatchCategory::Recommended: match = (cat == "recommended"); break;
                case PatchCategory::Optional:    match = (cat == "optional"); break;
                case PatchCategory::Feature:     match = (cat == "feature"); break;
                default: match = true; break;
            }

            if (!match)
                continue;
        }

        PatchInfo info;
        info.name        = QString::fromStdString(sel->name());
        info.summary     = QString::fromStdString(patch->summary());
        info.category    = QString::fromStdString(patch->category());
        info.severity    = QString::fromStdString(patch->severity());
        info.version     = QString::fromStdString(patch->edition().asString());
        info.status      = fromZyppStatus(sel->status());
        info.interactive = patch->interactive();

        const std::string pArch = obj.arch().asString();
        const std::string pRepo = obj.repoInfo().alias();
        info.id               = QString::fromStdString(makePatchId(sel->name(), patch->edition().asString(), pArch, pRepo));
        info.isNeeded         = obj.isNeeded();
        info.isRelevant       = obj.isRelevant();
        info.isSatisfied      = obj.isSatisfied();
        info.rebootSuggested  = patch->rebootSuggested();
        info.restartSuggested = patch->restartSuggested();
        info.selected         = sel->status() == zypp::ui::S_Install || sel->status() == zypp::ui::S_AutoInstall;

        result.push_back(std::move(info));
    }

    return result;
}

// -- 変更予定パッケージ一覧 --

/**
 * @brief 変更予定のパッケージ一覧を取得する
 *
 * Install, AutoInstall, Update, AutoUpdate, Del, AutoDelのステータスを持つパッケージを返す
 *
 * @return 変更予定パッケージ情報のベクタ
 */
std::vector<PackageInfo> ZyppManager::getPendingChanges() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    std::vector<PackageInfo> result;

    if (!m_initialized)
        return result;

    auto pool = m_zypp->poolProxy();
    for (auto it = pool.byKindBegin<zypp::Package>();
         it != pool.byKindEnd<zypp::Package>(); ++it) {

        const auto& sel = *it;
        auto st = sel->status();

        // Install, AutoInstall, Update, AutoUpdate, Del, AutoDelのみ
        if (st == zypp::ui::S_Install   || st == zypp::ui::S_AutoInstall ||
            st == zypp::ui::S_Update    || st == zypp::ui::S_AutoUpdate  ||
            st == zypp::ui::S_Del       || st == zypp::ui::S_AutoDel) {
            result.push_back(makePackageInfo(sel));
        }
    }

    return result;
}

} // namespace qZypper
