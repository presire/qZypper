#include <QCoreApplication>
#include <QDBusArgument>
#include <QDBusConnectionInterface>
#include <QDBusError>
#include <QDBusMetaType>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusReply>
#include <QDBusVariant>
#include <QDebug>
#include <QMap>
#include <QThread>
#include <atomic>
#include <exception>
#include <memory>
#include "PackageManagerAdaptor.h"
#include "ZyppManager.h"
#include "ZyppCallbackReceiver.h"

namespace qZypper {

/** @brief Polkitのsystem-bus-name subjectのD-Bus構造体 */
struct PolicyKitSubject {
    QString kind;
    QVariantMap attributes;
};

using PolicyKitStringMap = QMap<QString, QString>;

/**
 * @brief Polkit subjectを(sa{sv})としてマーシャリングする。
 * @param argument 書込先
 * @param subject 認可対象
 * @return 書込先
 */
QDBusArgument &operator<<(QDBusArgument &argument, const PolicyKitSubject &subject)
{
    argument.beginStructure();
    argument << subject.kind << subject.attributes;
    argument.endStructure();
    return argument;
}

/**
 * @brief Polkit subjectをデマーシャリングする。
 * @param argument 読込元
 * @param subject 認可対象の格納先
 * @return 読込元
 */
const QDBusArgument &operator>>(const QDBusArgument &argument, PolicyKitSubject &subject)
{
    argument.beginStructure();
    argument >> subject.kind >> subject.attributes;
    argument.endStructure();
    return argument;
}

} // namespace qZypper

Q_DECLARE_METATYPE(qZypper::PolicyKitSubject)
Q_DECLARE_METATYPE(qZypper::PolicyKitStringMap)

