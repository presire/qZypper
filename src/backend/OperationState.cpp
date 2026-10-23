#include "OperationState.h"

namespace qZypper {
    namespace {
        /** @brief 外部公開用の段階名を取得する */
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

    /** @brief アイドル状態から認証待ちへ遷移する */
    bool OperationState::authorize()
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_phase != OperationPhase::Idle)
            return false;
        m_phase = OperationPhase::Authorizing;
        m_stage = OperationStage::None;
        m_cancel = false;
        m_warning.clear();
        m_promptWaiting = false;
        m_heartbeat = std::chrono::steady_clock::now();
        return true;
    }

    /** @brief 待機または認証待ちからワーカー実行へ遷移する */
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
        m_promptWaiting = false;
        m_heartbeat = std::chrono::steady_clock::now();
        return true;
    }

    /**
     * @brief ワーカーが実行段階を記録する
     *
     * RPM適用段階に入った後は前の段階へ戻さない
     */
    void OperationState::stage(OperationStage stage)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_phase == OperationPhase::Running && m_stage != OperationStage::Installing)
            m_stage = stage;
        m_heartbeat = std::chrono::steady_clock::now();
    }

    /** @brief 進捗監視の基準時刻を更新する */
    void OperationState::heartbeat()
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_heartbeat = std::chrono::steady_clock::now();
    }

    /**
     * @brief プロンプト応答待ちの間はウォッチドッグを保留する
     * @param bound 応答待ちを許容する上限時間
     * @note m_mutexで保護しm_promptWaitingとm_promptDeadlineを更新する
     */
    void OperationState::beginPromptWait(std::chrono::milliseconds bound)
    {
        // 監視スレッドがm_promptWaitingとm_promptDeadlineを参照するため排他制御する
        std::lock_guard<std::mutex> lock(m_mutex);

        // 負の待機時間は即時期限切れとして扱う
        if (bound.count() <= 0) {
            bound = std::chrono::milliseconds(0);
        }

        m_promptWaiting = true;
        m_promptDeadline = std::chrono::steady_clock::now() + bound;
    }

    /**
     * @brief プロンプト応答待ちを終了し、進捗監視時刻を更新する
     * @note m_mutexで保護しm_promptWaitingとm_heartbeatを更新する
     */
    void OperationState::endPromptWait()
    {
        // 監視スレッドが参照する待機状態とハートビート時刻を同時に更新する
        std::lock_guard<std::mutex> lock(m_mutex);

        m_promptWaiting = false;
        m_heartbeat = std::chrono::steady_clock::now();
    }

    /** @brief ロック保持中に取消しを受け付けられるかを判定する */
    bool OperationState::cancellable() const
    {
        if (m_phase == OperationPhase::Authorizing)
            return true;
        return m_phase == OperationPhase::Running
            && m_stage != OperationStage::Installing && m_stage != OperationStage::Releasing;
    }

    /** @brief RPM中の取消しを拒否し、それ以外は協調的な取消しを要求する */
    bool OperationState::requestCancel()
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (!cancellable())
            return false;
        m_cancel = true;
        return true;
    }

    /** @brief ワーカーの終了後にのみ完了処理へ移る */
    void OperationState::finishing()
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_phase != OperationPhase::Idle)
            m_phase = OperationPhase::Finishing;
    }

    /** @brief 完了通知の送出後にアイドルへ戻す */
    bool OperationState::complete()
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_phase != OperationPhase::Finishing)
            return false;
        m_phase = OperationPhase::Idle;
        m_stage = OperationStage::None;
        return true;
    }

    /** @brief D-Bus用に状態の一貫したスナップショットを取得する */
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

    /**
     * @brief 無進捗を検出する
     * @param timeout 無進捗とみなす経過時間
     * @return 監視による処置 [None]/[Cancel]/[Warn]
     * @note プロンプト応答待ちの間は上限時間まで[None]を返し、RPM中は警告だけで取消しはしない
     */
    WatchdogAction OperationState::watchdog(std::chrono::milliseconds timeout)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        const auto now = std::chrono::steady_clock::now();

        // プロンプト応答待ちの間は無応答とみなさない
        // 上限超過後は通常の判定へ戻す
        if (m_promptWaiting) {
            if (now < m_promptDeadline) {
                return WatchdogAction::None;
            }

            m_promptWaiting = false;
        }

        if (m_phase == OperationPhase::Idle || m_phase == OperationPhase::Finishing
            || now - m_heartbeat < timeout || !m_warning.isEmpty()) {
            return WatchdogAction::None;
        }

        if (cancellable()) {
            m_cancel = true;
            m_warning = QStringLiteral("No progress; cancellation requested");
            return WatchdogAction::Cancel;
        }
        m_warning = QStringLiteral("No progress during RPM transaction; waiting without cancellation");
        return WatchdogAction::Warn;
    }
} // namespace qZypper
