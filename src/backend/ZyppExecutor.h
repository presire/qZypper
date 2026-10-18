#ifndef QZYPPER_BACKEND_ZYPPEXECUTOR_H
#define QZYPPER_BACKEND_ZYPPEXECUTOR_H

#include "KeyTrustPrompt.h"
#include "OperationState.h"
#include "PromptRegistry.h"
#include <QObject>
#include <QThread>
#include <optional>
#include <functional>

namespace qZypper {

/** @brief ワーカーが主スレッドへ返す、libzyppオブジェクトを含まない結果。 */
struct WorkerResult {
    QVariant value;
    QString errorName;
    QString errorText;
    bool transactionFinished = false;
    bool transactionSuccess = false;
    QString transactionSummary;
    bool initialized = false;
    bool poolReloaded = false;
};

/** @brief libzyppの生成・コールバック登録・利用・破棄を単一スレッドに固定する。 */
class ZyppExecutor : public QObject {
    Q_OBJECT
public:
    using Job = std::function<WorkerResult()>;
    using Completion = std::function<void(const WorkerResult &)>;
    explicit ZyppExecutor(QObject *parent = nullptr);
    ~ZyppExecutor() override;
    void submit(const Job &job, const Completion &completion, bool commit);
    OperationState &control() { return *m_control; }
    PromptRegistry &prompts() { return m_prompts; }
    QVariantMap ask(const PromptRequest &request);

    /**
     * @brief 署名鍵の信頼可否を利用者へ問い合わせる (ワーカースレッド専用)。
     * @param info 対象鍵の情報
     * @return 可否 (未回答・取消時は std::nullopt)
     */
    std::optional<KeyTrustDecision> askKeyTrust(const KeyTrustAnswerInfo &info);

signals:
    void untrustedKey(const QVariantMap &key);
    void promptRequested(quint32 id, const QString &type, const QVariantMap &data);
    void promptClosed(quint32 id);                       // 問合せの終了 (回答/期限切れ/中断)

private:
    QThread m_thread;
    QObject *m_worker = nullptr;
    OperationState *m_control = nullptr;
    PromptRegistry m_prompts;
};

} // namespace qZypper
#endif // QZYPPER_BACKEND_ZYPPEXECUTOR_H
