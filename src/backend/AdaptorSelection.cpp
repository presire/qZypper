#include <QDBusError>
#include "PackageManagerAdaptor.h"
#include "ZyppManager.h"

namespace qZypper {
namespace {
/**
 * @brief ソルバー結果をD-Bus返信用マップへ変換する
 * @param solver 変換する依存解決結果
 * @return success/problems/summaryを持つマップ
 */
QVariant solverMap(const ZyppManager::SolverResult &solver)
{
    QVariantList problems;
    for (const auto &problem : solver.problems) problems.append(problem.toVariantMap());
    return QVariantMap{{"success", solver.success}, {"problems", problems}, {"summary", solver.summary}};
}
}
/** @brief 選択の保存は引数なしの遅延返信を返す */
void PackageManagerAdaptor::SaveState()
{
    runOwnerSync([] { ZyppManager::instance().saveState(); return QVariant(); }, false);
}
/** @brief 選択状態を復元する */
void PackageManagerAdaptor::RestoreState()
{
    runOwnerSync([] { ZyppManager::instance().restoreState(); return QVariant(); });
}
/** @brief 列挙値を検証してパッケージ選択を変更する */
bool PackageManagerAdaptor::SetPackageStatus(const QString &name, int status)
{
    if (!ZyppManager::isValidPackageStatus(status)) {
        if (checkNotBusy() && checkOwner()) sendErrorReply(QDBusError::InvalidArgs, QStringLiteral("Invalid package status"));
        return false;
    }
    return runOwnerSync([name, status] { return QVariant(ZyppManager::instance().setPackageStatus(name.toStdString(), status)); }).toBool();
}
/** @brief 選択版を変更する */
bool PackageManagerAdaptor::SetPackageVersion(const QString &name, const QString &version, const QString &arch, const QString &repo)
{
    return runOwnerSync([name, version, arch, repo] {
        return QVariant(ZyppManager::instance().setPackageVersion(name.toStdString(), version.toStdString(), arch.toStdString(), repo.toStdString()));
    }).toBool();
}
/** @brief 列挙値を検証してパターン選択を変更する */
bool PackageManagerAdaptor::SetPatternStatus(const QString &name, int status)
{
    if (!ZyppManager::isValidPackageStatus(status)) {
        if (checkNotBusy() && checkOwner()) sendErrorReply(QDBusError::InvalidArgs, QStringLiteral("Invalid pattern status"));
        return false;
    }
    return runOwnerSync([name, status] { return QVariant(ZyppManager::instance().setPatternStatus(name.toStdString(), status)); }).toBool();
}
/** @brief 全更新を選択し、結果を既存のマップ形式で返す */
QVariantMap PackageManagerAdaptor::UpdateAllPackages()
{
    return runOwnerSync([] { return solverMap(ZyppManager::instance().updateAllPackages()); }, true, true).toMap();
}
/** @brief 所有者の選択を解決する */
QVariantMap PackageManagerAdaptor::ResolveDependencies()
{
    return runOwnerSync([] { return solverMap(ZyppManager::instance().resolveDependencies()); }, true, true).toMap();
}
/** @brief 有効な解決策添字だけをワーカーへ渡す */
bool PackageManagerAdaptor::ApplySolution(int problemIndex, int solutionIndex)
{
    if (problemIndex < 0 || solutionIndex < 0) {
        if (checkNotBusy() && checkOwner()) sendErrorReply(QDBusError::InvalidArgs, QStringLiteral("Invalid solution indices"));
        return false;
    }
    return runOwnerSync([problemIndex, solutionIndex] { return QVariant(ZyppManager::instance().applySolution(problemIndex, solutionIndex)); }).toBool();
}

} // namespace qZypper
