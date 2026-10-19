#include <QDebug>
#include <zypp/Patch.h>
#include <zypp/ResKind.h>
#include "ZyppManager.h"
#include "TransactionSummary.h"

namespace qZypper {

/**
 * @brief パッチの安定識別子を生成する
 *
 * 形式は、"name-edition.arch@repo"
 * getPatchesの結果に含め、setPatchStatusの入力として使用する
 *
 * @param name パッチ名
 * @param version エディション文字列
 * @param arch アーキテクチャ
 * @param repoAlias リポジトリエイリアス
 * @return 安定識別子
 */
std::string ZyppManager::makePatchId(const std::string &name, const std::string &version,
                                     const std::string &arch, const std::string &repoAlias)
{
    return name + "-" + version + "." + arch + "@" + repoAlias;
}

/**
 * @brief パッチの選択状態を変更する
 *
 * 識別子でパッチを特定し、Install / NoInst (NoChange) / Tabooを設定する
 *
 * @param id GetPatchesが返す安定識別子
 * @param status PackageStatus (Install / NoInst / Tabooのみ許可)
 * @return 設定成功時true
 */
bool ZyppManager::setPatchStatus(const std::string& id, int status)
{
    std::lock_guard<std::mutex> lock(m_mutex);

    if (!m_initialized) {
        m_lastError = "Backend not initialized";
        return false;
    }

    if (id.empty()) {
        m_lastError = "Empty patch identifier";
        return false;
    }

    zypp::ui::Status target = zypp::ui::S_NoInst;

    if (!toZyppStatus(status, target) || (target != zypp::ui::S_Install && target != zypp::ui::S_Taboo && target != zypp::ui::S_NoInst)) {
        m_lastError = "Invalid patch status";
        return false;
    }

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

        const std::string patchId = makePatchId(sel->name(), patch->edition().asString(),
                                                obj.arch().asString(),
                                                obj.repoInfo().alias());
        if (patchId != id)
            continue;

        // ロック等で変更できない場合libzyppはfalseを返す (成功扱いにしない)
        if (!sel->setStatus(target)) {
            m_lastError = "Cannot change status of patch: " + id;
            return false;
        }
        return true;
    }

    m_lastError = "No such patch: " + id;
    return false;
}

/**
 * @brief 必要なパッチを一括選択して依存解決する (zypper patch相当)
 *
 * カテゴリ・対話性・必要性でフィルタしたパッチをInstallに設定し、ソルバを実行する
 *
 * @param securityOnly セキュリティパッチのみを対象にするか
 * @param includeOptional 任意・機能パッチも対象にするか
 * @param skipInteractive 対話操作が必要なパッチを除外するか
 * @return SolverResult (成功フラグ + 衝突リスト + リスクサマリ)
 */
ZyppManager::SolverResult ZyppManager::selectAllNeededPatches(bool securityOnly,
                                                              bool includeOptional,
                                                              bool skipInteractive)
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
        m_distUpgradeMode = false;

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

            const std::string cat = patch->category();

            if (securityOnly && cat != "security")
                continue;

            if (!includeOptional && (cat == "optional" || cat == "feature"))
                continue;

            if (skipInteractive && patch->interactive())
                continue;

            if (!includeOptional && !sel->isNeeded())
                continue;

            sel->setStatus(zypp::ui::S_Install);
        }

        result.success = resolver->resolvePool();
    }
    catch (const zypp::Exception& e) {
        m_lastError = "Patch selection error: " + e.asString();
        result.success = false;
        return result;
    }
    catch (const std::exception& e) {
        m_lastError = std::string("Patch selection error: ") + e.what();
        result.success = false;
        return result;
    }

    if (!result.success) {
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

} // namespace qZypper
