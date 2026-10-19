#include <zypp/sat/Pool.h>
#include "ZyppManager.h"

namespace qZypper {

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
        if (sel->name() == name) {
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
    }

    m_lastError = "Package not found: " + name;
    return false;
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
 * @brief パッケージ・パターン・パッチの選択状態を保存する
 */
void ZyppManager::saveState()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_initialized) {
        m_zypp->pool().proxy().saveState<zypp::Package>();
        m_zypp->pool().proxy().saveState<zypp::Pattern>();
        m_zypp->pool().proxy().saveState<zypp::Patch>();
    }
}

/**
 * @brief 保存された選択状態を復元する
 */
void ZyppManager::restoreState()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_initialized) {
        m_problems.clear();
        m_zypp->pool().proxy().restoreState<zypp::Package>();
        m_zypp->pool().proxy().restoreState<zypp::Pattern>();
        m_zypp->pool().proxy().restoreState<zypp::Patch>();
    }
}

} // namespace qZypper