namespace qZypper {

/**
 * @brief 認可・所有者監視・未承認鍵の主スレッド通知を設定する。
 * @param parent 親QObject
 */
PackageManagerAdaptor::PackageManagerAdaptor(QObject *parent)
    : QObject(parent)
{
    qDBusRegisterMetaType<PolicyKitSubject>();
    qDBusRegisterMetaType<PolicyKitStringMap>();

    m_idleTimer.setInterval(5 * 60 * 1000);
    m_idleTimer.setSingleShot(true);
    connect(&m_idleTimer, &QTimer::timeout, this, [this]() {
        if (!m_busy && m_pendingInitAuths == 0)
            QCoreApplication::quit();
        else
            m_idleTimer.start();
    });
    m_idleTimer.start();

    m_ownerWatcher.setConnection(QDBusConnection::systemBus());
    m_ownerWatcher.setWatchMode(QDBusServiceWatcher::WatchForUnregistration);
    connect(&m_ownerWatcher, &QDBusServiceWatcher::serviceUnregistered,
            this, &PackageManagerAdaptor::ownerVanished);

    ZyppManager::instance().setUntrustedKeyCallback([this](const UntrustedKeyInfo &key) {
        QVariantMap info;
        info["fingerprint"] = QString::fromStdString(key.fingerprint);
        info["id"] = QString::fromStdString(key.id);
        info["name"] = QString::fromStdString(key.name);
        info["created"] = QVariant::fromValue(static_cast<qlonglong>(key.created));
        info["expires"] = QVariant::fromValue(static_cast<qlonglong>(key.expires));
        info["repoAlias"] = QString::fromStdString(key.repoAlias);
        info["repoName"] = QString::fromStdString(key.repoName);
        QMetaObject::invokeMethod(this, [this, info]() {
            emit UntrustedKeyDetected(info);
        }, Qt::QueuedConnection);
    });
}

/** @brief 所有者の呼出のみでアイドル期限を延長する。 */
void PackageManagerAdaptor::resetIdleTimer()
{
    if (!m_busy && !m_quitWhenIdle && calledFromDBus()
        && !m_owner.isEmpty() && message().service() == m_owner) {
        m_idleTimer.start();
    }
}

/**
 * @brief ワーカー処理中は、libzyppに触れる呼出を即座に拒否する。
 * @return 呼出可能ならtrue
 */
bool PackageManagerAdaptor::checkNotBusy()
{
    if (m_busy) {
        sendErrorReply(QStringLiteral("org.presire.qzypper.Error.Busy"), QStringLiteral("Another operation is in progress"));
        return false;
    }
    return true;
}

/**
 * @brief 読取呼出の処理中検査と所有者のアイドル更新を行う。
 * @return 呼出可能ならtrue
 */
bool PackageManagerAdaptor::checkRead()
{
    if (!checkNotBusy())
        return false;
    resetIdleTimer();
    return true;
}

/**
 * @brief 所有者の一意バス名を検査する (暗黙の獲得は行わない)
 * @return 生存する所有者の呼出ならtrue
 */
bool PackageManagerAdaptor::checkOwner()
{
    const QString caller = calledFromDBus() ? message().service() : QString();
    if (m_quitWhenIdle || m_owner.isEmpty() || caller != m_owner) {
        sendErrorReply(QDBusError::AccessDenied,
                       QStringLiteral("Another client owns the qZypper session"));
        return false;
    }
    resetIdleTimer();
    return true;
}

/**
 * @brief 所有者消失時にプールを次のクライアントへ引き継がず終了する。
 * @param owner 消失した一意バス名
 */
void PackageManagerAdaptor::ownerVanished(const QString &owner)
{
    if (m_owner.isEmpty() || owner != m_owner)
        return;
    m_ownerWatcher.removeWatchedService(m_owner);
    m_owner.clear();
    m_quitWhenIdle = true;
    m_idleTimer.stop();
    if (!m_busy)
        QTimer::singleShot(0, qApp, &QCoreApplication::quit);
}

/** @brief 選択改訂番号を進め、解決済みの確認を無効化する。 */
void PackageManagerAdaptor::bumpRevision()
{
    ++m_revision;
    m_resolvedRevision = 0;
}

/**
 * @brief 同期の所有者操作を実行し、例外時も改訂を無効化する。
 * @param job 主スレッドで実行する処理
 * @param bump 改訂番号を進めるか
 * @param solver 成功時に解決済み改訂を記録するか
 * @return 処理結果 (拒否・例外時は空)
 */
QVariant PackageManagerAdaptor::runOwnerSync(const std::function<QVariant()> &job, bool bump, bool solver)
{
    if (!checkNotBusy() || !checkOwner())
        return {};
    try {
        const QVariant result = job();
        if (bump)
            bumpRevision();
        if (solver && result.toMap().value("success").toBool())
            m_resolvedRevision = m_revision;
        return result;
    }
    catch (const std::exception &e) {
        sendErrorReply(QDBusError::Failed, QString::fromUtf8(e.what()));
    }
    catch (...) {
        sendErrorReply(QDBusError::Failed, QStringLiteral("Unknown exception during operation"));
    }

    if (bump)
        bumpRevision();
    return {};
}

/**
 * @brief 特権操作の処理中・所有者・初期化状態を順に検査する。
 * @return 認可ワーカーを開始可能ならtrue
 */
bool PackageManagerAdaptor::preparePrivileged()
{
    if (!checkNotBusy() || !checkOwner())
        return false;
    if (!ZyppManager::instance().isInitialized()) {
        sendErrorReply(QStringLiteral("org.presire.qzypper.Error.NotInitialized"), QStringLiteral("Backend not initialized"));
        return false;
    }
    return true;
}

/**
 * @brief Polkit 認可要求を構築する。
 * @param caller 呼出元の一意バス名
 * @param actionId PolkitアクションID
 * @return CheckAuthorizationのD-Bus要求
 */
QDBusMessage PackageManagerAdaptor::buildAuthRequest(const QString &caller, const QString &actionId)
{
    PolicyKitSubject subject;
    subject.kind = QStringLiteral("system-bus-name");
    // a{sv}の値はQtDBusがvariant化するため、QDBusVariantで包むとv(v(s))になり、polkitdに拒否される
    subject.attributes.insert(QStringLiteral("name"), caller);
    QDBusMessage request = QDBusMessage::createMethodCall(
        QStringLiteral("org.freedesktop.PolicyKit1"),
        QStringLiteral("/org/freedesktop/PolicyKit1/Authority"),
        QStringLiteral("org.freedesktop.PolicyKit1.Authority"),
        QStringLiteral("CheckAuthorization"));
    request << QVariant::fromValue(subject) << actionId
            << QVariant::fromValue(PolicyKitStringMap())
            << QVariant::fromValue(quint32(1)) << QString();
    return request;
}

/**
 * @brief Polkit認可応答を検証し認可の有無を返す (異常時は拒否)
 * @param reply Polkitからの応答
 * @return is_authorizedがtrueの場合のみtrue
 */
bool PackageManagerAdaptor::parseAuthReply(const QDBusMessage &reply)
{
    if (reply.type() != QDBusMessage::ReplyMessage
        || reply.signature() != QStringLiteral("(bba{ss})")
        || reply.arguments().size() != 1
        || reply.arguments().at(0).metaType() != QMetaType::fromType<QDBusArgument>()) {
        return false;
    }

    const QDBusArgument argument = reply.arguments().at(0).value<QDBusArgument>();
    bool authorized = false;
    bool challenge = false;
    PolicyKitStringMap details;
    argument.beginStructure();
    argument >> authorized >> challenge >> details;
    argument.endStructure();
    return authorized;
}

/**
 * @brief 一意バス名を対象にPolkitに問い合わせ、異常時は必ず拒否する。
 * @param caller 呼出元の一意バス名
 * @param actionId PolkitアクションID
 * @return 正しい認可応答のis_authorizedがtrueの場合のみtrue
 */
bool PackageManagerAdaptor::checkAuthorization(const QString &caller, const QString &actionId)
{
    if (caller.isEmpty() || !caller.startsWith(':'))
        return false;
    const QDBusMessage reply = QDBusConnection::systemBus().call(buildAuthRequest(caller, actionId), QDBus::Block, 300000);
    return parseAuthReply(reply);
}

/**
 * @brief 単一ワーカーで操作を実行し、主スレッドで遅延返信する (認可なし)
 * @param msg 保持したD-Bus呼出
 * @param job 事前検査済みの操作
 */
void PackageManagerAdaptor::startWorker(const QDBusMessage &msg, const std::function<AsyncOutcome()> &job)
{
    // 非同期認可完了後 (handleInitializeAuth) は D-Bus呼出文脈外のため、文脈内でのみ設定する
    if (calledFromDBus())
        setDelayedReply(true);
    auto outcome = std::make_shared<AsyncOutcome>();
    auto token = std::make_shared<std::atomic<bool>>(false);
    m_cancelToken = token;
    m_busy = true;
    m_idleTimer.stop();

    try {
        QThread *thread = QThread::create([job, outcome, token]() {
            try {
                if (token->load()) {
                    outcome->errorName = QStringLiteral("org.presire.qzypper.Error.Cancelled");
                    outcome->errorText = QStringLiteral("Operation cancelled");
                    return;
                }
                *outcome = job();
            } catch (const std::exception &e) {
                outcome->errorName = QDBusError::errorString(QDBusError::Failed);
                outcome->errorText = QString::fromUtf8(e.what());
            } catch (...) {
                outcome->errorName = QDBusError::errorString(QDBusError::Failed);
                outcome->errorText = QStringLiteral("Unknown exception during operation");
            }
        });
        connect(thread, &QThread::finished, this, [this, thread, msg, outcome]() {
            thread->wait();
            thread->deleteLater();
            finishPrivileged(msg, *outcome);
        }, Qt::QueuedConnection);
        thread->start();
    } catch (const std::exception &e) {
        outcome->errorName = QDBusError::errorString(QDBusError::Failed);
        outcome->errorText = QString::fromUtf8(e.what());
        finishPrivileged(msg, *outcome);
    } catch (...) {
        outcome->errorName = QDBusError::errorString(QDBusError::Failed);
        outcome->errorText = QStringLiteral("Unable to start operation worker");
        finishPrivileged(msg, *outcome);
    }
}

/**
 * @brief 認可と操作を単一ワーカーで行い、主スレッドで遅延返信する。
 * @param actionId 認可対象アクション
 * @param job 事前検査済みの操作
 */
void PackageManagerAdaptor::startPrivileged(const QString &actionId, const std::function<AsyncOutcome()> &job)
{
    const QString caller = message().service();
    const QDBusMessage msg = message();
    startWorker(msg, [this, caller, actionId, job]() {
        AsyncOutcome cancelled;
        if (!checkAuthorization(caller, actionId)) {
            cancelled.errorName = QDBusError::errorString(QDBusError::AccessDenied);
            cancelled.errorText = QStringLiteral("Not authorized: %1").arg(actionId);
            return cancelled;
        }
        // 認可待ちの間に取り消された場合は操作を実行しない
        const auto token = m_cancelToken;
        if (token && token->load()) {
            cancelled.errorName = QStringLiteral("org.presire.qzypper.Error.Cancelled");
            cancelled.errorText = QStringLiteral("Operation cancelled");
            return cancelled;
        }
        return job();
    });
}

/**
 * @brief 主スレッドで完了通知・返信・改訂更新・安全な終了を行う。
 * @param msg 保持したD-Bus呼出
 * @param outcomeワーカーの結果
 */
void PackageManagerAdaptor::finishPrivileged(const QDBusMessage &msg, const AsyncOutcome &outcome)
{
    if (msg.member() == QStringLiteral("Commit")) {
        emit TransactionFinished(outcome.transactionFinished && outcome.transactionSuccess,
                                 outcome.transactionFinished
                                     ? outcome.transactionSummary : outcome.errorText);
    }
    const QDBusMessage reply = outcome.errorName.isEmpty() ? msg.createReply(QVariant::fromValue(outcome.value))
                                                           : msg.createErrorReply(outcome.errorName, outcome.errorText);
    QDBusConnection::systemBus().send(reply);
    m_busy = false;
    m_cancelToken.reset();
    bumpRevision();
    if (m_quitWhenIdle)
        QTimer::singleShot(0, qApp, &QCoreApplication::quit);
    else
        m_idleTimer.start();
}

/**
 * @brief 真偽値の特権操作を実行し、libzyppの失敗理由を直後に取得する。
 * @param actionId 認可対象アクション
 * @param job ワーカーで実行する真偽値の操作
 * @return 遅延返信のため常にfalse
 */
bool PackageManagerAdaptor::runPrivilegedBool(const QString &actionId,
                                             const std::function<bool()> &job)
{
    if (!preparePrivileged())
        return false;
    startPrivileged(actionId, [job]() {
        AsyncOutcome outcome;
        if (job()) {
            outcome.value = true;
        } else {
            outcome.errorName = QDBusError::errorString(QDBusError::Failed);
            outcome.errorText = QString::fromStdString(ZyppManager::instance().lastError());
        }
        return outcome;
    });
    return false;
}

/** @brief 認可後に所有権を確立してlibzyppを初期化する。
 *  @return 初期化成功時はtrue (認可待ちは遅延返信)
 */
bool PackageManagerAdaptor::Initialize()
{
    static const QString actionId = QStringLiteral("org.presire.qzypper.initialize");
    if (m_quitWhenIdle) {
        sendErrorReply(QDBusError::AccessDenied, QStringLiteral("Another client owns the qZypper session"));
        return false;
    }
    const QString caller = calledFromDBus() ? message().service() : QString();
    if (!m_owner.isEmpty()) {
        if (caller != m_owner) {
            sendErrorReply(QDBusError::AccessDenied,
                           QStringLiteral("Another client owns the qZypper session"));
            return false;
        }

        if (!checkNotBusy()) {
            return false;
        }

        if (ZyppManager::instance().isInitialized()) {
            resetIdleTimer();
            return true;
        }

        setDelayedReply(true);
        startWorker(message(), makeInitializeJob());
        return false;
    }

    if (!checkNotBusy()) {
        return false;
    }

    if (caller.isEmpty() || !caller.startsWith(':')) {
        sendErrorReply(QDBusError::AccessDenied,
                       QStringLiteral("Not authorized: %1").arg(actionId));
        return false;
    }

    // 認可待ちの間はワーカーも m_busy も使わず、複数呼出の同時待ちを許す
    // ただし同一呼出元は1件まで、全呼出元で合計8件までとする
    if (m_pendingInitAuths >= 8) {
        sendErrorReply(QStringLiteral("org.freedesktop.DBus.Error.LimitsExceeded"), QStringLiteral("Too many pending authorization requests"));
        return false;
    }

    for (auto it = m_pendingInitCalls.constBegin(); it != m_pendingInitCalls.constEnd(); ++it) {
        if (it.value().first == caller) {
            sendErrorReply(QStringLiteral("org.presire.qzypper.Error.Busy"), QStringLiteral("Authorization already pending"));
            return false;
        }
    }

    setDelayedReply(true);

    const QDBusMessage msg = message();

    ++m_pendingInitAuths;

    QDBusPendingCall call = QDBusConnection::systemBus().asyncCall(buildAuthRequest(caller, actionId), 300000);

    auto *watcher = new QDBusPendingCallWatcher(call, this);
    m_pendingInitCalls.insert(watcher, qMakePair(caller, msg));

    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher *self) { handleInitializeAuth(self); });
    return false;
}

