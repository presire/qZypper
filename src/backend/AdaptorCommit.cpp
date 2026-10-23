#include "PackageManagerAdaptor.h"
#include "ZyppManager.h"

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
        const auto commit = ZyppManager::instance().commit([this](const CommitProgressInfo &info) {
            m_executor.control().stage(info.stage == "downloading" ? OperationStage::Downloading : OperationStage::Installing);
            QMetaObject::invokeMethod(this, [this, info] {
                publishState();
                const auto pkg = QString::fromStdString(info.packageName);
                const auto stage = QString::fromStdString(info.stage);
                emitToOwner(QStringLiteral("CommitProgressChanged"), {pkg, info.percentage, stage,
                    info.totalSteps, info.completedSteps, info.overallPercentage});
                emitToOwner(QStringLiteral("ProgressChanged"), {pkg, info.percentage, stage});
            }, Qt::QueuedConnection);
        }, [this](const std::string &package, const std::string &event) {
            if (event == "install_start" || event == "remove_start")
                m_executor.control().stage(OperationStage::Installing);
            QMetaObject::invokeMethod(this, [this, package, event] {
                publishState();
                emitToOwner(QStringLiteral("PackageStateChanged"), {QString::fromStdString(package), QString::fromStdString(event)});
            }, Qt::QueuedConnection);
        }, [this](const PromptRequest &request) {
            // libzyppワーカースレッド上で所有者の回答を同期的に待つ
            return m_executor.ask(request);
        }, [this](const QString &package, const QString &scriptType, const QString &line, bool isError) {
            // スクリプト/rpm出力はメインスレッドへ引き渡して送信する
            QMetaObject::invokeMethod(this, [this, package, scriptType, line, isError] {
                emitToOwner(QStringLiteral("ScriptOutput"), {QVariantMap{
                    {"package", package}, {"scriptType", scriptType},
                    {"line", line}, {"isError", isError}}});
            }, Qt::QueuedConnection);
        });

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
