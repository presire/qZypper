#ifndef QZYPPER_BACKEND_ZYPPEXECUTOR_H
#define QZYPPER_BACKEND_ZYPPEXECUTOR_H

#include <QObject>
#include <QThread>
#include <optional>
#include <functional>
#include "KeyTrustPrompt.h"
#include "OperationState.h"
#include "PromptRegistry.h"

namespace qZypper {

/** @brief ワーカーが主スレッドへ返す、libzyppオブジェクトを含まない結果 */
struct WorkerResult {
    QVariant value;                    // D-Busへ返す値
    QString errorName;                 // D-Busエラー名
    QString errorText;                 // 利用者へ返すエラー内容
    bool transactionFinished = false;  // トランザクション終了通知の要否
    bool transactionSuccess = false;   // トランザクション成功結果
    QString transactionSummary;        // トランザクション要約
    bool initialized = false;          // libzypp初期化成功結果
    bool poolReloaded = false;         // パッケージプール再読込の有無
};

/**
 * @class ZyppExecutor
 * @brief libzyppの生成・コールバック登録・利用・破棄を単一スレッドに固定する
 */
class ZyppExecutor : public QObject {
    Q_OBJECT
public:
    using Job = std::function<WorkerResult()>;                         // ワーカースレッドで実行する処理
    using Completion = std::function<void(const WorkerResult &)>;      // 主スレッドで処理する完了通知

    /** @brief 実行器を親オブジェクトへ関連付けて生成する */
    explicit ZyppExecutor(QObject *parent = nullptr);

    /** @brief ワーカーを停止して、所有するQObjectを安全に破棄する */
    ~ZyppExecutor() override;

    /** @brief 単一ワーカーへジョブを投入して、完了通知を登録する */
    void submit(const Job &job, const Completion &completion, bool commit);

    /** @brief 操作の取消状態を共有する制御オブジェクトを返す */
    OperationState &control() { return *m_control; }

    /** @brief 保留中の利用者問合せを管理するレジストリを返す */
    PromptRegistry &prompts() { return m_prompts; }

    /** @brief 一般的な利用者問合せを送信して、回答を待機する */
    QVariantMap ask(const PromptRequest &request);

    /**
     * @brief 署名鍵の信頼可否を利用者へ問い合わせる (ワーカースレッド専用)
     * @param info 対象鍵の情報
     * @return 可否 (未回答・取消時はstd::nullopt)
     */
    std::optional<KeyTrustDecision> askKeyTrust(const KeyTrustAnswerInfo &info);

signals:
    void untrustedKey(const QVariantMap &key);                                       // 未承認鍵を主スレッドへ通知
    void promptRequested(quint32 id, const QString &type, const QVariantMap &data);  // 問合せ表示を要求
    void promptClosed(quint32 id);                                                   // 問合せの終了 (回答/期限切れ/中断)

private:
    QThread m_thread;                       // libzypp専用ワーカースレッド
    QObject *m_worker = nullptr;            // ワーカースレッドへ移動する実行オブジェクト
    OperationState *m_control = nullptr;    // 操作状態と取消要求の共有先
    PromptRegistry m_prompts;               // ワーカーとUI間の問合せ管理
};

} // namespace qZypper

#endif // QZYPPER_BACKEND_ZYPPEXECUTOR_H
