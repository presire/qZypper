#include <algorithm>
#include <zypp/sat/Pool.h>
#include "ZyppManager.h"

namespace qZypper {
namespace {
/** @brief failedへ保持するパッケージ名の上限 */
constexpr std::size_t kMaxBatchFailures = 100;

/**
 * @brief 文字列が指定の接尾辞で終わるかを返す
 * @param value 検査する文字列
 * @param suffix 接尾辞
 * @return suffixで終わる場合はtrue
 */
bool endsWith(const std::string& value, const std::string& suffix)
{
    return value.size() >= suffix.size()
        && value.compare(value.size() - suffix.size(), suffix.size(), suffix) == 0;
}

/**
 * @brief サブパッケージの接尾辞で終わるかを返す
 * @param name パッケージ名
 * @return -devel/-debuginfo/-debugsourceのいずれかで終わる場合はtrue
 */
bool hasSubpackageSuffix(const std::string& name)
{
    return endsWith(name, "-devel") || endsWith(name, "-debuginfo") || endsWith(name, "-debugsource");
}
}

/**
 * @brief パッケージのステータスを変更する
 *
 * zyppステートマシンが直接遷移を拒否する場合はフォールバック遷移を試みる
 *
 * @param name パッケージ名
 * @param status 新しいステータス (PackageStatus)
 * @return 変更成功時true
 */
bool ZyppManager::setPackageStatus(const std::string& name, int status)
{
    std::lock_guard<std::mutex> lock(m_mutex);

    if (!m_initialized) {
        m_lastError = "Backend not initialized";
        return false;
    }

    zypp::ui::Status target;
    if (!toZyppStatus(status, target)) {
        m_lastError = "Invalid package status: " + std::to_string(status);
        return false;
    }

    // 選択が変わると前回のソルバー問題リストは無効になる
    m_problems.clear();

    auto pool = m_zypp->poolProxy();
    for (auto it = pool.byKindBegin<zypp::Package>();
         it != pool.byKindEnd<zypp::Package>(); ++it) {

        const auto& sel = *it;
        if (sel->name() == name)
            return applyStatusLocked(sel, target);
    }

    m_lastError = "Package not found: " + name;
    return false;
}

/**
 * @brief ステータス遷移とフォールバックを適用する
 *
 * zyppステートマシンが直接遷移を拒否する場合はフォールバック遷移を試みる
 *
 * @param sel 対象の選択可能オブジェクト
 * @param target 遷移先ステータス
 * @return 遷移に成功した場合はtrue
 */
bool ZyppManager::applyStatusLocked(const zypp::ui::Selectable::Ptr& sel, zypp::ui::Status target)
{
    if (sel->setStatus(target))
        return true;

    // zyppステートマシンが直接遷移を拒否 → フォールバック
    auto cur = sel->status();
    if (cur == zypp::ui::S_AutoInstall && target == zypp::ui::S_NoInst) {
        // AutoInstall → Taboo (自動インストール禁止)
        return sel->setStatus(zypp::ui::S_Taboo);
    }
    if (cur == zypp::ui::S_AutoDel && target == zypp::ui::S_KeepInstalled) {
        // AutoDel → KeepInstalled (自動削除キャンセル)
        return sel->setStatus(zypp::ui::S_KeepInstalled);
    }
    if (cur == zypp::ui::S_AutoUpdate && target == zypp::ui::S_KeepInstalled) {
        // AutoUpdate → KeepInstalled (自動更新キャンセル)
        return sel->setStatus(zypp::ui::S_KeepInstalled);
    }
    if (cur == zypp::ui::S_Taboo && target == zypp::ui::S_Install) {
        // Taboo → NoInst → Install
        // 途中で失敗したら元のTabooに戻す
        if (!sel->setStatus(zypp::ui::S_NoInst)) {
            m_lastError = "Status transition not allowed";
            return false;
        }
        if (sel->setStatus(zypp::ui::S_Install))
            return true;
        sel->setStatus(zypp::ui::S_Taboo);
        m_lastError = "Status transition not allowed";
        return false;
    }

    m_lastError = "Status transition not allowed";
    return false;
}

/**
 * @brief 複数パッケージへ同じ操作を一括適用する
 *
 * 名前は重複除去して処理し、Protected/Tabooはkeep以外の操作では変更しない
 *
 * @param names 対象パッケージ名
 * @param action install/remove/update/updateForce/keep/taboo/protect
 * @return 変更・スキップ・失敗の集計結果
 */
ZyppManager::BatchResult ZyppManager::setPackagesAction(const std::vector<std::string>& names,
                                                        const std::string& action)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    BatchResult result;

