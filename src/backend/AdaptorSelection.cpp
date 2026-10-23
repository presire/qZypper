#include <QDBusError>
#include <QSet>
#include <QStringList>
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

/**
 * @brief 一括操作の結果をD-Bus返信用マップへ変換する
 * @param batch 変換する一括操作結果
 * @return changed/skipped/failedCount/failedを持つマップ
 */
QVariantMap batchMap(const ZyppManager::BatchResult &batch)
{
    QStringList failed;
    for (const auto &name : batch.failed) failed.append(QString::fromStdString(name));
    return QVariantMap{{"changed", batch.changed}, {"skipped", batch.skipped},
                       {"failedCount", batch.failedCount}, {"failed", failed}};
}

/**
 * @brief 一括操作名として有効かを返す
 * @param action 検査する操作名
 * @return 7種類の操作のいずれかの場合はtrue
 */
bool validBatchAction(const QString &action)
{
    static const QSet<QString> actions{
        QStringLiteral("install"), QStringLiteral("remove"), QStringLiteral("update"),
        QStringLiteral("updateForce"), QStringLiteral("keep"), QStringLiteral("taboo"),
        QStringLiteral("protect")};
    return actions.contains(action);
}
}
/** @brief 選択状態を保存する (引数なし、返信はワーカー完了後に遅延で返す) */
void PackageManagerAdaptor::SaveState()
{
    runOwnerSync([] { ZyppManager::instance().saveState(); return QVariant(); }, false);
}
/** @brief 選択状態を復元する */
void PackageManagerAdaptor::RestoreState()
{
    runOwnerSync([] { ZyppManager::instance().restoreState(); return QVariant(); });
}
/** @brief 状態値を検証してパッケージの選択状態を変更する */
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
/**
 * @brief 引数を検証して複数版対応パッケージの特定版の選択状態を変更する
 *
 * @param name パッケージ名
 * @param version バージョン
 * @param arch アーキテクチャ
 * @param repoAlias リポジトリエイリアス
 * @param status 新しいステータス
 * @return 変更を受け付けた場合true
 * @note 引数不正時はD-Busエラー応答を返し、選択状態の変更はrunOwnerSyncで所有者に限定する
 */
bool PackageManagerAdaptor::SetPackageVersionStatus(const QString &name, const QString &version,
                                                    const QString &arch, const QString &repoAlias,
                                                    int status)
{
    // 名前・版・arch・リポジトリエイリアスのいずれかが空、または256バイトを超える場合は受け付けない
    const bool validText = !name.isEmpty() && !version.isEmpty() && !arch.isEmpty() && !repoAlias.isEmpty()
                           && name.toUtf8().size() <= 256 && version.toUtf8().size() <= 256
                           && arch.toUtf8().size() <= 256 && repoAlias.toUtf8().size() <= 256;

    if (!validText || !ZyppManager::isValidPackageStatus(status)) {
        // 所有者からの呼び出しで実行中でない場合のみ、引数が不正であることをエラー応答で返す
        if (checkNotBusy() && checkOwner()) {
            sendErrorReply(QDBusError::InvalidArgs, QStringLiteral("Invalid package version status arguments"));
        }

        // 選択状態は変更せず、呼び出し元には失敗を返す
        return false;
    }

    // ZyppManager::setPackageVersionStatusを呼び出して、版単位で選択状態を変更する
    return runOwnerSync([name, version, arch, repoAlias, status] {
        return QVariant(ZyppManager::instance().setPackageVersionStatus(
            name.toStdString(), version.toStdString(), arch.toStdString(), repoAlias.toStdString(), status));
    }).toBool();
}
/** @brief 状態値を検証してパターンの選択状態を変更する */
bool PackageManagerAdaptor::SetPatternStatus(const QString &name, int status)
{
    if (!ZyppManager::isValidPackageStatus(status)) {
        if (checkNotBusy() && checkOwner()) sendErrorReply(QDBusError::InvalidArgs, QStringLiteral("Invalid pattern status"));
        return false;
    }
    return runOwnerSync([name, status] { return QVariant(ZyppManager::instance().setPatternStatus(name.toStdString(), status)); }).toBool();
}
/** @brief 引数を検証して複数のパッケージへ同じ操作を一括適用する */
QVariantMap PackageManagerAdaptor::SetPackagesAction(const QStringList &names, const QString &action)
{
    if (names.isEmpty() || !validBatchAction(action)) {
        if (checkNotBusy() && checkOwner()) sendErrorReply(QDBusError::InvalidArgs, QStringLiteral("Invalid package batch arguments"));
        return {};
    }
    return runOwnerSync([names, action] {
        std::vector<std::string> targets;
        targets.reserve(static_cast<std::size_t>(names.size()));
        for (const auto &name : names) targets.push_back(name.toStdString());
        return QVariant(batchMap(ZyppManager::instance().setPackagesAction(targets, action.toStdString())));
    }, true, false).toMap();
}
/** @brief 接尾辞を検証し、対応するサブパッケージを選択する */
QVariantMap PackageManagerAdaptor::SelectSubpackages(const QString &suffix)
{
    if (suffix != QStringLiteral("-devel") && suffix != QStringLiteral("-debuginfo")
        && suffix != QStringLiteral("-debugsource")) {
        if (checkNotBusy() && checkOwner()) sendErrorReply(QDBusError::InvalidArgs, QStringLiteral("Invalid subpackage suffix"));
        return {};
    }
    return runOwnerSync([suffix] {
        return QVariant(batchMap(ZyppManager::instance().selectSubpackages(suffix.toStdString())));
    }, true, false).toMap();
}
/** @brief 推奨パッケージをすべて選択し、ソルバー結果をマップで返す */
QVariantMap PackageManagerAdaptor::SelectAllRecommended()
{
    return runOwnerSync([] { return solverMap(ZyppManager::instance().selectAllRecommended()); }, true, true).toMap();
}
/** @brief 適用可能な更新をすべて選択し、ソルバー結果をマップで返す */
QVariantMap PackageManagerAdaptor::UpdateAllPackages()
{
    return runOwnerSync([] { return solverMap(ZyppManager::instance().updateAllPackages()); }, true, true).toMap();
}
/** @brief 所有者の編集セッションで依存関係を解決する */
QVariantMap PackageManagerAdaptor::ResolveDependencies()
{
    return runOwnerSync([] { return solverMap(ZyppManager::instance().resolveDependencies()); }, true, true).toMap();
}
/** @brief 解決策の添字を検証してから選択した解決策を適用する */
bool PackageManagerAdaptor::ApplySolution(int problemIndex, int solutionIndex)
{
    if (problemIndex < 0 || solutionIndex < 0) {
        if (checkNotBusy() && checkOwner()) sendErrorReply(QDBusError::InvalidArgs, QStringLiteral("Invalid solution indices"));
        return false;
    }
    return runOwnerSync([problemIndex, solutionIndex] { return QVariant(ZyppManager::instance().applySolution(problemIndex, solutionIndex)); }).toBool();
}

} // namespace qZypper