/**
 * @brief libzypp 初期化のワーカー処理を生成する (失敗時は通知して false を返す)。
 * @return 初期化ジョブ
 */
std::function<PackageManagerAdaptor::AsyncOutcome()> PackageManagerAdaptor::makeInitializeJob()
{
    return [this]() {
        AsyncOutcome outcome;
        auto &mgr = ZyppManager::instance();
        if (mgr.initialize()) {
            outcome.value = true;
        }
        else {
            const QString error = QString::fromStdString(mgr.lastError());
            QMetaObject::invokeMethod(this, [this, error]() {
                emit ErrorOccurred(error);
            }, Qt::QueuedConnection);
            outcome.value = false;
        }
        return outcome;
    };
}

/**
 * @brief 呼出元を所有者として採用できるか検査し登録する。
 * @param caller 呼出元の一意バス名
 * @return 採用できた場合はtrue
 */
bool PackageManagerAdaptor::adoptOwner(const QString &caller)
{
    if (caller.isEmpty() || !caller.startsWith(':') || !m_owner.isEmpty())
        return false;
    m_ownerWatcher.addWatchedService(caller);
    auto *interface = QDBusConnection::systemBus().interface();
    const QDBusReply<bool> registered = interface ? interface->isServiceRegistered(caller)
                                                  : QDBusReply<bool>();
    if (!registered.isValid() || !registered.value()) {
        m_ownerWatcher.removeWatchedService(caller);
        return false;
    }
    m_owner = caller;
    return true;
}

