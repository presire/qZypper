#ifndef QZYPPER_BACKEND_PROMPTREGISTRY_H
#define QZYPPER_BACKEND_PROMPTREGISTRY_H

#include <QVariantMap>
#include <QStringList>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <optional>

namespace qZypper {

/** @brief ワーカーが作成する問合せの契約、回答はactionキーのみを許可する */
struct PromptRequest {
    QString type;                                  // 問合せ種別
    QVariantMap data;                              // UIへ送る表示用データ
    std::chrono::milliseconds timeout{60000};      // 回答待機時間
    QVariantMap safeDefault{{"action", "abort"}};  // 未回答時に返す安全側の回答
    QStringList actions{QStringLiteral("abort")};  // 許可するaction値
};

/**
 * @class PromptRegistry
 * @brief 単一の保留中問合せを管理するD-Bus非依存レジストリ
 */
class PromptRegistry {
public:
    using Notify = std::function<void(quint32, const QString &, const QVariantMap &)>; // 問合せ公開通知

    /** @brief 問合せを公開して有効な回答または既定値を待つ */
    QVariantMap ask(const PromptRequest &request, const Notify &notify);

    /** @brief 保留中問合せへ通常の回答を適用する */
    bool answer(quint32 id, const QVariantMap &answer);

    /**
     * @brief 回答を検証し、認可が終わるまで問合せを保留する
     *
     * 保留中は期限切れにならず、通常のanswer()も受け付けない
     * 保留はrelease()かinterrupt()でのみ終わる
     *
     * @param id 問合せID
     * @param answer 認可後に適用する予定の回答
     * @return 回答が有効で保留を開始した場合true
     */
    bool hold(quint32 id, const QVariantMap &answer);

    /**
     * @brief 保留を終え、認可結果に応じた回答を適用する
     * @param id hold()に渡した問合せID
     * @param answer 適用する回答
     * @return 回答を適用できた場合true (中断済み・別の問合せに移った場合はfalse)
     */
    bool release(quint32 id, const QVariantMap &answer);

    /** @brief 新しい問合せを受け付ける状態へ戻す */
    void open();

    /** @brief 保留中問合せを安全側の回答で中断する */
    void interrupt();

private:
    std::mutex m_mutex;                       // 保留状態を保護する排他制御
    std::condition_variable m_ready;          // 回答到着を待つワーカーの通知先
    quint32 m_nextId = 0;                     // 次に割り当てる問合せID
    quint32 m_pendingId = 0;                  // 現在保留中の問合せID
    QStringList m_actions;                    // 現在許可するaction値
    std::optional<QVariantMap> m_answer;      // 適用待ちの検証済み回答
    bool m_active = false;                    // 問合せ待機中か
    bool m_held = false;                      // Polkit認可待ちで期限を停止中か

    /** @brief IDとactionが現在の問合せで受理可能か検査する */
    bool acceptable(quint32 id, const QVariantMap &answer) const;
};

} // namespace qZypper

#endif // QZYPPER_BACKEND_PROMPTREGISTRY_H
