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

/** @brief ワーカーが作成する問合せの契約。回答はactionキーだけを許可する。 */
struct PromptRequest {
    QString type;
    QVariantMap data;
    std::chrono::milliseconds timeout{60000};
    QVariantMap safeDefault{{"action", "abort"}};
    QStringList actions{QStringLiteral("abort")};
};

/** @brief 単一の保留中問合せを管理するD-Bus非依存レジストリ。 */
class PromptRegistry {
public:
    using Notify = std::function<void(quint32, const QString &, const QVariantMap &)>;
    QVariantMap ask(const PromptRequest &request, const Notify &notify);
    bool answer(quint32 id, const QVariantMap &answer);
    /**
     * @brief 回答を検証し、認可が終わるまで問合せを保留する。
     *
     * 保留中は期限切れにならず、通常の answer() も受け付けない。
     * 保留は release() か interrupt() でのみ終わる。
     * @param id 問合せID
     * @param answer 認可後に適用する予定の回答
     * @return 回答が有効で保留を開始した場合 true
     */
    bool hold(quint32 id, const QVariantMap &answer);
    /**
     * @brief 保留を終え、認可結果に応じた回答を適用する。
     * @param id hold() に渡した問合せID
     * @param answer 適用する回答
     * @return 回答を適用できた場合 true (中断済み・別の問合せに移った場合は false)
     */
    bool release(quint32 id, const QVariantMap &answer);
    void open();
    void interrupt();

private:
    std::mutex m_mutex;
    std::condition_variable m_ready;
    quint32 m_nextId = 0;
    quint32 m_pendingId = 0;
    QStringList m_actions;
    std::optional<QVariantMap> m_answer;
    bool m_active = false;
    bool m_held = false;
    bool acceptable(quint32 id, const QVariantMap &answer) const;
};

} // namespace qZypper
#endif // QZYPPER_BACKEND_PROMPTREGISTRY_H