/**
 * @brief Initialize の非同期認可結果を処理し所有権を確立する。
 * @param watcher 認可待ちの監視子
 */
void PackageManagerAdaptor::handleInitializeAuth(QDBusPendingCallWatcher *watcher)
{
    static const QString actionId = QStringLiteral("org.presire.qzypper.initialize");
    const auto pending = m_pendingInitCalls.take(watcher);
    const QString caller = pending.first;
    const QDBusMessage msg = pending.second;
    --m_pendingInitAuths;
    watcher->deleteLater();

    if (!parseAuthReply(watcher->reply())) {
        QDBusConnection::systemBus().send(msg.createErrorReply(
            QDBusError::AccessDenied,
            QStringLiteral("Not authorized: %1").arg(actionId)));
        return;
    }
    if (m_quitWhenIdle || !m_owner.isEmpty()) {
        QDBusConnection::systemBus().send(msg.createErrorReply(
            QDBusError::AccessDenied,
            QStringLiteral("Another client owns the qZypper session")));
        return;
    }
    if (m_busy) {
        QDBusConnection::systemBus().send(msg.createErrorReply(
            QStringLiteral("org.presire.qzypper.Error.Busy"),
            QStringLiteral("Another operation is in progress")));
        return;
    }
    // 呼出元が応答前に切断した場合は返信せず破棄する。
    if (!adoptOwner(caller))
        return;
    startWorker(msg, makeInitializeJob());
}

