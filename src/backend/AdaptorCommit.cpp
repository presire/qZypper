#include "PackageManagerAdaptor.h"
#include "CommitInteraction.h"
#include "ZyppManager.h"
#include <chrono>
#include <memory>

namespace qZypper {
namespace {
/**
 * @brief パッケージ名のstd::string配列をQStringListへ変換する
 * @param packages 変換するパッケージ名の配列
 * @return D-Bus転送に使うパッケージ名リスト
 */
QStringList names(const std::vector<std::string> &packages)
{
    QStringList result;
    for (const auto &package : packages) {
        result.append(QString::fromStdString(package));
    }
    return result;
}
}

/**
 * @brief 解決済み選択だけを対象に、Polkit認証を伴わないライセンス確認をワーカーで行う
 * @return 常に空のマップ(応答は非同期で返す)
 * @note D-Bus所有者の回答を受けるため、ワーカー完了時まで遅延応答を保持する
 * @note 選択が未解決ならD-Busエラーを返し、ライセンス対話を開始しない
 */
QVariantMap PackageManagerAdaptor::ConfirmLicenses()
{
    // 所有者と引数を検証し、処理中の別要求との競合を防ぐ
    if (!preparePrivileged()) {
        return {};
    }

    // 依存関係解決後の候補だけを確認し、古い選択への承認を防ぐ
    if (m_resolvedRevision != m_revision) {
        sendErrorReply(QStringLiteral("org.presire.qzypper.Error.SelectionChanged"),
                       QStringLiteral("Package selection is unresolved; resolve and confirm again"));
        return {};
    }
    // ZyppManager::confirmLicensesをワーカーで呼び、所有者のD-Bus対話はexecutorへ委譲する
    startWorker(message(), [this] {
        AsyncOutcome result;
        result.value = ZyppManager::instance().confirmLicenses([this](const PromptRequest &request) {
            return m_executor.ask(request);
        }).toVariantMap();
        return result;
    });
    return {};
}

/** @brief 確認した選択のみをPolkit認証後にコミットし、全ステップの結果を返す */
QVariantMap PackageManagerAdaptor::Commit(qulonglong expectedRevision)
{
    if (!preparePrivileged()) return {};
    if (expectedRevision != m_revision || m_resolvedRevision != m_revision) {
        sendErrorReply(QStringLiteral("org.presire.qzypper.Error.SelectionChanged"),
            QStringLiteral("Package selection changed; resolve and confirm again"));
        return {};
    }
    startPrivileged(QStringLiteral("org.presire.qzypper.install-packages"), [this] {
        // 間引きの状態はワーカースレッド内に閉じ込め、送信時だけmainスレッドへ渡す
        // 進捗・状態の両コールバックと commit() 後の処理は、いずれも commit() を呼んだ
        // このワーカースレッド上で同期的に実行される (ZyppCommit.cpp は libzypp の
        // commit() を同一スレッドで呼び、受信側を別スレッドへ渡さない)。
        // そのため throttle は排他制御なしで共有できる。
        auto throttle = std::make_shared<ProgressThrottle>();

        // 進捗の送信をmainスレッドへ引き渡す。間引き済みの保留サンプルの回収にも使う
        const auto sendProgress = [this](const ProgressSample &sample) {
            QMetaObject::invokeMethod(this, [this, sample] {
                publishState();
                emitToOwner(QStringLiteral("CommitProgressChanged"),
                            {sample.package, sample.percentage, sample.stage,
                             sample.totalSteps, sample.completedSteps,
                             sample.overallPercentage});
            }, Qt::QueuedConnection);
        };

        // 間引きで送られなかった最新の進捗があれば送る。後続の進捗通知が来ないまま
        // 長いRPMスクリプトに入っても、利用者に古い進捗が表示され続けないようにする
        const auto flushPendingProgress = [throttle, sendProgress] {
            if (const auto pending = throttle->takePending(std::chrono::steady_clock::now()))
                sendProgress(*pending);
        };

        const auto commit = ZyppManager::instance().commit([throttle, sendProgress, this](const CommitProgressInfo &info) {
            m_executor.control().stage(info.stage == "downloading" ? OperationStage::Downloading : OperationStage::Installing);
            const ProgressSample sample{QString::fromStdString(info.packageName),
                                        QString::fromStdString(info.stage),
                                        info.percentage, info.totalSteps,
                                        info.completedSteps, info.overallPercentage};
            if (!throttle->shouldEmit(sample, std::chrono::steady_clock::now()))
                return;
            sendProgress(sample);
        }, [this, flushPendingProgress](const std::string &package, const std::string &event) {
            // 状態遷移の前に保留中の進捗を送り、受信側で「進捗→状態」の順序を保つ
            flushPendingProgress();
            if (event == "install_start" || event == "remove_start")
                m_executor.control().stage(OperationStage::Installing);
            QMetaObject::invokeMethod(this, [this, package, event] {
                publishState();
                emitToOwner(QStringLiteral("PackageStateChanged"), {QString::fromStdString(package), QString::fromStdString(event)});
            }, Qt::QueuedConnection);
        }, [this](const PromptRequest &request) {
            // libzyppワーカースレッド上で所有者の回答を同期的に待つ
            return m_executor.ask(request);
        }, [this](const std::vector<ScriptOutputEntry> &entries) {
            // 行の変換はワーカースレッドで済ませ、送信だけをメインスレッドへ引き渡す
            QVariantList batch;
            batch.reserve(static_cast<int>(entries.size()));
            for (const ScriptOutputEntry &entry : entries) {
                batch.append(QVariantMap{
                    {"package", entry.package}, {"scriptType", entry.scriptType},
                    {"line", entry.line}, {"isError", entry.isError}});
            }
            QMetaObject::invokeMethod(this, [this, batch] {
                emitToOwner(QStringLiteral("ScriptOutputBatch"), {batch});
            }, Qt::QueuedConnection);
        });

        // 最後の進捗が間引かれていても、完了結果より先に利用者へ届ける
        flushPendingProgress();

        const auto &classification = commit.classification;
        QVariantMap value{{"success", commit.success},
            {"outcome", QString::fromLatin1(commitOutcomeName(classification.outcome))},
            {"doneCount", classification.doneCount}, {"errorCount", classification.errorCount},
            {"skippedCount", classification.skippedCount},
            {"installed", commit.installed}, {"updated", commit.updated}, {"removed", commit.removed},
            {"installedPackages", names(commit.installedPackages)}, {"updatedPackages", names(commit.updatedPackages)},
            {"removedPackages", names(commit.removedPackages)}, {"failedPackages", names(commit.failedPackages)},
            {"ignoredPackages", names(commit.ignoredPackages)},
            {"skippedPackages", names(classification.skippedPackages)},
            {"totalInstalledSize", QVariant::fromValue(commit.totalInstalledSize)},
            {"totalDownloadSize", QVariant::fromValue(commit.totalDownloadSize)}, {"elapsedSeconds", commit.elapsedSeconds},
            {"scriptOutput", commit.scriptOutput}, {"rpmMessages", commit.rpmMessages},
            {"scriptOutputTruncated", commit.scriptOutputTruncated},
            {"rebootNeeded", commit.rebootNeeded}, {"restartSuggested", commit.restartSuggested},
            {"servicesToRestart", commit.servicesToRestart}, {"snapshots", snapshotsToVariantList(commit.snapshots)}};

        const auto error = QString::fromStdString(commit.errorMessage);

        if (!commit.success) value["errorMessage"] = error;

        AsyncOutcome result;
        result.value = value;
        result.transactionFinished = true;
        result.transactionSuccess = commit.success;
        result.transactionSummary = commit.success ? QStringLiteral("Installed: %1, Updated: %2, Removed: %3")
            .arg(commit.installed).arg(commit.updated).arg(commit.removed) : error;
        return result;
    });
    return {};
}

} // namespace qZypper
