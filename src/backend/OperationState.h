#ifndef QZYPPER_BACKEND_OPERATIONSTATE_H
#define QZYPPER_BACKEND_OPERATIONSTATE_H

#include <QVariantMap>
#include <atomic>
#include <chrono>
#include <mutex>

namespace qZypper {

/** @brief 主スレッドとワーカーが共有する操作段階。 */
enum class OperationPhase { Idle, Authorizing, Running, Finishing };
/** @brief RPM開始後は操作完了まで取消し不能とする。 */
enum class OperationStage { None, Initializing, Reading, Resolving, Refreshing,
                            Downloading, Installing, RepoChange, Releasing };
/** @brief ウォッチドッグが取るべき動作。 */
enum class WatchdogAction { None, Cancel, Warn };

/** @brief libzyppに触れずに状態と取消要求を同期する。 */
class OperationState {
public:
    bool authorize();
    bool run(OperationStage stage);
    void stage(OperationStage stage);
    void heartbeat();
    bool requestCancel();
    void finishing();
    bool complete();
    QVariantMap snapshot() const;
    WatchdogAction watchdog(std::chrono::milliseconds timeout);
    std::atomic<bool> &cancelFlag() { return m_cancel; }

private:
    bool cancellable() const;
    mutable std::mutex m_mutex;
    OperationPhase m_phase = OperationPhase::Idle;
    OperationStage m_stage = OperationStage::None;
    std::atomic<bool> m_cancel{false};
    std::chrono::steady_clock::time_point m_heartbeat = std::chrono::steady_clock::now();
    QString m_warning;
};

} // namespace qZypper
#endif // QZYPPER_BACKEND_OPERATIONSTATE_H