/** @brief 所有者の選択を保存する (改訂は維持) */
void PackageManagerAdaptor::SaveState()
{
    runOwnerSync([]() {
        ZyppManager::instance().saveState();
        return QVariant();
    }, false);
}

/** @brief 所有者の保存済み選択を復元する。 */
void PackageManagerAdaptor::RestoreState()
{
    runOwnerSync([]() {
        ZyppManager::instance().restoreState();
        return QVariant();
    });
}

/** @brief リポジトリ一覧を取得する。
 *  @return リポジトリ情報のリスト */
QVariantList PackageManagerAdaptor::GetRepos()
{
    if (!checkRead())
        return {};
    QVariantList result;
    for (const auto &repo : ZyppManager::instance().getRepos())
        result.append(repo.toVariantMap());
    return result;
}

/** @brief 認可後に全リポジトリを更新する
 *  @return 遅延返信値
 */
bool PackageManagerAdaptor::RefreshRepos()
{
    return runPrivilegedBool(QStringLiteral("org.presire.qzypper.refresh-repos"), [this]() {
        return ZyppManager::instance().refreshRepos([this](const std::string &alias, int pct) {
            const QString repoAlias = QString::fromStdString(alias);
            QMetaObject::invokeMethod(this, [this, repoAlias, pct]() {
                emit RepoRefreshProgress(repoAlias, pct);
            }, Qt::QueuedConnection);
        });
    });
}

/**
 * @brief 認可後に指定リポジトリを更新する。
 * @param alias リポジトリエイリアス
 * @return 遅延返信値
 */
bool PackageManagerAdaptor::RefreshSingleRepo(const QString &alias)
{
    return runPrivilegedBool(QStringLiteral("org.presire.qzypper.refresh-repos"), [this, alias]() {
        return ZyppManager::instance().refreshRepo(alias.toStdString(),
            [this](const std::string &name, int pct) {
                const QString repoAlias = QString::fromStdString(name);
                QMetaObject::invokeMethod(this, [this, repoAlias, pct]() {
                    emit RepoRefreshProgress(repoAlias, pct);
                }, Qt::QueuedConnection);
            });
    });
}

/**
 * @brief 有効な指紋を認証付きで承認する。
 * @param fingerprint 署名鍵の指紋
 * @return 遅延返信値
 */
bool PackageManagerAdaptor::TrustKey(const QString &fingerprint)
{
    if (!preparePrivileged())
        return false;
    const std::string normalized = KeyRingReceiver::normalizeFingerprint(fingerprint.toStdString());
    if (normalized.empty()) {
        sendErrorReply(QDBusError::InvalidArgs, QStringLiteral("Invalid signing key fingerprint"));
        return false;
    }
    startPrivileged(QStringLiteral("org.presire.qzypper.trust-key"), [normalized]() {
        AsyncOutcome outcome;
        if (ZyppManager::instance().approveKeyFingerprint(normalized)) {
            outcome.value = true;
        } else {
            outcome.errorName = QDBusError::errorString(QDBusError::Failed);
            outcome.errorText = QString::fromStdString(ZyppManager::instance().lastError());
        }
        return outcome;
    });
    return false;
}

/**
 * @brief 認可後に URL からリポジトリを追加する。
 * @param url リポジトリURL
 * @param name リポジトリ名
 * @return 遅延返信でsuccessと失敗理由を返す
 */
QVariantMap PackageManagerAdaptor::AddRepo(const QString &url, const QString &name)
{
    if (!preparePrivileged())
        return {};
    startPrivileged(QStringLiteral("org.presire.qzypper.manage-repos"), [url, name]() {
        auto &mgr = ZyppManager::instance();
        QVariantMap result;
        const bool success = mgr.addRepo(url.toStdString(), name.toStdString());
        const QString error = success ? QString() : QString::fromStdString(mgr.lastError());
        result["success"] = success;
        if (!success)
            result["errorMessage"] = error;
        AsyncOutcome outcome;
        outcome.value = result;
        return outcome;
    });
    return {};
}

/**
 * @brief 認可後に詳細プロパティからリポジトリを追加する。
 * @param properties リポジトリプロパティ
 * @return 遅延返信でsuccessと失敗理由を返す
 */
