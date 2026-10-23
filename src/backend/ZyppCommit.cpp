#include <QFileInfo>
#include <algorithm>
#include <chrono>
#include <zypp/sat/Transaction.h>
#include <zypp/ZYppCommitResult.h>
#include <zypp-core/base/UserRequestException>
#include "ZyppManager.h"
#include "CommitInteraction.h"
#include "CommitReceivers.h"
#include "SnapshotInfo.h"
#include "RebootCheck.h"
#include "RepoPolicy.h"
#include "ZyppLogging.h"
#include "LicenseConfirmation.h"

namespace qZypper {

/**
 * @brief パッケージの変更をコミットする
 *
 * libzyppのコールバックレシーバーを登録して進捗を逐次通知する
 *
 * transactionStepListを走査して正確な件数とサイズを収集する
 *
 * コミット前にファイル競合の対話を行い、コミット中にメディア変更とRPMスクリプト出力の転送を行う
 *
 * コミット後に再起動要否とsnapperスナップショットを収集する
 *
 * @param progressCallback 進捗コールバック
 * @param stateCallback 状態イベントコールバック
 * @param askCallback 対話要求コールバック(nullの場合は安全側に倒れる)
 * @param scriptOutputCallback スクリプト出力コールバック(null許容)
 * @return コミット結果
 */
ZyppManager::CommitResult ZyppManager::commit(ProgressCallbackFn progressCallback,
                                              StateEventCallbackFn stateCallback,
                                              PromptAskFn askCallback,
                                              ScriptOutputFn scriptOutputCallback)
{
    std::lock_guard<std::mutex> lock(m_mutex);

    CommitResult result;

    if (!m_initialized) {
        result.errorMessage = "Backend not initialized";
        return result;
    }
    // 未確認の候補は副作用より前に拒否する
    // コミット内での対話や再解決は禁止する
    for (const auto &item : m_zypp->pool()) {
        // 削除候補にはライセンス承認が不要なため、導入または更新候補だけを検査する
        if (!item.status().transacts() || item.status().isToBeUninstalled()) {
            continue;
        }

        const auto solv = item.satSolvable();
        if (solv && solv.needToAcceptLicense()
            && (!item.status().isLicenceConfirmed()
                || !m_acceptedLicenses.count(licenseApprovalKey(solv.name(), solv.edition().asString(),
                    solv.arch().asString(), item.repoInfo().alias(), solv.licenseToConfirm())))) {
            // ConfirmLicensesを通らない未確認候補を、libzyppの副作用より前に必ず拒否する
            result.errorMessage = "License for package " + solv.name()
                + " is not confirmed; resolve and confirm licenses before applying changes";
            return result;
        }
    }

    // リポジトリ/プールの変更で、PoolItemを保持する既存の解決結果は無効になる
    m_problems.clear();
    m_distUpgradeMode = false;

    // スクリプト出力の収集器(例外経路でも結果へ反映するためtryの外で生成する)
    ScriptOutputAggregator scriptAggregator(scriptOutputCallback);
    auto finalizeResult = [this, &scriptAggregator, &result]() -> CommitResult & {
        cleanupLocalRpmsLocked();
        scriptAggregator.flush();
        result.scriptOutput = scriptAggregator.collected();
        result.rpmMessages = scriptAggregator.rpmMessages();
        result.scriptOutputTruncated = scriptAggregator.truncated();
        return result;
    };

    // ユーザが[Ignore]したパッケージ名を
    // 成功として扱わない
    // 例外経路でも結果へ反映するためtryの外で保持する
    std::vector<std::string> ignoredPackages;

    // [Ignore]したパッケージを結果の無視一覧と失敗一覧へ反映する
    // 失敗一覧に既にあるパッケージは数えない
    // 正常経路と例外経路の両方から呼び出しても二重計上しない
    auto recordIgnoredPackages = [&result, &ignoredPackages]() {
        result.ignoredPackages = ignoredPackages;

        for (const auto &pkg : ignoredPackages) {
            const bool alreadyFailed =
                std::find(result.failedPackages.begin(), result.failedPackages.end(), pkg)
                != result.failedPackages.end();

            // すでに失敗一覧へ入っているパッケージは二重計上しない
            if (alreadyFailed) {
                continue;
            }

            result.failedPackages.push_back(pkg);
            ++result.classification.errorCount;
        }
    };

    // キャンセル要求はstartWorkerでのみクリアされる
    // そのため事前検査中に届いた要求もここで検出できる
    // checkRepoOriginsはネットワークI/Oを伴う場合がある
    auto cancelledResult = [this, &result]() {
        markCancelled();
        result.classification.outcome = CommitOutcome::Cancelled;
        result.success = false;
        result.errorMessage = m_lastError;
        return result;
    };

    // コミット前にプール既知の全リポジトリのポリシーを検査する
    // 不適合があれば拒否する
    // NOTE: m_mutex保持中のため、ロックを持たないstaticなcheckRepoPolicyを直接呼ぶ
    for (const auto &known : m_zypp->pool().knownRepositories()) {
        // キャンセル後は追加のリポジトリ検査をせず、呼び出し側へ取消し結果を返す
        if (m_cancelRequested.load()) {
            return cancelledResult();
        }

        std::string policyErr;
        if (!RepoPolicy::checkRepoPolicy(known.info(), policyErr)) {
            result.success = false;
            result.errorMessage = "Commit refused, repository violates policy: " + policyErr;
            m_lastError = result.errorMessage;
            return result;
        }

        std::string originsErr;
        if (!RepoPolicy::checkRepoOrigins(known.info(), originsErr)) {
            result.success = false;
            result.errorMessage = "Commit refused, repository violates policy: " + originsErr;
            m_lastError = result.errorMessage;
            return result;
        }
    }
    if (m_cancelRequested.load()) {
        return cancelledResult();
    }

    // コミット中の対話用レシーバー(例外経路でも状態を参照できるようtryの外で生成する)
    MediaChangeReceiver mediaRcv(askCallback);
    FileConflictReceiver conflictRcv(askCallback);
    CommitScriptReceiver scriptRcv(scriptAggregator);
    RpmOutputReceiver rpmOutRcv(scriptAggregator);
    RpmLoglineReceiver rpmLogRcv(scriptAggregator);

    try {
        // 経過時間の計測を開始する
        auto startTime = std::chrono::steady_clock::now();

        // プールから予定アクション数を計算する
        // ダウンロードとインストール/削除の両方をステップとして数える
        int downloadSteps = 0;
        int actionSteps   = 0;
        for (auto it = m_zypp->pool().begin(); it != m_zypp->pool().end(); ++it) {
            if (it->status().transacts()) {
                ++actionSteps;
                // インストール/更新にはダウンロードも必要
                if (!it->status().isToBeUninstalled())
                    ++downloadSteps;
            }
        }

        int totalSteps = downloadSteps + actionSteps;
        qCDebug(lcZypp) << "commit: actionSteps=" << actionSteps << "downloadSteps=" << downloadSteps;

        if (totalSteps == 0) {
            qCDebug(lcZypp) << "commit: no packages to transact in pool";
            totalSteps = 1;
        }

        int completedSteps = 0;

        // コミット前のsnapperスナップショットを記録する(作成はせず番号の差分だけを検出する)
        const bool snapperPresent = QFileInfo::exists(QStringLiteral("/usr/bin/snapper")) && QFileInfo::exists(QStringLiteral("/.snapshots"));
        std::vector<SnapshotEntry> snapshotsBefore;

        if (snapperPresent)
            snapshotsBefore = readSnapshots(QStringLiteral("/.snapshots"));

        // Install/Remove/Downloadのproblem()コールバックで発生した
        // エラー詳細を保持する(commit失敗時にerrorMessageとして返す)
        std::string problemDetail;

        // コールバックレシーバーを登録する
        InstallReceiver installRcv(progressCallback, stateCallback, completedSteps, totalSteps, m_cancelRequested, askCallback, problemDetail, ignoredPackages);
        RemoveReceiver removeRcv(progressCallback, stateCallback, completedSteps, totalSteps, m_cancelRequested, askCallback, problemDetail, ignoredPackages);
        DownloadReceiver downloadRcv(progressCallback, stateCallback, completedSteps, totalSteps, m_cancelRequested, askCallback, problemDetail, ignoredPackages);
        InstallReceiverSA installRcvSA(progressCallback, stateCallback, completedSteps, totalSteps, m_cancelRequested, problemDetail);
        RemoveReceiverSA removeRcvSA(progressCallback, stateCallback, completedSteps, totalSteps, m_cancelRequested, problemDetail);
        installRcv.connect();
        removeRcv.connect();
        downloadRcv.connect();
        installRcvSA.connect();
        removeRcvSA.connect();
        mediaRcv.connect();
        conflictRcv.connect();
        scriptRcv.connect();
        rpmOutRcv.connect();
        rpmLogRcv.connect();

        // コミットを実行する
        zypp::ZYppCommitPolicy policy;
        policy.dryRun(false);
        policy.downloadMode(zypp::DownloadInAdvance);
        auto commitResult = m_zypp->commit(policy);

        // レシーバーを切断する
        installRcv.disconnect();
        removeRcv.disconnect();
        downloadRcv.disconnect();
        installRcvSA.disconnect();
        removeRcvSA.disconnect();
        mediaRcv.disconnect();
        conflictRcv.disconnect();
        scriptRcv.disconnect();
        rpmOutRcv.disconnect();
        rpmLogRcv.disconnect();
        scriptAggregator.flush();

        std::vector<CommitStep> stages;
        for (auto it = commitResult.transaction().actionBegin(); it != commitResult.transaction().actionEnd(); ++it) {
            CommitStepStage stage = CommitStepStage::Todo;

            switch (it->stepStage()) {
                case zypp::sat::Transaction::STEP_DONE:
                    stage = CommitStepStage::Done; break;
                case zypp::sat::Transaction::STEP_ERROR:
                    stage = CommitStepStage::Error; break;
                case zypp::sat::Transaction::STEP_TODO:
                    break;
            }

            stages.push_back({it->ident().asString(), stage});
        }

        result.classification = classifyCommit(stages, m_cancelRequested.load(), false);
        result.success = result.classification.outcome == CommitOutcome::Completed;
        result.failedPackages = result.classification.failedPackages;

        // ユーザが[Ignore]したパッケージは成功として扱わず、失敗一覧へ追加する
        recordIgnoredPackages();

        // [Ignore]が1件でもあればコミット全体を成功にしない
        if (!ignoredPackages.empty()) {
            result.success = false;

            if (result.classification.outcome == CommitOutcome::Completed) {
                result.classification.outcome = CommitOutcome::Partial;
            }
        }

        // 対話による中止(ファイル競合/メディア変更)はRPMステージ前のため
        // 取消しとして扱う
        if (conflictRcv.aborted()) {
            result.classification.outcome = CommitOutcome::Cancelled;
            result.success = false;
            const QString abortMsg = conflictRcv.abortMessage();
            result.errorMessage = abortMsg.isEmpty() ? std::string("Commit cancelled due to file conflicts") : abortMsg.toStdString();
            m_lastError = result.errorMessage;
        }
        else if (mediaRcv.aborted() && !result.success) {
            result.classification.outcome = CommitOutcome::Cancelled;
            result.errorMessage = "Media change was aborted by the user";
            m_lastError = result.errorMessage;
        }

        auto txSteps = std::distance(commitResult.transaction().begin(), commitResult.transaction().end());
        qCDebug(lcZypp) << "commit: noError=" << result.success << "transaction_steps=" << txSteps;

        // transaction全体を走査して正確な件数とサイズを収集する
        const auto &trans = commitResult.transaction();

        // 更新の判定に使う
        // 同一identがINSTALLとERASEの両方にあれば更新とみなす
        std::set<zypp::IdString> eraseIdents;
        std::set<zypp::IdString> installIdents;
        for (auto it = trans.begin(); it != trans.end(); ++it) {
            if (it->stepStage() != zypp::sat::Transaction::STEP_DONE)
                continue;

            if (it->stepType() == zypp::sat::Transaction::TRANSACTION_ERASE)
                eraseIdents.insert(it->ident());
            else if (it->stepType() == zypp::sat::Transaction::TRANSACTION_INSTALL || it->stepType() == zypp::sat::Transaction::TRANSACTION_MULTIINSTALL)
                installIdents.insert(it->ident());
        }

        for (auto it = trans.begin(); it != trans.end(); ++it) {
            if (it->stepType() == zypp::sat::Transaction::TRANSACTION_IGNORE)
                continue;

            std::string name = it->ident().asString();

            if (it->stepStage() == zypp::sat::Transaction::STEP_DONE) {
                auto type = it->stepType();

                if (type == zypp::sat::Transaction::TRANSACTION_INSTALL || type == zypp::sat::Transaction::TRANSACTION_MULTIINSTALL) {
                    // サイズを集計する
                    auto solv = it->satSolvable();
                    if (solv) {
                        result.totalInstalledSize += solv.installSize();
                        result.totalDownloadSize  += solv.downloadSize();
                    }

                    // 更新か新規インストールかを判定する
                    if (eraseIdents.count(it->ident())) {
                        result.updated++;
                        result.updatedPackages.push_back(name);
                    }
                    else {
                        result.installed++;
                        result.installedPackages.push_back(name);
                    }
                }
                else if (type == zypp::sat::Transaction::TRANSACTION_ERASE) {
                    // 同一identのINSTALLがなければ単純な削除とみなす
                    if (!installIdents.count(it->ident())) {
                        result.removed++;
                        result.removedPackages.push_back(name);
                    }
                }
            }
        }

        // コミット後にsnapperプラグインが作成したスナップショットを差分で検出する
        if (snapperPresent) {
            const std::vector<SnapshotEntry> snapshotsAfter = readSnapshots(QStringLiteral("/.snapshots"));
            result.snapshots = newSnapshots(snapshotsBefore, snapshotsAfter);
        }

        // 再起動の要否、再起動の推奨、サービス再起動を検出する(外部プロセスは起動しない)
        bool patchRebootSuggested = false;
        bool patchRestartSuggested = false;
        for (auto it = m_zypp->pool().begin(); it != m_zypp->pool().end(); ++it) {
            if (!it->status().isInstalled())
                continue;

            const zypp::Patch::constPtr patch = zypp::asKind<zypp::Patch>(it->resolvable());
            if (!patch)
                continue;

            patchRebootSuggested = patchRebootSuggested || patch->rebootSuggested();
            patchRestartSuggested = patchRestartSuggested || patch->restartSuggested();
        }

        result.rebootNeeded = rebootFlagPresent(QStringLiteral("/run/reboot-needed")) || patchRebootSuggested;
        result.restartSuggested = patchRestartSuggested;
        result.servicesToRestart = servicesUsingDeletedFiles(QStringLiteral("/proc"));

        // 経過時間を計算する
        auto endTime = std::chrono::steady_clock::now();
        result.elapsedSeconds = std::chrono::duration<double>(endTime - startTime).count();

        qCDebug(lcZypp) << "commit result: success=" << result.success
                        << "installed=" << result.installed
                        << "updated=" << result.updated
                        << "removed=" << result.removed
                        << "failed=" << result.failedPackages.size();

        // problem()コールバックで詳細が記録されていれば優先して採用する
        if (!result.success && result.errorMessage.empty()) {
            if (!problemDetail.empty())
                result.errorMessage = problemDetail;
            else
                result.errorMessage = "Transaction did not complete all requested actions";
        }

        // コミット成功時はlibzyppが要求ロケールを永続化する
        // そのため差分表示の基準も同じ内容へ追従させる(ドライランでは永続化しない)
        if (result.success && !policy.dryRun())
            m_savedRequestedLocales = zypp::sat::Pool::instance().getRequestedLocales();

        return finalizeResult();
    }
    catch (const zypp::Exception& e) {
        m_lastError = "Commit failed: " + e.asString();
        result.success = false;
        result.errorMessage = m_lastError;

        // 対話による中止が原因の例外は取消しとして報告する
        if (conflictRcv.aborted() || mediaRcv.aborted()) {
            result.classification.outcome = CommitOutcome::Cancelled;
            result.errorMessage = conflictRcv.aborted() ? std::string("Commit cancelled due to file conflicts")
                                                        : std::string("Media change was aborted by the user");
            m_lastError = result.errorMessage;
        }
        // 取消し要求に起因する中断例外だけを取消しとする
        // 取消し要求後でも無関係な例外は失敗のまま
        // 取消し要求がないABORT(problem()の中止など)も失敗とする
        else if (classifyCommitException(m_cancelRequested.load(),
                 dynamic_cast<const zypp::UserRequestException *>(&e) != nullptr) == CommitOutcome::Cancelled) {
            result.classification.outcome = CommitOutcome::Cancelled;
            result.errorMessage = "Commit was cancelled by the user";
            m_lastError = result.errorMessage;
        }

        qCWarning(lcZypp) << "commit exception:" << QString::fromStdString(e.asString())
                          << "| history:" << QString::fromStdString(e.historyAsString());

        // 例外の前にユーザが[Ignore]したパッケージも結果へ残す
        recordIgnoredPackages();
        return finalizeResult();
    }
    catch (const std::exception& e) {
        m_lastError = std::string("Commit failed: ") + e.what();
        result.success = false;
        result.errorMessage = m_lastError;

        if (conflictRcv.aborted() || mediaRcv.aborted()) {
            result.classification.outcome = CommitOutcome::Cancelled;
            result.errorMessage = conflictRcv.aborted() ? std::string("Commit cancelled due to file conflicts")
                                                        : std::string("Media change was aborted by the user");
            m_lastError = result.errorMessage;
        }

        qCWarning(lcZypp) << "commit std::exception:" << e.what();

        // 例外の前にユーザが[Ignore]したパッケージも結果へ残す
        recordIgnoredPackages();
        return finalizeResult();
    }
}

} // namespace qZypper
