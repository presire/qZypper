#include "OperationState.h"

namespace qZypper {
namespace {
/** @brief 外部公開用の段階名を取得する。 */
QString stageName(OperationStage stage)
{
    switch (stage) {
    case OperationStage::None: return {};
    case OperationStage::Initializing: return QStringLiteral("initializing");
    case OperationStage::Reading: return QStringLiteral("reading");
    case OperationStage::Resolving: return QStringLiteral("resolving");
    case OperationStage::Refreshing: return QStringLiteral("refreshing");
    case OperationStage::Downloading: return QStringLiteral("downloading");
    case OperationStage::Installing: return QStringLiteral("installing");
    case OperationStage::RepoChange: return QStringLiteral("repo-change");
    case OperationStage::Releasing: return QStringLiteral("releasing");
    }
    return {};
}
}

/** @brief アイドル状態から認可待ちへ遷移する。 */
bool OperationState::authorize()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_phase != OperationPhase::Idle)
        return false;
    m_phase = OperationPhase::Authorizing;
    m_stage = OperationStage::None;
    m_cancel = false;
    m_warning.clear();
    m_heartbeat = std::chrono::steady_clock::now();
    return true;
}

/** @brief 待機または認可待ちからワーカー実行へ遷移する。 */
bool OperationState::run(OperationStage stage)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_phase != OperationPhase::Idle && m_phase != OperationPhase::Authorizing)
        return false;
    if (m_phase == OperationPhase::Idle) {
        m_cancel = false;
        m_warning.clear();
    }
    m_phase = OperationPhase::Running;
    m_stage = stage;
    m_heartbeat = std::chrono::steady_clock::now();
    return true;
}

/** @brief ワーカーが実段階を記録する。RPM段階からは戻らない。 */
void OperationState::stage(OperationStage stage)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_phase == OperationPhase::Running && m_stage != OperationStage::Installing)
        m_stage = stage;
    m_heartbeat = std::chrono::steady_clock::now();
}

/** @brief 進捗の生存時刻を更新する。 */
void OperationState::heartbeat()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_heartbeat = std::chrono::steady_clock::now();
}

/** @brief ロック保持中に取消し可能かを判定する。 */
bool OperationState::cancellable() const
{
    if (m_phase == OperationPhase::Authorizing)
        return true;
    return m_phase == OperationPhase::Running
        && m_stage != OperationStage::Installing && m_stage != OperationStage::Releasing;
}

/** @brief RPM中の取消しを拒否し、それ以外は協調的な取消しを要求する。 */
bool OperationState::requestCancel()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!cancellable())
        return false;
    m_cancel = true;
    return true;
}

/** @brief ワーカーの終了後にのみ完了処理へ移る。 */
void OperationState::finishing()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_phase != OperationPhase::Idle)
        m_phase = OperationPhase::Finishing;
}

/** @brief 完了通知の終了後にアイドルへ戻す。 */
bool OperationState::complete()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_phase != OperationPhase::Finishing)
        return false;
    m_phase = OperationPhase::Idle;
    m_stage = OperationStage::None;
    return true;
}

/** @brief D-Bus用に状態の一貫したスナップショットを取得する。 */
QVariantMap OperationState::snapshot() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    QString phase;
    switch (m_phase) {
    case OperationPhase::Idle: phase = QStringLiteral("idle"); break;
    case OperationPhase::Authorizing: phase = QStringLiteral("authorizing"); break;
    case OperationPhase::Running: phase = QStringLiteral("running"); break;
    case OperationPhase::Finishing: phase = QStringLiteral("finishing"); break;
    }
    return {{"state", phase}, {"stage", stageName(m_stage)},
            {"busy", m_phase != OperationPhase::Idle}, {"cancellable", cancellable()},
            {"cancelRequested", m_cancel.load()}, {"warning", m_warning}};
}

/** @brief 無進捗を検出する。RPM中は警告だけで取消しはしない。 */
WatchdogAction OperationState::watchdog(std::chrono::milliseconds timeout)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_phase == OperationPhase::Idle || m_phase == OperationPhase::Finishing
        || std::chrono::steady_clock::now() - m_heartbeat < timeout || !m_warning.isEmpty())
        return WatchdogAction::None;
    if (cancellable()) {
        m_cancel = true;
        m_warning = QStringLiteral("No progress; cancellation requested");
        return WatchdogAction::Cancel;
    }
    m_warning = QStringLiteral("No progress during RPM transaction; waiting without cancellation");
    return WatchdogAction::Warn;
}

} // namespace qZypper