QVariantMap PackageManagerAdaptor::AddRepoFull(const QVariantMap &properties)
{
    if (!preparePrivileged())
        return {};
    const RepoInfo info = RepoInfo::fromVariantMap(properties);
    startPrivileged(QStringLiteral("org.presire.qzypper.manage-repos"), [info]() {
        auto &mgr = ZyppManager::instance();
        QVariantMap result;
        const bool success = mgr.addRepo(info);
        const QString error = success ? QString() : QString::fromStdString(mgr.lastError());
        result["success"] = success;
        if (!success)
            result["errorMessage"] = error;
        AsyncOutcome outcome;
        outcome.value = result;
        return outcome;
    });
    return {};
}

/** @brief 認可後にリポジトリを削除する
 *  @param alias エイリアス
 *  @return 遅延返信値
 */
bool PackageManagerAdaptor::RemoveRepo(const QString &alias)
{
    return runPrivilegedBool(QStringLiteral("org.presire.qzypper.manage-repos"), [alias]() {
        return ZyppManager::instance().removeRepo(alias.toStdString());
    });
}

/**
 * @brief 認可後にリポジトリの有効状態を変更する。
 * @param alias エイリアス
 * @param enabled 有効状態
 * @return 遅延返信値
 */
bool PackageManagerAdaptor::SetRepoEnabled(const QString &alias, bool enabled)
{
    return runPrivilegedBool(QStringLiteral("org.presire.qzypper.manage-repos"), [alias, enabled]() {
        return ZyppManager::instance().setRepoEnabled(alias.toStdString(), enabled);
    });
}

/**
 * @brief 認可後にリポジトリのプロパティを変更する。
 * @param alias エイリアス
 * @param properties 変更後のプロパティ
 * @return 遅延返信値
 */
bool PackageManagerAdaptor::ModifyRepo(const QString &alias, const QVariantMap &properties)
{
    return runPrivilegedBool(QStringLiteral("org.presire.qzypper.manage-repos"), [alias, properties]() {
        return ZyppManager::instance().modifyRepo(alias.toStdString(), RepoInfo::fromVariantMap(properties));
    });
}

/** @brief サービス一覧を取得する
 *  @return サービス情報のリスト
 */
QVariantList PackageManagerAdaptor::GetServices()
{
    if (!checkRead())
        return {};
    QVariantList result;
    for (const auto &service : ZyppManager::instance().getServices())
        result.append(service.toVariantMap());
    return result;
}

/**
 * @brief 認可後にサービスを追加する。
 * @param url サービス URL
 * @param alias エイリアス
 * @return 遅延返信値
 */
bool PackageManagerAdaptor::AddService(const QString &url, const QString &alias)
{
    return runPrivilegedBool(QStringLiteral("org.presire.qzypper.manage-repos"), [url, alias]() {
        return ZyppManager::instance().addService(url.toStdString(), alias.toStdString());
    });
}

/** @brief 認可後にサービスを削除する。 @param alias エイリアス @return 遅延返信値 */
bool PackageManagerAdaptor::RemoveService(const QString &alias)
{
    return runPrivilegedBool(QStringLiteral("org.presire.qzypper.manage-repos"), [alias]() {
        return ZyppManager::instance().removeService(alias.toStdString());
    });
}

/**
 * @brief 認可後にサービスのプロパティを変更する。
 * @param alias エイリアス
 * @param properties 変更後のプロパティ
 * @return 遅延返信値
 */
bool PackageManagerAdaptor::ModifyService(const QString &alias, const QVariantMap &properties)
{
    return runPrivilegedBool(QStringLiteral("org.presire.qzypper.manage-repos"), [alias, properties]() {
        return ZyppManager::instance().modifyService(alias.toStdString(), ServiceInfo::fromVariantMap(properties));
    });
}

/** @brief 認可後にサービスを更新する。 @param alias エイリアス @return 遅延返信値 */
bool PackageManagerAdaptor::RefreshService(const QString &alias)
{
    return runPrivilegedBool(QStringLiteral("org.presire.qzypper.manage-repos"), [alias]() {
        return ZyppManager::instance().refreshService(alias.toStdString());
    });
}

/**
 * @brief パッケージを検索する。
 * @param query 検索語
 * @param flags 検索フラグ
 * @return パッケージ情報のリスト
 */
QVariantList PackageManagerAdaptor::SearchPackages(const QString &query, int flags)
{
    if (!checkRead())
        return {};
    if (query.toUtf8().size() > 256) {
        sendErrorReply(QDBusError::InvalidArgs, QStringLiteral("Search query too long"));
        return {};
    }
    QVariantList result;
    for (const auto &pkg : ZyppManager::instance().searchPackages(query.toStdString(), flags)) {
        result.append(pkg.toVariantMap());
    }
    return result;
}

/** @brief パッケージ詳細を取得する
 *  @param name パッケージ名
 *  @return 詳細情報
 */
