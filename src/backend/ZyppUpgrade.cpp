#include <QDebug>
#include <algorithm>
#include <zypp/sat/Pool.h>
#include "ZyppManager.h"
#include "RepoPolicy.h"
#include "TransactionSummary.h"

namespace qZypper {

/**
 * @brief 検証済みのリポジトリ切り替え要求を保持する (依存解決は行わない)
 *
 * 空・不正なエイリアス (パストラバーサル・INIインジェクション)、不明なリポジトリ、
 * システムリポジトリ、ローカルRPMリポジトリは拒否する。libzyppの例外は捕捉して
 * m_lastErrorに格納する
 *
 * @param alias 切り替え対象のリポジトリエイリアス
 * @param enabled trueで切り替え対象に追加、falseで解除
 * @return 要求を受理した場合はtrue
 */
bool ZyppManager::setRepoUpgrade(const std::string& alias, bool enabled)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_initialized) {
        // 拒否理由を呼び出し側へ返せるよう、以前の操作の古いエラーを残さない
        m_lastError = "Backend not initialized";
        return false;
    }
    if (alias.empty()) {
        m_lastError = "Repository alias is empty";
        return false;
    }
    std::string err;
    if (!RepoPolicy::validateAlias(alias, err)) {
        m_lastError = "Invalid repository alias: " + err;
        return false;
    }
    try {
        const auto repo = zypp::sat::Pool::instance().reposFind(alias);
        if (repo == zypp::Repository::noRepository) {
            m_lastError = "Repository not found: " + alias;
            return false;
        }
        if (repo.isSystemRepo()) {
            m_lastError = "System repository cannot be switched";
            return false;
        }
        if (alias == m_localRpmAlias) {
            m_lastError = "Local RPM repository cannot be switched";
            return false;
        }
    }
    catch (const zypp::Exception& e) {
        m_lastError = "Repository switch error: " + e.asString();
        return false;
    }
    catch (const std::exception& e) {
        m_lastError = std::string("Repository switch error: ") + e.what();
        return false;
    }
    const auto found = std::find(m_upgradeRepoAliases.begin(), m_upgradeRepoAliases.end(), alias);
    if (enabled) {
        if (found == m_upgradeRepoAliases.end())
            m_upgradeRepoAliases.push_back(alias);
    }
    else if (found != m_upgradeRepoAliases.end()) {
        m_upgradeRepoAliases.erase(found);
    }
    return true;
}

/**
 * @brief 保持リストを変更せず、現在もプール内に存在する切り替え対象を返す
 *
 * libzyppの例外は捕捉してm_lastErrorに格納し、空リストを返す
 *
 * @return プール内に存在する切り替え対象のエイリアス一覧
 */
std::vector<std::string> ZyppManager::upgradeRepos() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    std::vector<std::string> aliases;
    try {
        for (const auto& alias : m_upgradeRepoAliases) {
            if (zypp::sat::Pool::instance().reposFind(alias) != zypp::Repository::noRepository)
                aliases.push_back(alias);
        }
    }
    catch (const zypp::Exception& e) {
        m_lastError = "Repository switch list error: " + e.asString();
        return {};
    }
    catch (const std::exception& e) {
        m_lastError = std::string("Repository switch list error: ") + e.what();
        return {};
    }
    return aliases;
}

/** @brief ロック保持中に切り替え要求とDUP専用のベンダー許可を再適用する */
void ZyppManager::syncUpgradeReposLocked(const zypp::Resolver_Ptr& resolver)
{
    resolver->removeUpgradeRepos();
    auto alias = m_upgradeRepoAliases.begin();
    while (alias != m_upgradeRepoAliases.end()) {
        const auto repo = zypp::sat::Pool::instance().reposFind(*alias);
        if (repo == zypp::Repository::noRepository || repo.isSystemRepo() || *alias == m_localRpmAlias) {
            alias = m_upgradeRepoAliases.erase(alias);
        }
        else {
            resolver->addUpgradeRepo(repo);
            ++alias;
        }
    }
    // libsolvのベンダー変更許可フラグはソルバ全体で共有される設定で、リポジトリ単位には
    // 指定できない。そのため切り替え対象が1つでもある間は、DistUpgrade が
    // allowVendorChange=false で呼ばれていても解決全体でベンダー変更を許可する。
    // 別ベンダーのリポジトリへ切り替える操作はベンダー変更を伴うため、許可しないと
    // 切り替えが成立しない。これは意図した挙動で、GUIの DistUpgradeDialog が
    // 切り替え中はベンダー変更が許可されることを利用者へ案内している
    const bool switching = !m_upgradeRepoAliases.empty();
    if (m_distUpgradeMode)
        resolver->dupSetAllowVendorChange(m_dupAllowVendorChange || switching);
    else if (switching)
        resolver->dupSetAllowVendorChange(true);
}

/**
 * @brief ディストリビューションアップグレード (zypper dup) を実行する
 *
 * libzyppのResolver::doUpgrade()を呼び出し、全リポジトリを跨いだディストリビューション更新を解決する
 * (openSUSE Tumbleweed向け)
 *
 * @param allowVendorChange ベンダ変更を許可するか
 * @return SolverResult (成功フラグ + 衝突リスト + リスクサマリ)
 */
ZyppManager::SolverResult ZyppManager::distUpgrade(bool allowVendorChange)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    SolverResult result;

    if (!m_initialized) {
        result.success = false;
        return result;
    }

    m_problems.clear();

    try {
        auto resolver = m_zypp->resolver();
        resolver->setDefaultSolverFlags();
        applySolverOverridesLocked(resolver);
        resolver->setIgnoreAlreadyRecommended(true);
        m_distUpgradeMode = true;
        m_dupAllowVendorChange = allowVendorChange;
        syncUpgradeReposLocked(resolver);
        resolver->setUpdateMode(false);
        result.success = resolver->doUpgrade();
    }
    catch (const zypp::Exception& e) {
        m_lastError = "Distribution upgrade error: " + e.asString();
        result.success = false;
        return result;
    }
    catch (const std::exception& e) {
        m_lastError = std::string("Distribution upgrade error: ") + e.what();
        result.success = false;
        return result;
    }

    if (!result.success) {
        // 衝突あり - 問題リストを取得
        try {
            m_problems = m_zypp->resolver()->problems();

            for (const auto& problem : m_problems) {
                ConflictInfo conflict;
                conflict.description = QString::fromStdString(problem->description());
                conflict.details     = QString::fromStdString(problem->details());

                auto solutions = problem->solutions();
                for (const auto& sol : solutions) {
                    QVariantMap solMap;
                    solMap["description"] = QString::fromStdString(sol->description());
                    solMap["details"]     = QString::fromStdString(sol->details());
                    conflict.solutions.append(solMap);
                }

                result.problems.push_back(std::move(conflict));
            }
        } catch (const std::exception& e) {
            qWarning() << "Failed to get solver problems:" << e.what();
        }
    }

    result.summary = computeTransactionSummaryLocked();
    return result;
}

} // namespace qZypper
