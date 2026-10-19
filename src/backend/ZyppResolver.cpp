#include "ZyppManager.h"

#include <zypp/ZConfig.h>

namespace qZypper {

/**
 * @brief 推奨パッケージをすべて選択して依存を解決する
 *
 * 既推奨の再展開を抑制しているフラグを一時的に解除し、
 * solver.onlyRequiresが有効な場合は依存のみに絞らないよう一時的に無効化する
 *
 * @return ソルバー結果 (success, problems)
 */
ZyppManager::SolverResult ZyppManager::selectAllRecommended()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    SolverResult result;

    if (!m_initialized) {
        result.success = false;
        return result;
    }

    m_problems.clear();

    bool onlyRequiresDisabled = false;
    bool solverThrew = false;
    try {
        auto resolver = m_zypp->resolver();
        resolver->setDefaultSolverFlags();
        applySolverOverridesLocked(resolver);
        resolver->setIgnoreAlreadyRecommended(false);

        onlyRequiresDisabled = !m_solverOverrides.installRecommends.value_or(
            !zypp::ZConfig::instance().solver_onlyRequires());
        if (onlyRequiresDisabled)
            resolver->setOnlyRequires(false);

        result.success = resolver->resolvePool();
    }
    catch (const zypp::Exception& e) {
        m_lastError = "Solver error: " + e.asString();
        result.success = false;
        solverThrew = true;
    }
    catch (const std::exception& e) {
        m_lastError = std::string("Solver error: ") + e.what();
        result.success = false;
        solverThrew = true;
    }

    // 例外が発生しても元のソルバー設定へ戻す
    try {
        auto resolver = m_zypp->resolver();
        resolver->setIgnoreAlreadyRecommended(true);
        if (onlyRequiresDisabled) {
            resolver->resetOnlyRequires();
            applySolverOverridesLocked(resolver);
        }
    }
    catch (const std::exception& e) {
        qWarning() << "Failed to restore solver flags:" << e.what();
    }

    // 例外時はソルバーが問題リストを生成していないため、復元後すぐに戻す
    if (solverThrew)
        return result;

    if (!result.success) {
        // 衝突あり: 問題リストを取得
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
        }
        catch (const std::exception& e) {
            qWarning() << "Failed to get solver problems:" << e.what();
        }
    }

    result.summary = computeTransactionSummaryLocked();
    return result;
}

/**
 * @brief 依存関係を解決する
 *
 * ソルバーを実行し、衝突がある場合は問題リストと解決策を返す
 *
 * @return ソルバー結果 (success, problems)
 */
ZyppManager::SolverResult ZyppManager::resolveDependencies()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    SolverResult result;

    if (!m_initialized) {
        result.success = false;
        return result;
    }

    // 前回の問題リストは今回の解決結果と無関係である
    // 古い番号のままApplySolutionされないよう、解決前に必ず破棄する
    m_problems.clear();

    // YaSTと同じ直接呼び出しパターン: fork()はlibzyppの内部状態を破壊するため使用しない
    try {
        auto resolver = m_zypp->resolver();
        resolver->setDefaultSolverFlags();            // zypp.confデフォルト再適用
        applySolverOverridesLocked(resolver);
        resolver->setIgnoreAlreadyRecommended(true);  // 既推奨パッケージの再展開を抑制

        if (m_distUpgradeMode) {
            // DistUpgrade後の確認・再解決ではdupの操作種別とオプションを維持する
            resolver->dupSetAllowVendorChange(m_dupAllowVendorChange);
            result.success = resolver->doUpgrade();
        }
        else {
            result.success = resolver->resolvePool();
        }
    }
    catch (const zypp::Exception& e) {
        m_lastError = "Solver error: " + e.asString();
        result.success = false;
        return result;
    }
    catch (const std::exception& e) {
        m_lastError = std::string("Solver error: ") + e.what();
        result.success = false;
        return result;
    }

    if (!result.success) {
        // 衝突あり: 問題リストを取得
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
        }
        catch (const std::exception& e) {
            qWarning() << "Failed to get solver problems:" << e.what();
        }
    }

    result.summary = computeTransactionSummaryLocked();
    return result;
}

/**
 * @brief 全パッケージの更新を実行する
 *
 * libzyppのResolver::doUpdate()を呼び出し、インストール済みパッケージのうち更新可能なものを全てマークし依存解決を行う
 *
 * @return SolverResult (成功フラグ + 衝突リスト)
 */
ZyppManager::SolverResult ZyppManager::updateAllPackages()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    SolverResult result;

    if (!m_initialized) {
        result.success = false;
        return result;
    }

    // 前回の問題リストは今回の解決結果と無関係
    // 古い番号のままApplySolutionされないよう、解決前に必ず破棄する
    m_problems.clear();

    try {
        auto resolver = m_zypp->resolver();
        resolver->setDefaultSolverFlags();
        applySolverOverridesLocked(resolver);
        resolver->setIgnoreAlreadyRecommended(true);
        m_distUpgradeMode = false;

        resolver->doUpdate();
        result.success = resolver->resolvePool();
    }
    catch (const zypp::Exception& e) {
        m_lastError = "Update all error: " + e.asString();
        result.success = false;
        return result;
    }
    catch (const std::exception& e) {
        m_lastError = std::string("Update all error: ") + e.what();
        result.success = false;
        return result;
    }

    if (!result.success) {
        // 衝突あり: 問題リストを取得
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
        }
        catch (const std::exception& e) {
            qWarning() << "Failed to get solver problems:" << e.what();
        }
    }

    result.summary = computeTransactionSummaryLocked();
    return result;
}

/**
 * @brief 依存関係の衝突に対する解決策を適用する
 * @param problemIndex 問題のインデックス
 * @param solutionIndex 解決策のインデックス
 * @return 適用成功時true
 */
bool ZyppManager::applySolution(int problemIndex, int solutionIndex)
{
    std::lock_guard<std::mutex> lock(m_mutex);

    if (!m_initialized) {
        m_lastError = "Backend not initialized";
        return false;
    }

    if (problemIndex < 0 || problemIndex >= static_cast<int>(m_problems.size())) {
        m_lastError = "No such solver problem; resolve dependencies again";
        return false;
    }

    auto problemIt = m_problems.begin();
    std::advance(problemIt, problemIndex);

    auto solutions = (*problemIt)->solutions();
    if (solutionIndex < 0 || solutionIndex >= static_cast<int>(solutions.size())) {
        m_lastError = "No such solution for the solver problem";
        return false;
    }

    auto solutionIt = solutions.begin();
    std::advance(solutionIt, solutionIndex);

    zypp::ProblemSolutionList solutionList;
    solutionList.push_back(*solutionIt);
    m_zypp->resolver()->applySolutions(solutionList);
    return true;
}

} // namespace qZypper