QVariantMap PackageManagerAdaptor::GetPackageDetails(const QString &name)
{
    if (!checkRead())
        return {};
    return ZyppManager::instance().getPackageDetails(name.toStdString()).toVariantMap();
}

/**
 * @brief リポジトリ別のパッケージを取得する。
 * @param repoAlias リポジトリエイリアス
 * @return パッケージ情報のリスト
 */
QVariantList PackageManagerAdaptor::GetPackagesByRepo(const QString &repoAlias)
{
    if (!checkRead())
        return {};
    QVariantList result;
    for (const auto &pkg : ZyppManager::instance().getPackagesByRepo(repoAlias.toStdString()))
        result.append(pkg.toVariantMap());
    return result;
}

/**
 * @brief 所有者のパッケージ選択状態を変更する。
 * @param name パッケージ名
 * @param status 選択状態
 * @return 変更成功時 true
 */
bool PackageManagerAdaptor::SetPackageStatus(const QString &name, int status)
{
    return runOwnerSync([name, status]() {
        return QVariant(ZyppManager::instance().setPackageStatus(name.toStdString(), status));
    }).toBool();
}

/**
 * @brief 所有者が使用するバージョンを選択する。
 * @param name パッケージ名
 * @param version バージョン
 * @param arch アーキテクチャ
 * @param repo リポジトリ
 * @return 選択成功時はtrue
 */
bool PackageManagerAdaptor::SetPackageVersion(const QString &name, const QString &version, const QString &arch, const QString &repo)
{
    return runOwnerSync([name, version, arch, repo]() {
        return QVariant(ZyppManager::instance().setPackageVersion(name.toStdString(), version.toStdString(),
                                                                arch.toStdString(), repo.toStdString()));
    }).toBool();
}

/** @brief パターン一覧を取得する。
 *  @return パターン情報のリスト
 */
QVariantList PackageManagerAdaptor::GetPatterns()
{
    if (!checkRead())
        return {};
    QVariantList result;
    for (const auto &pattern : ZyppManager::instance().getPatterns())
        result.append(pattern.toVariantMap());
    return result;
}

/**
 * @brief パターンに含まれるパッケージを取得する。
 * @param patternName パターン名
 * @return パッケージ情報のリスト
 */
QVariantList PackageManagerAdaptor::GetPackagesByPattern(const QString &patternName)
{
    if (!checkRead())
        return {};
    QVariantList result;
    for (const auto &pkg : ZyppManager::instance().getPackagesByPattern(patternName.toStdString()))
        result.append(pkg.toVariantMap());
    return result;
}

/**
 * @brief 所有者のパターン選択状態を変更する。
 * @param patternName パターン名
 * @param status 選択状態
 * @return 変更成功時はtrue
 */
bool PackageManagerAdaptor::SetPatternStatus(const QString &patternName, int status)
{
    return runOwnerSync([patternName, status]() {
        return QVariant(ZyppManager::instance().setPatternStatus(patternName.toStdString(), status));
    }).toBool();
}

/** @brief パッチ一覧を取得する
 *  @param category 分類名
 *  @return パッチ情報のリスト
 */
QVariantList PackageManagerAdaptor::GetPatches(int category)
{
    if (!checkRead())
        return {};
    QVariantList result;
    for (const auto &patch : ZyppManager::instance().getPatches(category))
        result.append(patch.toVariantMap());
    return result;
}

/** @brief 保留中の変更を取得する
 *  @return パッケージ情報のリスト
 */
QVariantList PackageManagerAdaptor::GetPendingChanges()
{
    if (!checkRead())
        return {};
    QVariantList result;
    for (const auto &pkg : ZyppManager::instance().getPendingChanges())
        result.append(pkg.toVariantMap());
    return result;
}

/** @brief 所有者の選択を全更新して解決済み改訂を記録する
 *  @return ソルバー結果
 */
QVariantMap PackageManagerAdaptor::UpdateAllPackages()
{
    return runOwnerSync([]() {
        const auto solverResult = ZyppManager::instance().updateAllPackages();
        QVariantMap result;
        result["success"] = solverResult.success;
        QVariantList problems;
        for (const auto &problem : solverResult.problems)
            problems.append(problem.toVariantMap());
        result["problems"] = problems;
        return QVariant(result);
    }, true, true).toMap();
}

/** @brief 所有者の選択の依存関係を解決する
 *  @return ソルバー結果
 */
QVariantMap PackageManagerAdaptor::ResolveDependencies()
{
    return runOwnerSync([]() {
        const auto solverResult = ZyppManager::instance().resolveDependencies();
        QVariantMap result;
        result["success"] = solverResult.success;
        QVariantList problems;
        for (const auto &problem : solverResult.problems)
            problems.append(problem.toVariantMap());
        result["problems"] = problems;
        return QVariant(result);
    }, true, true).toMap();
}

/**
 * @brief 所有者の選択に衝突解決策を適用する。
 * @param problemIndex 問題の添字
 * @param solutionIndex 解決策の添字
 * @return 適用成功時 true
 */