    if (!m_initialized) {
        m_lastError = "Backend not initialized";
        result.failedCount = static_cast<int>(names.size());
        const auto end = names.begin()
            + static_cast<std::ptrdiff_t>(std::min(names.size(), kMaxBatchFailures));
        result.failed.assign(names.begin(), end);
        return result;
    }

    m_problems.clear();

    const auto fail = [&result](const std::string& name) {
        ++result.failedCount;
        if (result.failed.size() < kMaxBatchFailures)
            result.failed.push_back(name);
    };

    std::set<std::string> seen;
    int processed = 0;
    for (const auto& name : names) {
        if (!seen.insert(name).second)
            continue;
        if (++processed % 1000 == 0)
            m_operation.heartbeat();

        const auto sel = zypp::ui::Selectable::get(zypp::ResKind::package, name);
        if (!sel) {
            fail(name);
            continue;
        }

        const bool installed = !sel->installedEmpty();
        const zypp::ui::Status current = sel->status();

        if (current == zypp::ui::S_Protected || current == zypp::ui::S_Taboo) {
            if (action != "keep") {
                ++result.skipped;
                continue;
            }
            const auto target = current == zypp::ui::S_Protected
                ? zypp::ui::S_KeepInstalled : zypp::ui::S_NoInst;
            if (applyStatusLocked(sel, target))
                ++result.changed;
            else
                fail(name);
            continue;
        }

        zypp::ui::Status target = current;
        zypp::PoolItem previousCandidate;
        bool candidateChanged = false;

        if (action == "install") {
            if (!installed) {
                target = zypp::ui::S_Install;
            }
            else if (const zypp::PoolItem update = sel->updateCandidateObj()) {
                previousCandidate = sel->candidateObj();
                sel->setCandidate(update);
                candidateChanged = true;
                target = zypp::ui::S_Update;
            }
            else {
                ++result.skipped;
                continue;
            }
        }
        else if (action == "remove") {
            if (installed) {
                target = zypp::ui::S_Del;
            }
            else if (current == zypp::ui::S_Install || current == zypp::ui::S_AutoInstall) {
                target = zypp::ui::S_NoInst;
            }
            else {
                ++result.skipped;
                continue;
            }
        }
        else if (action == "update" || action == "updateForce") {
            if (!installed) {
                ++result.skipped;
                continue;
            }
            const zypp::PoolItem update = action == "update"
                ? sel->updateCandidateObj() : sel->candidateObj();
            if (!update) {
                ++result.skipped;
                continue;
            }
            previousCandidate = sel->candidateObj();
            sel->setCandidate(update);
            candidateChanged = true;
            target = zypp::ui::S_Update;
        }
        else if (action == "keep") {
            target = installed ? zypp::ui::S_KeepInstalled : zypp::ui::S_NoInst;
        }
        else if (action == "taboo") {
            if (installed) {
                ++result.skipped;
                continue;
            }
            target = zypp::ui::S_Taboo;
        }
        else if (action == "protect") {
            if (!installed) {
                ++result.skipped;
                continue;
            }
            target = zypp::ui::S_Protected;
        }
        else {
            fail(name);
            continue;
        }

        if (target == current) {
            ++result.skipped;
            if (candidateChanged)
                sel->setCandidate(previousCandidate);
            continue;
        }

        if (applyStatusLocked(sel, target)) {
            ++result.changed;
        }
        else {
            if (candidateChanged)
                sel->setCandidate(previousCandidate);
            fail(name);
        }
    }

