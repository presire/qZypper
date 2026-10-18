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
    /** @brief answer() が受理する回答かを、消費せずに検査する (認可開始前の検証用)。 */
    bool accepts(quint32 id, const QVariantMap &answer);
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
};

} // namespace qZypper
#endif // QZYPPER_BACKEND_PROMPTREGISTRY_H
