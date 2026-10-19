#include <QDBusError>
#include "PackageManagerAdaptor.h"
#include "ZyppManager.h"

namespace qZypper {
namespace {
/** @brief ソルバーを実行する分類検索かを判定する (ZyppManager::getPackagesByClassificationと同じ分類) */
bool classificationNeedsSolver(const QDBusMessage &msg)
{
    const QList<QVariant> args = msg.arguments();
    if (args.isEmpty()) return false;
    const QString cls = args.first().toString().trimmed().toLower();
    return cls == "recommended" || cls == "suggested" || cls == "orphaned" || cls == "unneeded";
}

/** @brief 公開メソッドと引数から実行開始段階を選ぶ */
OperationStage initialStage(const QDBusMessage &msg)
{
    const QString member = msg.member();
    if (member == "Initialize") return OperationStage::Initializing;
    if (member == "ReleaseSession") return OperationStage::Releasing;
    if (member == "Commit") return OperationStage::Downloading;
    if (member == "InstallLocalRpms") return OperationStage::Resolving;
    if (member.startsWith("Refresh")) return OperationStage::Refreshing;
    if (member == "ResolveDependencies" || member == "UpdateAllPackages"
        || member == "ApplySolution" || member.startsWith("SetPackage") || member == "SetPatternStatus"
        || member == "SetPatchStatus" || member == "DistUpgrade" || member == "SelectAllNeededPatches"
        || member == "SelectSubpackages" || member == "SelectAllRecommended"
        || member == "SetLocaleRequested"
        || member == "VerifySystem")
        return OperationStage::Resolving;
    if (member == "GetPackagesByClassification" && classificationNeedsSolver(msg))
        return OperationStage::Resolving;
    if (member.startsWith("Add") || member.startsWith("Remove") || member.startsWith("Modify")
        || member == "SetRepoEnabled" || member == "TrustKey") return OperationStage::RepoChange;
    return OperationStage::Reading;
}

/** @brief 選択やプールを変更する操作かを判定する */
bool changesSelection(const QString &member)
{
    return !(member.startsWith("Get") || member == "SearchPackages"
             || member == "SearchPackagesEx" || member == "SaveState"
             || member == "ParseRepoFile");
}
}

/** @brief 所有者の読取も遅延返信で同じ常駐ワーカーへ送る、途中の取消しはエラーで返す */
QVariant PackageManagerAdaptor::runRead(const std::function<QVariant()> &job)
{
    if (!checkRead()) return {};
    OperationState *control = &m_executor.control();
    startWorker(message(), [job, control] {
        AsyncOutcome result;
        result.value = job();
        if (control->cancelFlag().load()) {
            result.value = QVariant();
            result.errorName = QStringLiteral("org.presire.qzypper.Error.Cancelled");
            result.errorText = QStringLiteral("Operation cancelled");
        }
        return result;
    });
    return {};
}

/** @brief 所有者による選択操作をワーカーへ送る。旧同期APIの返信型は維持する */
QVariant PackageManagerAdaptor::runOwnerSync(const std::function<QVariant()> &job, bool bump, bool solver)
{
    Q_UNUSED(bump)
    Q_UNUSED(solver)
    if (!checkNotBusy() || !checkOwner() || !checkArguments()) return {};
    startWorker(message(), [job] { AsyncOutcome result; result.value = job(); return result; });
    return {};
}

/** @brief 主スレッドに複製した初期化状態で認可前の検査を行う */
bool PackageManagerAdaptor::preparePrivileged()
{
    if (!checkNotBusy() || !checkOwner() || !checkArguments()) return false;
    if (m_initialized) return true;
    sendErrorReply(QStringLiteral("org.presire.qzypper.Error.NotInitialized"), QStringLiteral("Backend not initialized"));
    return false;
}

/** @brief 実行中フラグを維持したまま処理をキュー投入する */
void PackageManagerAdaptor::startWorker(const QDBusMessage &msg, const std::function<AsyncOutcome()> &job)
{
    if (calledFromDBus()) setDelayedReply(true);
    // アイドルタイマーは止めない
    // 期限が来ても実行中なら再設定され、終了時は所有者の要求だけが延長する
    m_busy = true;
    m_executor.control().run(initialStage(msg));
    m_executor.prompts().open();
    publishState();
    m_executor.submit(job, [this, msg](const AsyncOutcome &result) {
        m_initialized = result.initialized;
        finishPrivileged(msg, result);
    }, msg.member() == "Commit");
}

/** @brief Polkit待機中も主イベントループを止めない */
void PackageManagerAdaptor::startPrivileged(const QString &actionId, const std::function<AsyncOutcome()> &job)
{
    const auto msg = message();
    setDelayedReply(true);
    m_busy = true;
    m_executor.control().authorize();
    publishState();
    m_authId = m_authorization.request(msg.service(), actionId,
                                       [this, msg, job](const QString &authId, bool authorized) {
        clearAuthorizationId(m_authId, authId);
        if (!authorized || m_executor.control().cancelFlag().load() || m_owner != msg.service()) {
            AsyncOutcome result;
            result.errorName = m_executor.control().cancelFlag().load()
                ? QStringLiteral("org.presire.qzypper.Error.Cancelled")
                : QStringLiteral("org.freedesktop.DBus.Error.AccessDenied");
            result.errorText = QStringLiteral("Authorization denied or cancelled");
            finishPrivileged(msg, result);
            return;
        }
        startWorker(msg, job);
    });
}

/** @brief 真偽値の特権処理を既存のD-Bus返信型で実行する */
bool PackageManagerAdaptor::runPrivilegedBool(const QString &actionId, const std::function<bool()> &job)
{
    if (!preparePrivileged()) return false;
    startPrivileged(actionId, [job] {
        AsyncOutcome result;
        auto &manager = ZyppManager::instance();
        result.value = job();
        if (!result.value.toBool()) {
            result.errorName = manager.lastOperationCancelled()
                ? QStringLiteral("org.presire.qzypper.Error.Cancelled") : QStringLiteral("org.freedesktop.DBus.Error.Failed");
            result.errorText = QString::fromStdString(manager.lastError());
        }
        return result;
    });
    return false;
}

/** @brief ワーカー終了通知の後にだけbusyを解除する。
 *         void返信は引数なしにする
 */
void PackageManagerAdaptor::finishPrivileged(const QDBusMessage &msg, const AsyncOutcome &outcome)
{
    // 所有者の解放前に判定し、所有者以外の要求ではアイドル期限を延長しない
    const bool fromOwner = !m_owner.isEmpty() && msg.service() == m_owner;
    m_executor.control().finishing();
    m_executor.prompts().interrupt();
    publishState();

    if (changesSelection(msg.member()) || outcome.poolReloaded) bumpRevision();

    if ((msg.member() == "ResolveDependencies" || msg.member() == "UpdateAllPackages"
         || msg.member() == "DistUpgrade" || msg.member() == "SelectAllNeededPatches"
         || msg.member() == "SelectAllRecommended")
        && outcome.errorName.isEmpty() && outcome.value.toMap().value("success").toBool())
        m_resolvedRevision = m_revision;

    if (outcome.poolReloaded) emitToOwner(QStringLiteral("PoolReloaded"), {});

    if (msg.member() == "Commit")
        emitToOwner(QStringLiteral("TransactionFinished"), {outcome.transactionFinished && outcome.transactionSuccess,
            outcome.transactionFinished ? outcome.transactionSummary : outcome.errorText});
    const auto reply = !outcome.errorName.isEmpty() ? msg.createErrorReply(outcome.errorName, outcome.errorText)
        : outcome.value.isValid() ? msg.createReply(QVariantList{outcome.value}) : msg.createReply();
    QDBusConnection::systemBus().send(reply);
    m_executor.control().complete();
    publishState();

    if (msg.member() == "ReleaseSession" || (msg.member() == "Initialize"
        && (!outcome.errorName.isEmpty() || !outcome.value.toBool()))) {
        m_ownerWatcher.removeWatchedService(m_owner);
        m_owner.clear();
    }

    if (m_quitWhenIdle) { releaseAbandonedSession(); return; }

    m_busy = false;

    if (fromOwner || !m_idleTimer.isActive()) m_idleTimer.start();
}

} // namespace qZypper