    return result;
}

/**
 * @brief インストール済みパッケージの対応するサブパッケージを選択する
 *
 * 対象接尾辞は-devel/-debuginfo/-debugsourceのみ
 *
 * @param suffix サブパッケージの接尾辞
 * @return 変更・スキップ・失敗の集計結果
 */
ZyppManager::BatchResult ZyppManager::selectSubpackages(const std::string& suffix)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    BatchResult result;

    if (suffix != "-devel" && suffix != "-debuginfo" && suffix != "-debugsource") {
        m_lastError = "Invalid subpackage suffix: " + suffix;
        return result;
    }
    if (!m_initialized) {
        m_lastError = "Backend not initialized";
        return result;
    }

    m_problems.clear();

    auto pool = m_zypp->poolProxy();
    int processed = 0;
    for (auto it = pool.byKindBegin<zypp::Package>();
         it != pool.byKindEnd<zypp::Package>(); ++it) {

        if (++processed % 1000 == 0)
            m_operation.heartbeat();

        const auto& sel = *it;
        if (sel->installedEmpty())
            continue;

        const std::string name = sel->name();
        if (hasSubpackageSuffix(name))
            continue;

        const std::string target = endsWith(name, "-32bit")
            ? name.substr(0, name.size() - 6) + suffix + "-32bit"
            : name + suffix;

        const auto sub = zypp::ui::Selectable::get(zypp::ResKind::package, target);
        if (!sub || !sub->installedEmpty() || !sub->candidateObj())
            continue;
        if (sub->status() != zypp::ui::S_NoInst)
            continue;

        if (sub->setStatus(zypp::ui::S_Install))
            ++result.changed;
        else
            ++result.skipped;
    }

    return result;
}

/**
 * @brief パッケージのバージョンを変更する
 *
 * 指定されたバージョン・アーキテクチャ・リポジトリに一致するPoolItemを候補として設定し、
 * インストール済みパッケージの場合はステータスをS_Update (またはS_KeepInstalled) に変更する
 *
 * 選択した版がインストール済みのいずれかの版と一致する場合はS_KeepInstalled (変更なし) とする
 *
 * YaSTのYQPkgVersionsView::checkForChangedCandidate()と同等の動作
 *
 * @param name パッケージ名
 * @param version 選択バージョン (edition文字列)
 * @param arch アーキテクチャ
 * @param repoAlias リポジトリエイリアス
 * @return 変更成功時true
 */
