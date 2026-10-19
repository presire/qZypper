#include <QDebug>
#include "ZyppManager.h"
#include "TransactionSummary.h"

namespace qZypper {

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
        // DUPは専用フラグ (zypper dup --allow-vendor-changeコマンドと同じ) を使用する
        resolver->dupSetAllowVendorChange(allowVendorChange);

        m_distUpgradeMode = true;
        m_dupAllowVendorChange = allowVendorChange;
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
