#include <QDBusConnectionInterface>
#include <QDBusError>
#include <QDBusReply>
#include "PackageManagerAdaptor.h"
#include "ZyppManager.h"

namespace qZypper {
    /** @brief 制限付きの非同期認可で所有権を獲得する */
    bool PackageManagerAdaptor::Initialize()
    {
        if (!checkNotBusy()) return false;

        const auto caller = calledFromDBus() ? message().service() : QString();

        if (!m_owner.isEmpty()) {
            if (checkOwner()) startWorker(message(), makeInitializeJob());
            return false;
        }

        if (m_quitWhenIdle || m_idleExpired || !caller.startsWith(':')) {
            sendErrorReply(QDBusError::AccessDenied, QStringLiteral("Session unavailable"));
            return false;
        }

        if (m_pendingInitCalls.contains(caller) || m_pendingInitCalls.size() >= kMaxPendingInitAuths) {
            sendErrorReply(QStringLiteral("org.freedesktop.DBus.Error.LimitsExceeded"), QStringLiteral("Too many pending authorization requests"));
            return false;
        }

        auto *bus = QDBusConnection::systemBus().interface();
        const QDBusReply<uint> uid = bus ? bus->serviceUid(caller) : QDBusReply<uint>();

        if (!uid.isValid()) {
            sendErrorReply(QDBusError::AccessDenied, QStringLiteral("Cannot identify caller"));
            return false;
        }

        int pendingForUid = 0;

        for (const auto &pending : m_pendingInitCalls)
            if (pending.uid == uid.value()) ++pendingForUid;

        if (pendingForUid >= kMaxPendingInitAuthsPerUid) {
            sendErrorReply(QStringLiteral("org.freedesktop.DBus.Error.LimitsExceeded"), QStringLiteral("Too many pending authorization requests for this user"));
            return false;
        }

        setDelayedReply(true);

        const auto authId = m_authorization.request(caller, QStringLiteral("org.presire.qzypper.initialize"),
                                                    [this, caller](const QString &, bool authorized) {
                                                        handleInitializeAuth(caller, authorized);
                                                    });

        m_pendingInitCalls.insert(caller, PendingInit{caller, uid.value(), message(), authId});
        m_ownerWatcher.addWatchedService(caller);
        return false;
    }

    /** @brief 認可応答時にも接続と所有権を再確認する */
    void PackageManagerAdaptor::handleInitializeAuth(const QString &caller, bool authorized)
    {
        if (!m_pendingInitCalls.contains(caller)) return;
        const auto pending = m_pendingInitCalls.take(caller);
        m_ownerWatcher.removeWatchedService(caller);

        if (!authorized || m_busy || !m_owner.isEmpty() || m_quitWhenIdle) {
            QDBusConnection::systemBus().send(pending.msg.createErrorReply(QDBusError::AccessDenied,
                QStringLiteral("Authorization denied, cancelled, or session unavailable")));
        } else if (adoptOwner(caller)) {
            startWorker(pending.msg, makeInitializeJob());
            return;
        }

        // アイドル期限後に待っていた認可が所有者を生まずに終わったら、終了を再判定する
        if (m_idleExpired) idleTimeout();
    }

    /** @brief 生存する認可済み接続だけを所有者として登録する */
    bool PackageManagerAdaptor::adoptOwner(const QString &caller)
    {
        m_ownerWatcher.addWatchedService(caller);
        auto *bus = QDBusConnection::systemBus().interface();
        const QDBusReply<bool> alive = bus ? bus->isServiceRegistered(caller) : QDBusReply<bool>();
        if (!alive.isValid() || !alive.value()) {
            m_ownerWatcher.removeWatchedService(caller);
            return false;
        }
        m_owner = caller;
        m_idleExpired = false;
        m_publishedState.clear();
        return true;
    }

    /** @brief 初期化失敗時にも半端なセッションをワーカー上で解放する */
    std::function<PackageManagerAdaptor::AsyncOutcome()> PackageManagerAdaptor::makeInitializeJob()
    {
        return [this] {
            auto &manager = ZyppManager::instance();
            AsyncOutcome result;
            const bool ok = manager.initialize();
            const auto error = QString::fromStdString(manager.lastError());
            result.value = ok;
            if (!ok) manager.shutdown();
            if (!error.isEmpty()) QMetaObject::invokeMethod(this, [this, error] {
                emitToOwner(QStringLiteral("ErrorOccurred"), {error});
            }, Qt::QueuedConnection);
            return result;
        };
    }

    /** @brief 認可待ちの要求を取消す */
    void PackageManagerAdaptor::cancelAuthorization(const QString &id)
    {
        if (!id.isEmpty()) m_authorization.cancel(id);
    }

    /** @brief 所有者消失時もRPMを中断せず、実行終了後にロックを解放する */
    void PackageManagerAdaptor::ownerVanished(const QString &owner)
    {
        const auto pending = m_pendingInitCalls.find(owner);

        if (pending != m_pendingInitCalls.end()) cancelAuthorization(pending->authId);

        if (owner != m_owner || m_owner.isEmpty()) return;

        m_ownerWatcher.removeWatchedService(m_owner);
        m_owner.clear();
        m_quitWhenIdle = true;
        m_executor.control().requestCancel();
        m_executor.prompts().interrupt();
        cancelAuthorization(m_authId);

        if (!m_busy) releaseAbandonedSession();
    }

    /** @brief 操作完了後の孤立したセッションを常駐ワーカー上で破棄する */
    void PackageManagerAdaptor::releaseAbandonedSession()
    {
        m_busy = true;
        m_idleTimer.stop();
        m_executor.control().run(OperationStage::Releasing);
        m_executor.submit([] { ZyppManager::instance().shutdown(); return AsyncOutcome{}; },
            [this](const AsyncOutcome &) {
                m_initialized = false;
                bumpRevision();
                m_executor.control().finishing();
                m_executor.control().complete();
                m_busy = false;
                m_quitWhenIdle = false;
                m_idleTimer.start();
            }, false);
    }

    /** @brief アイドル時に所有者が明示的にロックと選択状態を解放する */
    bool PackageManagerAdaptor::ReleaseSession()
    {
        if (!checkNotBusy() || !checkOwner()) return false;
        startWorker(message(), [] {
            ZyppManager::instance().shutdown();
            AsyncOutcome result;
            result.value = true;
            return result;
        });
        return false;
    }
} // namespace qZypper