bool ZyppManager::setPackageVersion(const std::string& name, const std::string& version,
                                    const std::string& arch, const std::string& repoAlias)
{
    std::lock_guard<std::mutex> lock(m_mutex);

    if (!m_initialized)
        return false;

    auto pool = m_zypp->poolProxy();
    for (auto it = pool.byKindBegin<zypp::Package>();
         it != pool.byKindEnd<zypp::Package>(); ++it) {

        const auto& sel = *it;
        if (sel->name() != name)
            continue;

        // version + arch + repoAliasに一致するPoolItemを検索
        zypp::PoolItem matchedItem;
        for (auto avail = sel->availableBegin(); avail != sel->availableEnd(); ++avail) {
            if (avail->edition().asString() == version &&
                avail->arch().asString() == arch &&
                avail->repoInfo().alias() == repoAlias) {
                matchedItem = *avail;
                break;
            }
        }

        if (!matchedItem) {
            m_lastError = "Matching version not found: " + version;
            return false;
        }

        // 選択が変わると前回のソルバー問題リストは無効になる
        m_problems.clear();

        // 候補バージョンを設定 (状態遷移に失敗したら元の候補へ戻す)
        const zypp::PoolItem previousCandidate = sel->candidateObj();
        sel->setCandidate(matchedItem);

        auto rollback = [&]() {
            sel->setCandidate(previousCandidate);
            m_lastError = "Status transition not allowed";
            return false;
        };

        if (sel->installedEmpty()) {
            // 未インストール → 選択したバージョンをインストール対象にする (YaSTと同じ)
            if (sel->status() != zypp::ui::S_Install && !sel->setStatus(zypp::ui::S_Install))
                return rollback();
        }
        else {
            // 複数版を同時にインストールできるパッケージ (kernel等) を考慮し、インストール済みの全版と比較する
            // (getPackageDetails()のinstalled判定と同じ基準)
            //
            // installedObj()はソルバーが再インストール対象にした版を優先して返すため、1件だけとの比較では「変更なし」へ戻せない
            const bool isSameVersion = sel->identicalInstalled(matchedItem);

            if (isSameVersion) {
                // インストール済みのいずれかと同じバージョンを選択 → 変更なし
                // (S_KeepInstalledへの遷移で、ソルバーが付けた再インストール指定も解除される)
                auto cur = sel->status();
                if (cur != zypp::ui::S_KeepInstalled &&
                    cur != zypp::ui::S_Protected &&
                    !sel->setStatus(zypp::ui::S_KeepInstalled)) {
                    return rollback();
                }
            }
            else if (!sel->setStatus(zypp::ui::S_Update)) {
                // 異なるバージョンを選択 → 更新
                // フォールバック : AutoUpdate/AutoDel → KeepInstalled → Update
                auto cur = sel->status();
                const bool viaKeep = (cur == zypp::ui::S_AutoUpdate || cur == zypp::ui::S_AutoDel)
                                     && sel->setStatus(zypp::ui::S_KeepInstalled)
                                     && sel->setStatus(zypp::ui::S_Update);
                if (!viaKeep)
                    return rollback();
            }
        }

        return true;
    }

    m_lastError = "Package not found: " + name;
    return false;
}

/**
 * @brief パターンのステータスを変更する
 * @param name パターン名
 * @param status 新しいステータス (PackageStatus)
 * @return 変更成功時true
 */
bool ZyppManager::setPatternStatus(const std::string& name, int status)
{
    std::lock_guard<std::mutex> lock(m_mutex);

    if (!m_initialized) {
        m_lastError = "Backend not initialized";
        return false;
    }

    zypp::ui::Status target;
    if (!toZyppStatus(status, target)) {
        m_lastError = "Invalid pattern status: " + std::to_string(status);
        return false;
    }

    // 選択が変わると前回のソルバー問題リストは無効になる
    m_problems.clear();

    auto pool = m_zypp->poolProxy();
    for (auto it = pool.byKindBegin<zypp::Pattern>();
         it != pool.byKindEnd<zypp::Pattern>(); ++it) {

        const auto& sel = *it;
        if (sel->name() == name) {
            if (sel->setStatus(target))
                return true;
            m_lastError = "Status transition not allowed";
            return false;
        }
    }

    m_lastError = "Pattern not found: " + name;
    return false;
}

/**
 * @brief パッケージ・パターン・パッチの選択状態と要求ロケールを保存する
 */
void ZyppManager::saveState()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_initialized) {
        m_zypp->pool().proxy().saveState<zypp::Package>();
        m_zypp->pool().proxy().saveState<zypp::Pattern>();
        m_zypp->pool().proxy().saveState<zypp::Patch>();
        m_savedRequestedLocales = zypp::sat::Pool::instance().getRequestedLocales();
    }
}

/**
 * @brief 保存された選択状態と要求ロケールを復元する
 */
void ZyppManager::restoreState()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_initialized) {
        m_problems.clear();
        m_zypp->pool().proxy().restoreState<zypp::Package>();
        m_zypp->pool().proxy().restoreState<zypp::Pattern>();
        m_zypp->pool().proxy().restoreState<zypp::Patch>();
        zypp::sat::Pool::instance().setRequestedLocales(m_savedRequestedLocales);
    }
}

} // namespace qZypper
