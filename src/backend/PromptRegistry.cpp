#include "PromptRegistry.h"

namespace qZypper {

/** @brief 通知後に回答を待つ。中断・期限切れは安全な既定値を返す。 */
QVariantMap PromptRegistry::ask(const PromptRequest &request, const Notify &notify)
{
    std::unique_lock<std::mutex> lock(m_mutex);
    if (!m_active || m_pendingId != 0 || m_nextId == UINT32_MAX)
        return request.safeDefault;
    m_pendingId = ++m_nextId;
    m_actions = request.actions;
    m_answer.reset();
    const auto id = m_pendingId;
    lock.unlock();
    // 同期回答も許すため、通知はロックの外で行う。
    notify(id, request.type, request.data);
    lock.lock();
    m_ready.wait_for(lock, request.timeout, [this] { return m_answer.has_value() || !m_active; });
    const auto result = m_answer.value_or(request.safeDefault);
    m_pendingId = 0;
    m_answer.reset();
    return result;
}

/** @brief 一致するIDと厳密な回答スキーマのみを受理する。 */
bool PromptRegistry::accepts(quint32 id, const QVariantMap &answer)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_active || id == 0 || id != m_pendingId || m_answer || answer.size() != 1)
        return false;
    const auto action = answer.constFind(QStringLiteral("action"));
    return action != answer.cend() && action->typeId() == QMetaType::QString
           && m_actions.contains(action->toString());
}

bool PromptRegistry::answer(quint32 id, const QVariantMap &answer)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_active || id == 0 || id != m_pendingId || m_answer || answer.size() != 1)
        return false;
    const auto action = answer.constFind(QStringLiteral("action"));
    if (action == answer.cend() || action->typeId() != QMetaType::QString
        || !m_actions.contains(action->toString()))
        return false;
    m_answer = answer;
    m_ready.notify_one();
    return true;
}

/** @brief 操作開始時に問合せを有効化する。 */
void PromptRegistry::open()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_active = true;
}

/** @brief 取消し・所有者消失で待機を解除する。 */
void PromptRegistry::interrupt()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_active = false;
    m_answer.reset();
    m_ready.notify_one();
}

} // namespace qZypper
