#include <QFileInfo>
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

namespace qZypper {

/**
 * @brief パッケージの変更をコミットする
 *
 * libzyppのコールバックレシーバーを登録して進捗をリアルタイム通知し、transactionStepListを走査して正確なカウント・サイズ情報を収集する
 *
 * コミット前に対話 (ライセンス確認・ファイル競合) を、コミット中にメディア変更・RPMスクリプト出力転送を行い、
 * コミット後に再起動要否とsnapperスナップショットを収集する
 *
 * @param progressCallback 進捗コールバック
 * @param stateCallback 状態イベントコールバック
 * @param askCallback 対話要求コールバック (nullの場合は安全側に倒れる)
 * @param scriptOutputCallback スクリプト出力コールバック (null許容)
 * @return コミット結果
 */
ZyppManager::CommitResult ZyppManager::commit(ProgressCallbackFn progressCallback,
                                              StateEventCallbackFn stateCallback,
                                              PromptAskFn askCallback,
                                              ScriptOutputFn scriptOutputCallback)
{
    std::lock_guard<std::mutex> lock(m_mutex);

    // リポジトリ/プールの変更で、PoolItemを保持する既存の解決策は無効になる
    m_problems.clear();
    m_distUpgradeMode = false;
    CommitResult result;

    // スクリプト出力アグリゲータ (例外経路でも結果へ反映するためtryの外で生成)
    ScriptOutputAggregator scriptAggregator(scriptOutputCallback);
    auto finalizeResult = [this, &scriptAggregator, &result]() -> CommitResult & {
        cleanupLocalRpmsLocked();
        scriptAggregator.flush();
        result.scriptOutput = scriptAggregator.collected();
        result.rpmMessages = scriptAggregator.rpmMessages();
        result.scriptOutputTruncated = scriptAggregator.truncated();
        return result;
    };

    if (!m_initialized) {
        result.success = false;
        result.errorMessage = "Backend not initialized";
        return result;
    }

    // キャンセル要求はstartWorkerでのみクリアされるため、事前検査中
    // (checkRepoOriginsはネットワークI/Oを伴い得る) に届いた要求もここで拾える
    auto cancelledResult = [this, &result]() {
        markCancelled();
        result.classification.outcome = CommitOutcome::Cancelled;
        result.success = false;
        result.errorMessage = m_lastError;
        return result;
    };

    // コミット前にプール既知の全リポジトリをポリシー検査
    // 不適合があれば拒否する
    // NOTE: m_mutex保持中のためcheckRepoPolicy (static、ロックなし) を直接呼ぶ
    for (const auto &known : m_zypp->pool().knownRepositories()) {
        if (m_cancelRequested.load())
            return cancelledResult();

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
    if (m_cancelRequested.load())
        return cancelledResult();

    // コミット中インタラクション用レシーバー (例外経路でも状態参照できるようtryの外で生成)
    MediaChangeReceiver mediaRcv(askCallback);
    FileConflictReceiver conflictRcv(askCallback);
    CommitScriptReceiver scriptRcv(scriptAggregator);
    RpmOutputReceiver rpmOutRcv(scriptAggregator);
    RpmLoglineReceiver rpmLogRcv(scriptAggregator);

    try {
        // 経過時間計測開始
        auto startTime = std::chrono::steady_clock::now();

        // アクション予定数をプールから計算
        // ダウンロード+インストール/削除の両方をステップとしてカウント
        int downloadSteps = 0;
        int actionSteps   = 0;
        for (auto it = m_zypp->pool().begin(); it != m_zypp->pool().end(); ++it) {
            if (it->status().transacts()) {
                ++actionSteps;
                // インストール/更新はダウンロードも必要
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

        // ライセンス確認 (RPMステージ前)
        // 未承認ライセンスがあれば所有者へ問い合わせ、拒否された場合はRPMステージに到達させずコミットを中止する
        if (askCallback) {
            for (auto it = m_zypp->pool().begin(); it != m_zypp->pool().end(); ++it) {
                if (m_cancelRequested.load()) {
                    finalizeResult();
                    return cancelledResult();
                }

                if (!it->status().transacts() || it->status().isToBeUninstalled())
                    continue;

                const zypp::sat::Solvable solv = it->satSolvable();
                if (!solv || !solv.needToAcceptLicense())
                    continue;

                const std::string licenseText = solv.licenseToConfirm();
                if (licenseText.empty())
                    continue;

                const std::string licenseKey = solv.name() + "|" + solv.edition().asString();
                if (m_acceptedLicenses.count(licenseKey) > 0)
                    continue;

                PromptRequest request;
                request.type = QStringLiteral("license");
                request.data = buildLicensePromptData(QString::fromStdString(solv.name()),
                                                      QString::fromStdString(solv.edition().asString()),
                                                      QString::fromStdString(licenseText));
                request.actions = {QStringLiteral("accept"), QStringLiteral("reject")};
                request.safeDefault = QVariantMap{{QStringLiteral("action"), QStringLiteral("reject")}};
                request.timeout = std::chrono::milliseconds(300000);

                if (!licenseAnswerAccepted(askCallback(request))) {
                    result.classification.outcome = CommitOutcome::Cancelled;
                    result.success = false;
                    result.errorMessage = "License for package " + solv.name() + " was not accepted";
                    m_lastError = result.errorMessage;
                    return finalizeResult();
                }

                m_acceptedLicenses.insert(licenseKey);
            }
        }

        // snapperスナップショットのコミット前観測 (作成はせず番号のみ差分検出)
        const bool snapperPresent = QFileInfo::exists(QStringLiteral("/usr/bin/snapper")) && QFileInfo::exists(QStringLiteral("/.snapshots"));
        std::vector<SnapshotEntry> snapshotsBefore;

        if (snapperPresent)
            snapshotsBefore = readSnapshots(QStringLiteral("/.snapshots"));

        // Install/Remove/Downloadのproblem()コールバックで発生したエラー詳細を格納する (commit失敗時にerrorMessageとして返す)
        std::string problemDetail;

        // コールバックレシーバー登録
        InstallReceiver installRcv(progressCallback, stateCallback, completedSteps, totalSteps, m_cancelRequested, problemDetail);
        RemoveReceiver removeRcv(progressCallback, stateCallback, completedSteps, totalSteps, m_cancelRequested, problemDetail);
        DownloadReceiver downloadRcv(progressCallback, stateCallback, completedSteps, totalSteps, m_cancelRequested, problemDetail);
        InstallReceiverSA installRcvSA(progressCallback, stateCallback, completedSteps, totalSteps, m_cancelRequested);
        RemoveReceiverSA removeRcvSA(progressCallback, stateCallback, completedSteps, totalSteps, m_cancelRequested);
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

        // コミット実行
        zypp::ZYppCommitPolicy policy;
        policy.dryRun(false);
        policy.downloadMode(zypp::DownloadInAdvance);
        auto commitResult = m_zypp->commit(policy);

        // レシーバー切断
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

        // 対話による中止 (ファイル競合/メディア変更) はRPMステージ前のためcancelledとする
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

        // transaction全体を走査して正確なカウント・サイズ情報を収集
        const auto &trans = commitResult.transaction();

        // 更新判定用: 同一identがINSTALLとERASEの両方にある場合は更新
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
                    // サイズ集計
                    auto solv = it->satSolvable();
                    if (solv) {
                        result.totalInstalledSize += solv.installSize();
                        result.totalDownloadSize  += solv.downloadSize();
                    }

                    // 更新vs新規インストール判定
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
                    // 同一identのINSTALLがなければ純粋な削除
                    if (!installIdents.count(it->ident())) {
                        result.removed++;
                        result.removedPackages.push_back(name);
                    }
                }
            }
        }

        // コミット後にsnapperプラグインが作成したスナップショットを差分検出
        if (snapperPresent) {
            const std::vector<SnapshotEntry> snapshotsAfter = readSnapshots(QStringLiteral("/.snapshots"));
            result.snapshots = newSnapshots(snapshotsBefore, snapshotsAfter);
        }

        // 再起動要否/再起動推奨/サービス再起動の検出 (外部プロセスは起動しない)
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

        // 経過時間計算
        auto endTime = std::chrono::steady_clock::now();
        result.elapsedSeconds = std::chrono::duration<double>(endTime - startTime).count();

        qCDebug(lcZypp) << "commit result: success=" << result.success
                        << "installed=" << result.installed
                        << "updated=" << result.updated
                        << "removed=" << result.removed
                        << "failed=" << result.failedPackages.size();

        // problem()コールバックで詳細が記録されていれば優先採用
        if (!result.success && result.errorMessage.empty()) {
            if (!problemDetail.empty())
                result.errorMessage = problemDetail;
            else
                result.errorMessage = "Transaction did not complete all requested actions";
        }

        return finalizeResult();
    }
    catch (const zypp::Exception& e) {
        m_lastError = "Commit failed: " + e.asString();
        result.success = false;
        result.errorMessage = m_lastError;

        // 対話中止が原因の例外はcancelledとして報告する
        if (conflictRcv.aborted() || mediaRcv.aborted()) {
            result.classification.outcome = CommitOutcome::Cancelled;
            result.errorMessage = conflictRcv.aborted() ? std::string("Commit cancelled due to file conflicts")
                                                        : std::string("Media change was aborted by the user");
            m_lastError = result.errorMessage;
        }
        // 取消し要求に起因する中断例外だけをcancelledとする
        // 取消し要求後でも無関係な例外は失敗のまま、取消し要求なしのABORT (problem()の中止等) も失敗とする
        else if (classifyCommitException(m_cancelRequested.load(),
                 dynamic_cast<const zypp::UserRequestException *>(&e) != nullptr) == CommitOutcome::Cancelled) {
            result.classification.outcome = CommitOutcome::Cancelled;
            result.errorMessage = "Commit was cancelled by the user";
            m_lastError = result.errorMessage;
        }

        qCWarning(lcZypp) << "commit exception:" << QString::fromStdString(e.asString())
                          << "| history:" << QString::fromStdString(e.historyAsString());

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
        return finalizeResult();
    }
}

} // namespace qZypper
