#ifndef QZYPPER_BACKEND_OPERATIONSTATE_H
#define QZYPPER_BACKEND_OPERATIONSTATE_H

#include <QVariantMap>
#include <atomic>
#include <chrono>
#include <mutex>

namespace qZypper {

/** @brief 主スレッドとワーカーが共有する操作段階 */
enum class OperationPhase {
    Idle,          // 操作待機中
    Authorizing,   // Polkit認可待ち
    Running,       // libzypp操作を実行中
    Finishing      // 終了処理中
};

/** @brief RPM開始後は操作完了まで取消し不能とする */
enum class OperationStage {
    None,          // 未開始
    Initializing,  // libzypp初期化
    Reading,       // 読取専用処理
    Resolving,     // 依存関係解決
    Refreshing,    // リポジトリ更新
    Downloading,   // RPM取得
    Installing,    // RPM適用中
    RepoChange,    // リポジトリ設定変更
    Releasing      // セッション解放
};

/** @brief ウォッチドッグが取るべき動作 */
enum class WatchdogAction {
    None,          // 監視処置なし
    Cancel,        // 協調的取消要求
    Warn           // 警告状態を公開
};

/**
 * @class OperationState
 * @brief libzyppに触れずに状態と取消要求を同期する
 */
class OperationState {
public:
    /** @brief 認可待ち段階への遷移を試みる */
    bool authorize();

    /** @brief 指定段階で実行中状態への遷移を試みる */
    bool run(OperationStage stage);

    /** @brief 実行中操作の詳細段階を更新する */
    void stage(OperationStage stage);

    /** @brief ワーカーの進捗監視時刻を更新する */
    void heartbeat();

    /** @brief 現在の段階で取消可能なら取消要求を記録する */
    bool requestCancel();

    /** @brief 完了直前の終了処理段階へ遷移する */
    void finishing();

    /** @brief 操作を待機状態へ完了遷移させる */
    bool complete();

    /** @brief GUI公開用の操作状態をスナップショットとして返す */
    QVariantMap snapshot() const;

    /** @brief 無応答時間に応じた監視処置を返す */
    WatchdogAction watchdog(std::chrono::milliseconds timeout);

    /** @brief ワーカーが直接確認する取消フラグを返す */
    std::atomic<bool> &cancelFlag() { return m_cancel; }

private:
    /** @brief 現在の実行段階で取消要求を受け付けられるか返す */
    bool cancellable() const;

    mutable std::mutex m_mutex;                                                            // 状態項目を保護する排他制御
    OperationPhase m_phase = OperationPhase::Idle;                                         // 操作の大域的段階
    OperationStage m_stage = OperationStage::None;                                         // 実行中操作の詳細段階
    std::atomic<bool> m_cancel{false};                                                     // ワーカーが参照する取消要求
    std::chrono::steady_clock::time_point m_heartbeat = std::chrono::steady_clock::now();  // 最終応答時刻
    QString m_warning;                                                                     // GUI公開用の監視警告
};

} // namespace qZypper

#endif // QZYPPER_BACKEND_OPERATIONSTATE_H
