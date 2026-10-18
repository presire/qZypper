#include "PackageManagerAdaptor.h"
#include "ZyppManager.h"

namespace qZypper {
namespace {
QStringList names(const std::vector<std::string> &packages)
{
    QStringList result;
    for (const auto &package : packages) result.append(QString::fromStdString(package));
    return result;
}
}

/** @brief 確認した選択のみを認可後にコミットし、全ステップの結果を返す。 */
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
            // libzypp ワーカースレッド上で所有者の回答を同期的に待つ
            return m_executor.ask(request);
        }, [this](const QString &package, const QString &scriptType, const QString &line, bool isError) {
            // スクリプト/rpm 出力は集約後に主スレッドへ引き渡して送信する
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