bool PackageManagerAdaptor::ApplySolution(int problemIndex, int solutionIndex)
{
    return runOwnerSync([problemIndex, solutionIndex]() {
        return QVariant(ZyppManager::instance().applySolution(problemIndex, solutionIndex));
    }).toBool();
}

/** @brief libzypp に触れず現在の選択改訂を返す
 *  @return 選択改訂番号
 */
qulonglong PackageManagerAdaptor::GetSelectionRevision()
{
    resetIdleTimer();
    return m_revision;
}

/**
 * @brief 確認・解決済みの改訂だけを認可後にコミットする。
 * @param expectedRevision クライアントが確認した改訂番号
 * @return 遅延返信で既存のコミット結果マップを返す
 */
QVariantMap PackageManagerAdaptor::Commit(qulonglong expectedRevision)
{
    if (!preparePrivileged())
        return {};
    if (expectedRevision != m_revision || m_resolvedRevision != m_revision) {
        sendErrorReply(QStringLiteral("org.presire.qzypper.Error.SelectionChanged"),
                       QStringLiteral("Package selection changed since it was confirmed; resolve and confirm again"));
        return {};
    }
    startPrivileged(QStringLiteral("org.presire.qzypper.install-packages"), [this]() {
        auto &mgr = ZyppManager::instance();
        const auto commitResult = mgr.commit(
            [this](const CommitProgressInfo &info) {
                const QString pkg = QString::fromStdString(info.packageName);
                const QString stage = QString::fromStdString(info.stage);
                const int pct = info.percentage;
                const int totalSteps = info.totalSteps;
                const int completedSteps = info.completedSteps;
                const int overallPct = info.overallPercentage;
                QMetaObject::invokeMethod(this, [this, pkg, pct, stage, totalSteps, completedSteps, overallPct]() {
                    emit CommitProgressChanged(pkg, pct, stage, totalSteps, completedSteps, overallPct);
                    emit ProgressChanged(pkg, pct, stage);
                }, Qt::QueuedConnection);
            },
            [this](const std::string &packageName, const std::string &event) {
                const QString pkg = QString::fromStdString(packageName);
                const QString evt = QString::fromStdString(event);
                QMetaObject::invokeMethod(this, [this, pkg, evt]() {
                    emit PackageStateChanged(pkg, evt);
                }, Qt::QueuedConnection);
            });

        const QString error = commitResult.success ? QString()
            : QString::fromStdString(commitResult.errorMessage.empty()
                                      ? mgr.lastError() : commitResult.errorMessage);
        QVariantMap result;
        result["success"] = commitResult.success;
        result["installed"] = commitResult.installed;
        result["updated"] = commitResult.updated;
        result["removed"] = commitResult.removed;

        QStringList installedPkgs, updatedPkgs, removedPkgs, failedPkgs;
        for (const auto &p : commitResult.installedPackages)
            installedPkgs.append(QString::fromStdString(p));
        for (const auto &p : commitResult.updatedPackages)
            updatedPkgs.append(QString::fromStdString(p));
        for (const auto &p : commitResult.removedPackages)
            removedPkgs.append(QString::fromStdString(p));
        for (const auto &p : commitResult.failedPackages)
            failedPkgs.append(QString::fromStdString(p));
        result["installedPackages"] = installedPkgs;
        result["updatedPackages"] = updatedPkgs;
        result["removedPackages"] = removedPkgs;
        result["failedPackages"] = failedPkgs;
        result["totalInstalledSize"] = QVariant::fromValue(static_cast<qulonglong>(commitResult.totalInstalledSize));
        result["totalDownloadSize"] = QVariant::fromValue(static_cast<qulonglong>(commitResult.totalDownloadSize));
        result["elapsedSeconds"] = commitResult.elapsedSeconds;
        if (!commitResult.success)
            result["errorMessage"] = error;

        AsyncOutcome outcome;
        outcome.value = result;
        outcome.transactionFinished = true;
        outcome.transactionSuccess = commitResult.success;
        outcome.transactionSummary = commitResult.success
            ? QString("Installed: %1, Updated: %2, Removed: %3")
                  .arg(commitResult.installed).arg(commitResult.updated).arg(commitResult.removed)
            : error;
        return outcome;
    });
    return {};
}

/** @brief ディスク使用量を取得する
 *  @return 使用量情報のリスト
 */
QVariantList PackageManagerAdaptor::GetDiskUsage()
{
    if (!checkRead())
        return {};
    QVariantList result;
    for (const auto &du : ZyppManager::instance().getDiskUsage())
        result.append(du.toVariantMap());
    return result;
}

/** @brief 所有者のみが処理中の操作をキャンセルできる。 */
void PackageManagerAdaptor::CancelOperation()
{
    if (checkOwner()) {
        if (m_cancelToken)
            m_cancelToken->store(true);
        ZyppManager::instance().cancelOperation();
    }
}

} // namespace qZypper
