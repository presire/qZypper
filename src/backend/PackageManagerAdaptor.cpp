#include <QCoreApplication>
#include <QDBusError>
#include <QDBusArgument>
#include "PackageManagerAdaptor.h"
#include "InputValidation.h"

namespace qZypper {

/**
 * @brief D-Bus側のタイマと所有者監視を設定する
 * @note 所有者の接続中はアイドル終了しない (所有者の切断・解放後に5分で終了する)
 */
PackageManagerAdaptor::PackageManagerAdaptor(QObject *parent) : QObject(parent)
{
    m_idleTimer.setInterval(5 * 60 * 1000);
    m_idleTimer.setSingleShot(true);

    connect(&m_idleTimer, &QTimer::timeout, this, &PackageManagerAdaptor::idleTimeout);

    m_idleTimer.start();
    m_ownerWatcher.setConnection(QDBusConnection::systemBus());
    m_ownerWatcher.setWatchMode(QDBusServiceWatcher::WatchForUnregistration);

    connect(&m_ownerWatcher, &QDBusServiceWatcher::serviceUnregistered, this, &PackageManagerAdaptor::ownerVanished);
    connect(&m_executor, &ZyppExecutor::untrustedKey, this, [this](const QVariantMap &key) {
        emitToOwner(QStringLiteral("UntrustedKeyDetected"), {key});
    });
    connect(&m_executor, &ZyppExecutor::promptRequested, this,
        [this](quint32 id, const QString &type, const QVariantMap &data) {
            if (!m_owner.isEmpty()) emitToOwner(QStringLiteral("PromptRequested"), {id, type, data});
            else m_executor.prompts().interrupt();
        });
    connect(&m_executor, &ZyppExecutor::promptClosed, this, [this](quint32 id) {
        if (!m_owner.isEmpty()) emitToOwner(QStringLiteral("PromptClosed"), {id});
    });

    bool validTimeout = false;
    const int configured = qEnvironmentVariableIntValue("QZYPPER_WATCHDOG_TIMEOUT_MS", &validTimeout);
    const auto timeout = std::chrono::milliseconds(validTimeout && configured > 0 ? configured : 600000);
    m_watchdog.setInterval(1000);

    connect(&m_watchdog, &QTimer::timeout, this, [this, timeout] {
        const auto action = m_executor.control().watchdog(timeout);
        if (action == WatchdogAction::Cancel) {
            m_executor.prompts().interrupt();
            cancelAuthorization(m_authId);
        }

        if (action != WatchdogAction::None)
            emitToOwner(QStringLiteral("ErrorOccurred"), {m_executor.control().snapshot().value("warning")});

        publishState();
    });

    m_watchdog.start();
}

/** @brief 所有者だけがアイドル期限を延長する */
void PackageManagerAdaptor::resetIdleTimer()
{
    if (!m_busy && calledFromDBus() && message().service() == m_owner && !m_owner.isEmpty())
        m_idleTimer.start();
}

/**
 * @brief 所有者不在のアイドル期限で終了する
 * @note 期限時点のInitialize認可待ちは結果まで待機するが、以降の新規要求は断る
 *       (認可要求の繰返しによる無期限の延命を防ぐ。延長は認可応答の待機上限の5分まで)
 */
void PackageManagerAdaptor::idleTimeout()
{
    if (m_busy || !m_owner.isEmpty()) {
        m_idleExpired = false;
        m_idleTimer.start();
        return;
    }

    if (!m_pendingInitCalls.isEmpty()) {
        m_idleExpired = true;
        return;
    }

    QCoreApplication::quit();
}

/** @brief 実行中のワーカーや認可と競合する要求を拒否する */
bool PackageManagerAdaptor::checkNotBusy()
{
    if (!m_busy) return true;

    sendErrorReply(QStringLiteral("org.presire.qzypper.Error.Busy"), QStringLiteral("Another operation is in progress"));
    return false;
}

/**
 * @brief 読取要求を所有者だけに許可し、ワーカーとの競合も防ぐ
 *
 * 所有者以外は実行中かどうかに関係なくAccessDeniedとし、ワーカーもアイドル期限も使用させない
 */
bool PackageManagerAdaptor::checkRead()
{
    if (!checkOwner() || !checkNotBusy() || !checkArguments()) return false;

    if (m_initialized) return true;

    sendErrorReply(QStringLiteral("org.presire.qzypper.Error.NotInitialized"), QStringLiteral("Backend not initialized"));
    return false;
}

/** @brief 一意バス名で所有者を検査する */
bool PackageManagerAdaptor::checkOwner()
{
    if (calledFromDBus() && !m_owner.isEmpty() && message().service() == m_owner && !m_quitWhenIdle) {
        resetIdleTimer();
        return true;
    }

    sendErrorReply(QDBusError::AccessDenied, QStringLiteral("Another client owns the qZypper session"));
    return false;
}

/**
 * @brief 現在のD-Bus呼出し引数を型と内容の両面から検証する
 * @return 引数が安全に処理できる場合はtrue
 * @note .repo本文は改行を許可する専用検証を使用する
 */
bool PackageManagerAdaptor::checkArguments()
{
    QString error;
    auto arguments = message().arguments();
    const QString member = message().member();
    if (member == QLatin1String("ImportRepoFile") || member == QLatin1String("ParseRepoFile")) {
        // .repo本文は複数行のため共通の制御文字検査から除外する
        // ここではサイズと改行・タブ以外の制御文字のみ拒否し、各フィールドはパーサで検証する
        if (validateRepoFileContent(arguments, error)) return true;

        sendErrorReply(QDBusError::InvalidArgs, error);
        return false;
    }

    for (auto &argument : arguments) {
        if (argument.metaType() == QMetaType::fromType<QDBusArgument>()) {
            const auto wire = argument.value<QDBusArgument>();
            if (wire.currentSignature() == "a{sv}") argument = qdbus_cast<QVariantMap>(wire);
        }
    }

    if (validateArguments(arguments, error)) return true;

    sendErrorReply(QDBusError::InvalidArgs, error);
    return false;
}

/** @brief すべての通知を所有者だけへunicastする */
void PackageManagerAdaptor::emitToOwner(const QString &member, const QVariantList &args)
{
    if (m_owner.isEmpty()) return;

    auto signal = QDBusMessage::createTargetedSignal(m_owner, QStringLiteral("/org/presire/qzypper"),
                                                     QStringLiteral("org.presire.qzypper.PackageManager"), member);
    signal.setArguments(args);
    QDBusConnection::systemBus().send(signal);
}

/** @brief 変更のあった状態だけを通知する */
void PackageManagerAdaptor::publishState()
{
    const auto state = m_executor.control().snapshot();

    if (state == m_publishedState) return;

    m_publishedState = state;
    emitToOwner(QStringLiteral("OperationStateChanged"), {state});
}

/** @brief 所有者に状態を返す
 *         自身のInitialize認可待ちも照会できる
 */
QVariantMap PackageManagerAdaptor::GetOperationState()
{
    if (calledFromDBus() && m_pendingInitCalls.contains(message().service()))
        return {{"state", "authorizing"}, {"stage", "initializing"}, {"busy", true}, {"cancellable", true}};

    if (!checkOwner()) return {};

    return m_executor.control().snapshot();
}

/** @brief 所有者の一致する保留中問合せへの回答だけを受理する */
bool PackageManagerAdaptor::AnswerPrompt(uint id, const QVariantMap &answer)
{
    if (!checkOwner()) return false;

    // 鍵の信頼 (一時・恒久とも) は所有者のtrust-key認可を待ってから確定する (ask-then-authorize)
    const QString action = answer.value(QStringLiteral("action")).toString();

    if (action == QLatin1String("trust-permanent") || action == QLatin1String("trust-temporary")) {
        if (!m_authId.isEmpty()) {
            sendErrorReply(QStringLiteral("org.presire.qzypper.Error.Busy"), QStringLiteral("An authorization request is already pending"));
            return false;
        }

        // 認可ダイアログを出す前に回答を検証し、認可が終わるまで問合せの期限切れを止める
        if (!m_executor.prompts().hold(id, answer)) {
            sendErrorReply(QDBusError::InvalidArgs, QStringLiteral("Invalid, expired, or mismatched prompt answer"));
            return false;
        }

        const QVariantMap requested = answer;
        m_authId = m_authorization.request(m_owner, QStringLiteral("org.presire.qzypper.trust-key"),
                                           [this, id, requested](const QString &authId, bool authorized) {
            clearAuthorizationId(m_authId, authId);
            const QVariantMap effective = authorized ? requested : QVariantMap{{QStringLiteral("action"), QStringLiteral("reject")}};

            if (!m_executor.prompts().release(id, effective)) {
                // 認可は得たが中断などで問合せが既に終わっていた場合は、反映できなかったことを知らせる
                // 取消しで終わった場合は鍵が拒否されたままなので通知しない
                if (authorized)
                    emitToOwner(QStringLiteral("ErrorOccurred"),
                        {QStringLiteral("The key trust answer could not be applied because the prompt has ended")});
            }
            else if (!authorized) {
                emitToOwner(QStringLiteral("ErrorOccurred"),
                    {QStringLiteral("Authorization to trust the key was denied; the key was rejected")});
            }
        });
        return true;
    }

    if (m_executor.prompts().answer(id, answer)) return true;

    sendErrorReply(QDBusError::InvalidArgs, QStringLiteral("Invalid, expired, or mismatched prompt answer"));
    return false;
}

/** @brief 認可待ちまたは取消し可能な実行中処理を中断する */
void PackageManagerAdaptor::CancelOperation()
{
    const QString caller = calledFromDBus() ? message().service() : QString();

    if (m_pendingInitCalls.contains(caller)) {
        cancelAuthorization(m_pendingInitCalls.value(caller).authId);
        return;
    }

    if (!checkOwner()) return;

    if (m_executor.control().requestCancel()) {
        m_executor.prompts().interrupt();
        cancelAuthorization(m_authId);
    }

    publishState();
}

/** @brief 選択変更を記録し確認済みソルバー結果を無効にする */
void PackageManagerAdaptor::bumpRevision()
{
    ++m_revision;
    m_resolvedRevision = 0;
}

/** @brief libzyppに触れず改訂番号を返す
 *         選択状態の変化が分かるため所有者に限る
 */
qulonglong PackageManagerAdaptor::GetSelectionRevision()
{
    if (!checkOwner()) return 0;

    return m_revision;
}

} // namespace qZypper
