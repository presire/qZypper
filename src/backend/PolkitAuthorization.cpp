#include "PolkitAuthorization.h"
#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusMetaType>
#include <QDBusPendingCallWatcher>
#include <QDBusMessage>
#include <QUuid>
#include <QVariantMap>

namespace qZypper {
/** @brief Polkitの(sa{sv})サブジェクト。 */
struct PolicyKitSubject { QString kind; QVariantMap attributes; };
using PolicyKitStringMap = QMap<QString, QString>;
/** @brief サブジェクトをD-Bus構造体へ変換する。 */
QDBusArgument &operator<<(QDBusArgument &argument, const PolicyKitSubject &subject)
{
    argument.beginStructure(); argument << subject.kind << subject.attributes; argument.endStructure();
    return argument;
}
/** @brief サブジェクトをD-Bus構造体から読む。 */
const QDBusArgument &operator>>(const QDBusArgument &argument, PolicyKitSubject &subject)
{
    argument.beginStructure(); argument >> subject.kind >> subject.attributes; argument.endStructure();
    return argument;
}
}
Q_DECLARE_METATYPE(qZypper::PolicyKitSubject)
Q_DECLARE_METATYPE(qZypper::PolicyKitStringMap)

namespace qZypper {
namespace {
/** @brief Authorityへの要求を構築する。 */
QDBusMessage authorityCall(const QString &member)
{
    return QDBusMessage::createMethodCall(QStringLiteral("org.freedesktop.PolicyKit1"),
        QStringLiteral("/org/freedesktop/PolicyKit1/Authority"),
        QStringLiteral("org.freedesktop.PolicyKit1.Authority"), member);
}
/** @brief 正しい型の認可応答だけを受理する。 */
bool authorizedReply(const QDBusMessage &reply)
{
    if (reply.type() != QDBusMessage::ReplyMessage || reply.signature() != QStringLiteral("(bba{ss})")
        || reply.arguments().size() != 1 || reply.arguments().front().typeId() != qMetaTypeId<QDBusArgument>())
        return false;
    const auto argument = reply.arguments().front().value<QDBusArgument>();
    bool authorized = false, challenge = false;
    PolicyKitStringMap details;
    argument.beginStructure(); argument >> authorized >> challenge >> details; argument.endStructure();
    return authorized;
}
}

/** @brief Polkit構造体をQtDBusに登録する。 */
PolkitAuthorization::PolkitAuthorization(QObject *parent) : QObject(parent)
{
    qDBusRegisterMetaType<PolicyKitSubject>();
    qDBusRegisterMetaType<PolicyKitStringMap>();
}

/** @brief AllowUserInteraction付きの認可を開始し取消識別子を返す。 */
QString PolkitAuthorization::request(const QString &caller, const QString &action, const Completion &completion)
{
    const auto id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    m_cancelled.insert(id, false);
    auto call = authorityCall(QStringLiteral("CheckAuthorization"));
    call << QVariant::fromValue(PolicyKitSubject{QStringLiteral("system-bus-name"), {{"name", caller}}})
         << action << QVariant::fromValue(PolicyKitStringMap{}) << quint32(1) << id;
    auto *watcher = new QDBusPendingCallWatcher(QDBusConnection::systemBus().asyncCall(call, 300000), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, watcher, id, caller, completion] {
        const bool cancelled = m_cancelled.take(id);
        const bool authorized = !cancelled && caller.startsWith(':') && authorizedReply(watcher->reply());
        watcher->deleteLater();
        completion(id, authorized);
    });
    return id;
}

/** @brief 同一接続が開始した認可要求を非同期に取消す。 */
void PolkitAuthorization::cancel(const QString &id)
{
    auto it = m_cancelled.find(id);
    if (it == m_cancelled.end() || it.value()) return;
    it.value() = true;
    auto call = authorityCall(QStringLiteral("CancelCheckAuthorization"));
    call << id;
    QDBusConnection::systemBus().asyncCall(call);
}

} // namespace qZypper
