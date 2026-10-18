#include "ZyppExecutor.h"
#include "ZyppManager.h"
#include <QMetaObject>

namespace qZypper {
namespace {
/** @brief 全操作のlibzypp進捗を心拍と取消し判定へ接続する。 */
class ProgressReceiver : public zypp::callback::ReceiveReport<zypp::ProgressReport> {
public:
    explicit ProgressReceiver(OperationState &control) : m_control(control) { connect(); }
    bool progress(const zypp::ProgressData &) override {
        m_control.heartbeat();
        return !m_control.cancelFlag().load()
            || !m_control.snapshot().value("cancellable").toBool();
    }
private:
    OperationState &m_control;
};

/** @brief メディア転送の進捗もウォッチドッグに反映する。 */
class TransferReceiver : public zypp::callback::ReceiveReport<zypp::media::DownloadProgressReport> {
public:
    explicit TransferReceiver(OperationState &control) : m_control(control) { connect(); }
    bool progress(int, const zypp::Url &, double, double) override {
        m_control.heartbeat();
        return !m_control.cancelFlag().load()
            || !m_control.snapshot().value("cancellable").toBool();
    }
private:
    OperationState &m_control;
};
}

/** @brief ワーカーを一度だけ作り、初回のマネージャ生成もその上で行う。 */
ZyppExecutor::ZyppExecutor(QObject *parent) : QObject(parent), m_worker(new QObject)
{
    m_thread.setObjectName(QStringLiteral("zypp-executor"));
    m_worker->moveToThread(&m_thread);
    connect(&m_thread, &QThread::finished, m_worker, &QObject::deleteLater);
    m_thread.start();
    QMetaObject::invokeMethod(m_worker, [this] {
        auto &manager = ZyppManager::instance();
        m_control = &manager.operationState();
        manager.setUntrustedKeyCallback([this](const UntrustedKeyInfo &key) {
            emit untrustedKey({{"fingerprint", QString::fromStdString(key.fingerprint)},
                {"id", QString::fromStdString(key.id)}, {"name", QString::fromStdString(key.name)},
                {"created", QVariant::fromValue(qlonglong(key.created))},
                {"expires", QVariant::fromValue(qlonglong(key.expires))},
                {"repoAlias", QString::fromStdString(key.repoAlias)},
                {"repoName", QString::fromStdString(key.repoName)}});
        });
        manager.setKeyTrustPromptCallback([this](const KeyTrustAnswerInfo &info) {
            return askKeyTrust(info);
        });
    }, Qt::BlockingQueuedConnection);
}

/** @brief 実行中処理を待ち、libzyppを同じワーカー上で破棄してから終了する。 */
ZyppExecutor::~ZyppExecutor()
{
    m_prompts.interrupt();
    m_control->requestCancel();
    QMetaObject::invokeMethod(m_worker, [] {
        auto &manager = ZyppManager::instance();
        manager.setUntrustedKeyCallback({});
        manager.setKeyTrustPromptCallback({});
        manager.cleanupLocalRpms();
        manager.shutdown();
    }, Qt::BlockingQueuedConnection);
    m_thread.quit();
    m_thread.wait();
}

/** @brief libzypp処理をキュー投入し、主スレッドで結果を渡す。 */
void ZyppExecutor::submit(const Job &job, const Completion &completion, bool commit)
{
    QMetaObject::invokeMethod(m_worker, [this, job, completion, commit] {
        WorkerResult result;
        auto &manager = ZyppManager::instance();
        manager.beginOperation();
        ProgressReceiver progress(*m_control);
        TransferReceiver transfer(*m_control);
        bool reloaded = false;
        try {
            if (m_control->cancelFlag().load()) {
                result.errorName = QStringLiteral("org.presire.qzypper.Error.Cancelled");
                result.errorText = QStringLiteral("Operation cancelled");
            } else {
                const bool releasing = m_control->snapshot().value("stage") == QStringLiteral("releasing");
                if (!releasing) reloaded = manager.reloadIfChanged();
                if (commit && reloaded) {
                    result.errorName = QStringLiteral("org.presire.qzypper.Error.SelectionChanged");
                    result.errorText = QStringLiteral("RPM database changed; resolve and confirm again");
                } else {
                    result = job();
                }
            }
        } catch (const zypp::ZYppFactoryException &error) {
            result.errorName = QStringLiteral("org.presire.qzypper.Error.ZyppLocked");
            result.errorText = QStringLiteral("Package management is locked by %1 (PID %2)")
                .arg(QString::fromStdString(error.lockerName())).arg(error.lockerPid());
        } catch (const std::exception &) {
            result.errorName = QStringLiteral("org.freedesktop.DBus.Error.Failed");
            result.errorText = QStringLiteral("Package management operation failed");
        } catch (...) {
            result.errorName = QStringLiteral("org.freedesktop.DBus.Error.Failed");
            result.errorText = QStringLiteral("Unexpected package management failure");
        }
        result.initialized = manager.isInitialized();
        result.poolReloaded = reloaded;
        QMetaObject::invokeMethod(this, [completion, result] { completion(result); }, Qt::QueuedConnection);
    }, Qt::QueuedConnection);
}

/** @brief ワーカーから安全な既定値付きの対話要求を発行する。 */
QVariantMap ZyppExecutor::ask(const PromptRequest &request)
{
    Q_ASSERT(QThread::currentThread() == &m_thread);
    quint32 askedId = 0;
    const QVariantMap result = m_prompts.ask(request,
        [this, &askedId](quint32 id, const QString &type, const QVariantMap &data) {
            askedId = id;
            emit promptRequested(id, type, data);
        });
    // 回答・期限切れ・中断のいずれでも、GUI 側に残ったダイアログを閉じさせる
    if (askedId != 0)
        emit promptClosed(askedId);
    return result;
}

/** @brief 署名鍵の信頼可否を専用プロンプトで利用者へ問い合わせる。 */
std::optional<KeyTrustDecision> ZyppExecutor::askKeyTrust(const KeyTrustAnswerInfo &info)
{
    Q_ASSERT(QThread::currentThread() == &m_thread);
    const QVariantMap answer = ask(KeyTrustPrompt::buildKeyRequest(info));
    return KeyTrustPrompt::decisionFromAnswer(answer);
}

} // namespace qZypper
