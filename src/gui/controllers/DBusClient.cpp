#include <QDBusConnection>
#include <QDBusMetaType>
#include <QDBusError>
#include <QDBusMessage>
#include <QDBusArgument>
#include <QDBusPendingCall>
#include <QDBusPendingCallWatcher>
#include <QDebug>
#include <QRegularExpression>

#include <limits>

#include "DBusClient.h"

namespace qZypper {

namespace {
// ソルバー等の重い処理に備えた既定タイムアウト (120秒)
constexpr int kDefaultTimeout = 120000;
// バックエンドが遅延応答する非同期操作のためのタイムアウト無制限
constexpr int kNoTimeout = std::numeric_limits<int>::max();
} // namespace

static QVariant normalizeDBusValue(const QVariant &value);

/**
 * @brief QDBusArgument の現在要素を再帰的に QVariant へ変換する。
 *
 * Qt D-Bus は variant 内の配列/辞書 (aa{sv}, a{sv}, av 等) を QDBusArgument のまま
 * 返すため、入れ子の summary / problems / snapshots 等を QML から参照できるよう
 * QVariantMap / QVariantList へ展開する。
 * @param arg 読み出し元の QDBusArgument (現在位置の要素を 1 つ消費する)
 * @return 変換後の値
 */
static QVariant readDBusArgument(const QDBusArgument &arg)
{
    switch (arg.currentType()) {
    case QDBusArgument::MapType: {
        QVariantMap map;
        arg.beginMap();
        while (!arg.atEnd()) {
            arg.beginMapEntry();
            const QVariant key = normalizeDBusValue(arg.asVariant());
            const QVariant value = readDBusArgument(arg);
            arg.endMapEntry();
            map.insert(key.toString(), value);
        }
        arg.endMap();
        return map;
    }
    case QDBusArgument::ArrayType: {
        QVariantList list;
        arg.beginArray();
        while (!arg.atEnd())
            list.append(readDBusArgument(arg));
        arg.endArray();
        return list;
    }
    case QDBusArgument::StructureType: {
        QVariantList fields;
        arg.beginStructure();
        while (!arg.atEnd())
            fields.append(readDBusArgument(arg));
        arg.endStructure();
        return fields;
    }
    default:
        return normalizeDBusValue(arg.asVariant());
    }
}

/**
 * @brief QDBusVariant / QDBusArgument を含む値を再帰的に通常の QVariant へ正規化する。
 * @param value 変換対象
 * @return 正規化後の値
 */
static QVariant normalizeDBusValue(const QVariant &value)
{
    const int type = value.userType();
    if (type == qMetaTypeId<QDBusVariant>())
        return normalizeDBusValue(value.value<QDBusVariant>().variant());
    if (type == qMetaTypeId<QDBusArgument>())
        return readDBusArgument(value.value<QDBusArgument>());
    if (type == QMetaType::QVariantList) {
        QVariantList list = value.toList();
        for (QVariant &item : list)
            item = normalizeDBusValue(item);
        return list;
    }
    if (type == QMetaType::QVariantMap) {
        QVariantMap map = value.toMap();
        for (auto it = map.begin(); it != map.end(); ++it)
            it.value() = normalizeDBusValue(it.value());
        return map;
    }
    return value;
}

/**
 * @brief av / aa{sv} 形式の配列を QVariantList に変換する (入れ子も展開)。
 * @param variant 変換対象の引数
 * @return QVariantMap のリスト
 */
static QVariantList demarshallArrayOfMaps(const QVariant &variant)
{
    return normalizeDBusValue(variant).toList();
}

/**
 * @brief a{sv} 形式のマップを QVariantMap に変換する (入れ子も展開)。
 * @param variant 変換対象の引数
 * @return 変換後の QVariantMap
 */
static QVariantMap demarshallMap(const QVariant &variant)
{
    return normalizeDBusValue(variant).toMap();
}

/**
 * @brief D-Busエラーをユーザ向けメッセージへ変換する。
 * @param error D-Busエラー
 * @return 表示用メッセージ
 */
static QString describeError(const QDBusError &error)
{
    if (error.name() == QLatin1String("org.freedesktop.DBus.Error.AccessDenied")) {
        QString message = QStringLiteral("Authorization was denied");
        if (!error.message().isEmpty())
            message += QStringLiteral(": %1").arg(error.message());
        return message;
    }
    QString message = error.message();
    if (message.isEmpty())
        message = QStringLiteral("D-Bus call failed (error type: %1)").arg(error.name());
    return message;
}

/**
 * @brief コンストラクタ。バックエンドサービスの登録/解除を監視する。
 * @param parent 親オブジェクト
 */
DBusClient::DBusClient(QObject *parent)
    : QObject(parent)
{
    // InstallLocalRpms(ah, as) で記述子配列を送るため登録する
    qDBusRegisterMetaType<QList<QDBusUnixFileDescriptor>>();

    // サービスの登録/解除を監視
    m_watcher = new QDBusServiceWatcher(
        SERVICE_NAME,
        QDBusConnection::systemBus(),
        QDBusServiceWatcher::WatchForRegistration | QDBusServiceWatcher::WatchForUnregistration,
        this
    );

    connect(m_watcher, &QDBusServiceWatcher::serviceRegistered,
            this, &DBusClient::onServiceRegistered);
    connect(m_watcher, &QDBusServiceWatcher::serviceUnregistered,
            this, &DBusClient::onServiceUnregistered);
}

/**
 * @brief デストラクタ。D-Busインターフェースを解放する。
 */
DBusClient::~DBusClient()
{
    delete m_iface;
}

/**
 * @brief D-Busバックエンドサービスに接続する。
 *
 * システムバスに接続し、D-Busインターフェースを生成してシグナルを接続する。
 * @return 接続成功時: true
 */
bool DBusClient::connectToBackend()
{
    if (m_iface) {
        delete m_iface;
        m_iface = nullptr;
    }

    auto bus = QDBusConnection::systemBus();
    if (!bus.isConnected()) {
        m_lastError = "Cannot connect to system D-Bus";
        qWarning() << "DBusClient:" << m_lastError;
        return false;
    }

    m_iface = new QDBusInterface(
        SERVICE_NAME,
        OBJECT_PATH,
        INTERFACE,
        bus,
        this
    );

    if (!m_iface->isValid()) {
        m_lastError = QString("D-Bus interface not valid: %1").arg(bus.lastError().message());
        qWarning() << "DBusClient:" << m_lastError;
        delete m_iface;
        m_iface = nullptr;
        return false;
    }

    // ソルバー等の重い処理に備えてタイムアウトを120秒に延長
    m_iface->setTimeout(kDefaultTimeout);

    // 再接続で重複配送しないよう、既存のbus接続を一旦外す
    bus.disconnect(SERVICE_NAME, OBJECT_PATH, INTERFACE, "RepoRefreshProgress",
                   this, SIGNAL(repoRefreshProgress(QString, int)));
    bus.disconnect(SERVICE_NAME, OBJECT_PATH, INTERFACE, "ProgressChanged",
                   this, SIGNAL(progressChanged(QString, int, QString)));
    bus.disconnect(SERVICE_NAME, OBJECT_PATH, INTERFACE, "CommitProgressChanged",
                   this, SIGNAL(commitProgressChanged(QString, int, QString, int, int, int)));
    bus.disconnect(SERVICE_NAME, OBJECT_PATH, INTERFACE, "TransactionFinished",
                   this, SIGNAL(transactionFinished(bool, QString)));
    bus.disconnect(SERVICE_NAME, OBJECT_PATH, INTERFACE, "ErrorOccurred",
                   this, SIGNAL(errorOccurred(QString)));
    bus.disconnect(SERVICE_NAME, OBJECT_PATH, INTERFACE, "PackageStateChanged",
                   this, SIGNAL(packageStateChanged(QString, QString)));
    bus.disconnect(SERVICE_NAME, OBJECT_PATH, INTERFACE, "UntrustedKeyDetected",
                   this, SLOT(onUntrustedKeyDetected(QVariantMap)));
    bus.disconnect(SERVICE_NAME, OBJECT_PATH, INTERFACE, "OperationStateChanged",
                   this, SLOT(onOperationStateChanged(QVariantMap)));
    bus.disconnect(SERVICE_NAME, OBJECT_PATH, INTERFACE, "PromptRequested",
                   this, SLOT(onPromptRequested(uint, QString, QVariantMap)));
    bus.disconnect(SERVICE_NAME, OBJECT_PATH, INTERFACE, "PromptClosed",
                   this, SIGNAL(promptClosed(uint)));
    bus.disconnect(SERVICE_NAME, OBJECT_PATH, INTERFACE, "PoolReloaded",
                   this, SIGNAL(poolReloaded()));
    bus.disconnect(SERVICE_NAME, OBJECT_PATH, INTERFACE, "ScriptOutput",
                   this, SLOT(onScriptOutput(QVariantMap)));

    // バックエンドのブロードキャストシグナルをGUI側シグナルへ中継
    bus.connect(SERVICE_NAME, OBJECT_PATH, INTERFACE, "RepoRefreshProgress",
                this, SIGNAL(repoRefreshProgress(QString, int)));
    bus.connect(SERVICE_NAME, OBJECT_PATH, INTERFACE, "ProgressChanged",
                this, SIGNAL(progressChanged(QString, int, QString)));
    bus.connect(SERVICE_NAME, OBJECT_PATH, INTERFACE, "CommitProgressChanged",
                this, SIGNAL(commitProgressChanged(QString, int, QString, int, int, int)));
    bus.connect(SERVICE_NAME, OBJECT_PATH, INTERFACE, "TransactionFinished",
                this, SIGNAL(transactionFinished(bool, QString)));
    bus.connect(SERVICE_NAME, OBJECT_PATH, INTERFACE, "ErrorOccurred",
                this, SIGNAL(errorOccurred(QString)));
    bus.connect(SERVICE_NAME, OBJECT_PATH, INTERFACE, "PackageStateChanged",
                this, SIGNAL(packageStateChanged(QString, QString)));
    bus.connect(SERVICE_NAME, OBJECT_PATH, INTERFACE, "UntrustedKeyDetected",
                this, SLOT(onUntrustedKeyDetected(QVariantMap)));
    bus.connect(SERVICE_NAME, OBJECT_PATH, INTERFACE, "OperationStateChanged",
                this, SLOT(onOperationStateChanged(QVariantMap)));
    bus.connect(SERVICE_NAME, OBJECT_PATH, INTERFACE, "PromptRequested",
                this, SLOT(onPromptRequested(uint, QString, QVariantMap)));
    bus.connect(SERVICE_NAME, OBJECT_PATH, INTERFACE, "PromptClosed",
                this, SIGNAL(promptClosed(uint)));
    bus.connect(SERVICE_NAME, OBJECT_PATH, INTERFACE, "PoolReloaded",
                this, SIGNAL(poolReloaded()));
    bus.connect(SERVICE_NAME, OBJECT_PATH, INTERFACE, "ScriptOutput",
                this, SLOT(onScriptOutput(QVariantMap)));

    m_connected = true;
    qDebug() << "DBusClient: connected to backend";
    emit backendConnected();
    return true;
}

/**
 * @brief バックエンドとの接続状態を返す。
 * @return 接続済みかつインターフェースが有効な場合: true
 */
bool DBusClient::isConnected() const
{
    return m_connected && m_iface && m_iface->isValid();
}

/**
 * @brief バックエンドサービス登録時の処理。
 *
 * 自分の初期化中は再入を避けるためスキップする。
 * 再接続後は非同期で初期化し、成功時に backendReconnected を発行する。
 * @param serviceName 登録されたサービス名
 */
void DBusClient::onServiceRegistered(const QString &serviceName)
{
    Q_UNUSED(serviceName);
    if (m_initializing) {
        return;
    }
    if (connectToBackend()) {
        initializeBackend([this](bool success, const QString &, const QString &, const QVariantList &) {
            if (success)
                emit backendReconnected();
        });
    }
}

/**
 * @brief バックエンドサービス解除時の処理。
 * @param serviceName 解除されたサービス名
 */
void DBusClient::onServiceUnregistered(const QString &serviceName)
{
    Q_UNUSED(serviceName);
    m_connected = false;
    // 消滅したバックエンド宛ての呼び出しは応答が来ないため破棄する
    resetPendingCalls();
    emit backendDisconnected();
}

/**
 * @brief 待機中の呼び出しを破棄し、旧バックエンドの応答を無効化する。
 *
 * 世代番号を進めることで、既に送信済みで応答待ちの呼び出しのコールバックも破棄対象となる。
 */
void DBusClient::resetPendingCalls()
{
    ++m_generation;
    m_callQueue.clear();
    m_serialCallInFlight = false;
    // 破棄した Initialize の応答では解除されないため、再接続時の初期化を妨げないよう解除する
    m_initializing = false;
}

// -- 非同期呼び出し共通処理 --

/**
 * @brief D-Busメソッドを非同期で呼び出し、完了時にコールバックへ結果を渡す。
 *
 * QDBusPendingCallWatcher を用いるためGUIスレッドをブロックしない。
 * エラー時は m_lastError を更新し、コールバックへエラー名/メッセージを返す。
 * @param msg 送信するメソッド呼び出しメッセージ
 * @param timeout タイムアウト (ミリ秒)
 * @param callback 完了時に呼ばれるコールバック
 */
void DBusClient::callAsync(const QDBusMessage &msg, int timeout, ReplyCallback callback)
{
    // バックエンドは処理中に別操作を Busy で拒否する。取消し・プロンプト応答・状態照会は
    // 処理中でも受け付けられるため直列化せず即時送信し、それ以外は1件ずつ順番に送る。
    static const QStringList kConcurrentMembers = {
        QStringLiteral("CancelOperation"), QStringLiteral("AnswerPrompt"),
        QStringLiteral("GetOperationState"), QStringLiteral("GetSelectionRevision"),
    };
    if (kConcurrentMembers.contains(msg.member())) {
        dispatchCall(msg, timeout, std::move(callback), false);
        return;
    }
    if (m_serialCallInFlight || !m_callQueue.empty()) {
        m_callQueue.push_back({msg, timeout, std::move(callback)});
        return;
    }
    dispatchCall(msg, timeout, std::move(callback), true);
}

void DBusClient::dispatchNextQueuedCall()
{
    if (m_serialCallInFlight || m_callQueue.empty())
        return;
    QueuedCall next = std::move(m_callQueue.front());
    m_callQueue.pop_front();
    dispatchCall(next.msg, next.timeout, std::move(next.callback), true);
}

void DBusClient::dispatchCall(const QDBusMessage &msg, int timeout, ReplyCallback callback,
                              bool serialized)
{
    if (!isConnected()) {
        m_lastError = QStringLiteral("Not connected to backend");
        qWarning() << "DBusClient:" << m_lastError;
        emit lastErrorChanged();
        if (callback)
            callback(false, QStringLiteral("org.presire.qzypper.Error.NotConnected"), m_lastError, {});
        if (serialized)
            dispatchNextQueuedCall();
        return;
    }

    if (serialized)
        m_serialCallInFlight = true;

    const QString member = msg.member();
    const quint64 generation = m_generation;
    QDBusPendingCall pending = QDBusConnection::systemBus().asyncCall(msg, timeout);
    auto *watcher = new QDBusPendingCallWatcher(pending, this);

    connect(watcher, &QDBusPendingCallWatcher::finished, this,
            [this, callback, member, serialized, generation](QDBusPendingCallWatcher *w) {
        const QDBusMessage reply = w->reply();
        w->deleteLater();

        // バックエンド再接続後の古い応答は状態を壊すため破棄する
        if (generation != m_generation)
            return;

        // コールバック内で新たな呼び出しが積まれても順序を保つよう、先に完了扱いにする
        if (serialized)
            m_serialCallInFlight = false;

        if (reply.type() == QDBusMessage::ErrorMessage) {
            const QDBusError error(reply);
            m_lastError = describeError(error);
            qWarning() << "DBusClient:" << member << m_lastError;
            emit lastErrorChanged();
            if (error.name() == QLatin1String("org.presire.qzypper.Error.ZyppLocked")) {
                static const QRegularExpression lockedRe(
                    QStringLiteral("locked by (.+) \\(PID (\\d+)\\)"));
                const QRegularExpressionMatch m = lockedRe.match(error.message());
                const QString lockerName = m.hasMatch() ? m.captured(1) : QString();
                const int lockerPid = m.hasMatch() ? m.captured(2).toInt() : 0;
                emit packageLocked(lockerName, lockerPid, error.message());
            }
            if (callback)
                callback(false, error.name(), m_lastError, {});
        } else if (callback) {
            callback(true, QString(), QString(), reply.arguments());
        }

        if (serialized)
            dispatchNextQueuedCall();
    });
}

// -- 初期化 --

/**
 * @brief バックエンドを非同期で初期化する。
 *
 * Polkit認証を含むためタイムアウト無制限で呼び出す。バックエンドは必ず応答し、
 * プロセスが終了した場合はバスがエラーを返す。完了時にコールバックへ
 * 成功可否とエラー情報を渡す。
 * @param callback 完了時に呼ばれるコールバック
 */
void DBusClient::initializeBackend(ReplyCallback callback)
{
    if (m_initializing) {
        m_lastError = QStringLiteral("Initialization already in progress");
        emit lastErrorChanged();
        if (callback)
            callback(false, QStringLiteral("org.presire.qzypper.Error.Busy"), m_lastError, {});
        return;
    }
    if (!isConnected()) {
        m_lastError = QStringLiteral("Not connected to backend");
        emit lastErrorChanged();
        if (callback)
            callback(false, QStringLiteral("org.presire.qzypper.Error.NotConnected"), m_lastError, {});
        return;
    }

    m_initializing = true;
    QDBusMessage msg = QDBusMessage::createMethodCall(SERVICE_NAME, OBJECT_PATH, INTERFACE, "Initialize");
    callAsync(msg, kNoTimeout, [this, callback](bool success, const QString &errorName,
                                                const QString &errorMessage, const QVariantList &args) {
        m_initializing = false;
        const bool value = success && (args.isEmpty() || args.at(0).toBool());
        if (callback)
            callback(value, errorName, errorMessage, args);
    });
}

// -- リポジトリ操作 --

/**
 * @brief 全リポジトリのリフレッシュを非同期で要求する。
 * @param callback 完了時に呼ばれるコールバック
 */
void DBusClient::refreshReposAsync(ReplyCallback callback)
{
    QDBusMessage msg = QDBusMessage::createMethodCall(SERVICE_NAME, OBJECT_PATH, INTERFACE, "RefreshRepos");
    callAsync(msg, kNoTimeout, [callback](bool ok, const QString &name,
                                          const QString &message, const QVariantList &args) {
        if (callback)
            callback(ok, name, message,
                     ok && !args.isEmpty() ? QVariantList{ args.at(0).toBool() } : QVariantList{});
    });
}

/**
 * @brief 指定リポジトリのリフレッシュを非同期で要求する。
 * @param alias リフレッシュ対象のエイリアス
 * @param callback 完了時に呼ばれるコールバック
 */
void DBusClient::refreshSingleRepoAsync(const QString &alias, ReplyCallback callback)
{
    QDBusMessage msg = QDBusMessage::createMethodCall(SERVICE_NAME, OBJECT_PATH, INTERFACE, "RefreshSingleRepo");
    msg << alias;
    callAsync(msg, kNoTimeout, [callback](bool ok, const QString &name,
                                          const QString &message, const QVariantList &args) {
        if (callback)
            callback(ok, name, message,
                     ok && !args.isEmpty() ? QVariantList{ args.at(0).toBool() } : QVariantList{});
    });
}

/**
 * @brief 実行中の操作のキャンセルを要求する (fire-and-forget)。
 */
void DBusClient::cancelOperation()
{
    if (!isConnected())
        return;
    QDBusMessage msg = QDBusMessage::createMethodCall(SERVICE_NAME, OBJECT_PATH, INTERFACE, "CancelOperation");
    QDBusConnection::systemBus().send(msg);
}

/**
 * @brief リポジトリを追加する。
 * @param url リポジトリURL
 * @param name リポジトリ名
 * @param callback 完了時に呼ばれるコールバック (args[0] に QVariantMap)
 */
void DBusClient::addRepo(const QString &url, const QString &name, ReplyCallback callback)
{
    QDBusMessage msg = QDBusMessage::createMethodCall(SERVICE_NAME, OBJECT_PATH, INTERFACE, "AddRepo");
    msg << url << name;
    callAsync(msg, kNoTimeout, [callback](bool ok, const QString &name2,
                                          const QString &message, const QVariantList &args) {
        if (callback)
            callback(ok, name2, message,
                     ok && !args.isEmpty() ? QVariantList{ demarshallMap(args.at(0)) } : QVariantList{});
    });
}

/**
 * @brief 全属性を指定してリポジトリを追加する。
 * @param properties リポジトリ属性マップ
 * @param callback 完了時に呼ばれるコールバック (args[0] に QVariantMap)
 */
void DBusClient::addRepoFull(const QVariantMap &properties, ReplyCallback callback)
{
    QDBusMessage msg = QDBusMessage::createMethodCall(SERVICE_NAME, OBJECT_PATH, INTERFACE, "AddRepoFull");
    msg << properties;
    callAsync(msg, kNoTimeout, [callback](bool ok, const QString &name,
                                          const QString &message, const QVariantList &args) {
        if (callback)
            callback(ok, name, message,
                     ok && !args.isEmpty() ? QVariantList{ demarshallMap(args.at(0)) } : QVariantList{});
    });
}

/**
 * @brief リポジトリを削除する。
 * @param alias 削除対象のエイリアス
 * @param callback 完了時に呼ばれるコールバック
 */
void DBusClient::removeRepo(const QString &alias, ReplyCallback callback)
{
    QDBusMessage msg = QDBusMessage::createMethodCall(SERVICE_NAME, OBJECT_PATH, INTERFACE, "RemoveRepo");
    msg << alias;
    callAsync(msg, kNoTimeout, [callback](bool ok, const QString &name,
                                          const QString &message, const QVariantList &args) {
        if (callback)
            callback(ok, name, message,
                     ok && !args.isEmpty() ? QVariantList{ args.at(0).toBool() } : QVariantList{});
    });
}

/**
 * @brief リポジトリの有効/無効を切り替える。
 * @param alias 対象エイリアス
 * @param enabled 有効にする場合 true
 * @param callback 完了時に呼ばれるコールバック
 */
void DBusClient::setRepoEnabled(const QString &alias, bool enabled, ReplyCallback callback)
{
    QDBusMessage msg = QDBusMessage::createMethodCall(SERVICE_NAME, OBJECT_PATH, INTERFACE, "SetRepoEnabled");
    msg << alias << enabled;
    callAsync(msg, kNoTimeout, [callback](bool ok, const QString &name,
                                          const QString &message, const QVariantList &args) {
        if (callback)
            callback(ok, name, message,
                     ok && !args.isEmpty() ? QVariantList{ args.at(0).toBool() } : QVariantList{});
    });
}

/**
 * @brief リポジトリ属性を変更する。
 * @param alias 対象エイリアス
 * @param properties 変更する属性マップ
 * @param callback 完了時に呼ばれるコールバック
 */
void DBusClient::modifyRepo(const QString &alias, const QVariantMap &properties, ReplyCallback callback)
{
    QDBusMessage msg = QDBusMessage::createMethodCall(SERVICE_NAME, OBJECT_PATH, INTERFACE, "ModifyRepo");
    msg << alias << properties;
    callAsync(msg, kNoTimeout, [callback](bool ok, const QString &name,
                                          const QString &message, const QVariantList &args) {
        if (callback)
            callback(ok, name, message,
                     ok && !args.isEmpty() ? QVariantList{ args.at(0).toBool() } : QVariantList{});
    });
}

// -- サービス操作 --

/**
 * @brief サービスを追加する。
 * @param url サービスURL
 * @param alias サービスエイリアス
 * @param callback 完了時に呼ばれるコールバック
 */
void DBusClient::addService(const QString &url, const QString &alias, ReplyCallback callback)
{
    QDBusMessage msg = QDBusMessage::createMethodCall(SERVICE_NAME, OBJECT_PATH, INTERFACE, "AddService");
    msg << url << alias;
    callAsync(msg, kNoTimeout, [callback](bool ok, const QString &name,
                                          const QString &message, const QVariantList &args) {
        if (callback)
            callback(ok, name, message,
                     ok && !args.isEmpty() ? QVariantList{ args.at(0).toBool() } : QVariantList{});
    });
}

/**
 * @brief サービスを削除する。
 * @param alias 削除対象エイリアス
 * @param callback 完了時に呼ばれるコールバック
 */
void DBusClient::removeService(const QString &alias, ReplyCallback callback)
{
    QDBusMessage msg = QDBusMessage::createMethodCall(SERVICE_NAME, OBJECT_PATH, INTERFACE, "RemoveService");
    msg << alias;
    callAsync(msg, kNoTimeout, [callback](bool ok, const QString &name,
                                          const QString &message, const QVariantList &args) {
        if (callback)
            callback(ok, name, message,
                     ok && !args.isEmpty() ? QVariantList{ args.at(0).toBool() } : QVariantList{});
    });
}

/**
 * @brief サービス属性を変更する。
 * @param alias 対象エイリアス
 * @param properties 変更する属性マップ
 * @param callback 完了時に呼ばれるコールバック
 */
void DBusClient::modifyService(const QString &alias, const QVariantMap &properties, ReplyCallback callback)
{
    QDBusMessage msg = QDBusMessage::createMethodCall(SERVICE_NAME, OBJECT_PATH, INTERFACE, "ModifyService");
    msg << alias << properties;
    callAsync(msg, kNoTimeout, [callback](bool ok, const QString &name,
                                          const QString &message, const QVariantList &args) {
        if (callback)
            callback(ok, name, message,
                     ok && !args.isEmpty() ? QVariantList{ args.at(0).toBool() } : QVariantList{});
    });
}

/**
 * @brief サービスのリフレッシュを非同期で要求する。
 * @param alias 対象エイリアス
 * @param callback 完了時に呼ばれるコールバック
 */
void DBusClient::refreshServiceAsync(const QString &alias, ReplyCallback callback)
{
    QDBusMessage msg = QDBusMessage::createMethodCall(SERVICE_NAME, OBJECT_PATH, INTERFACE, "RefreshService");
    msg << alias;
    callAsync(msg, kNoTimeout, [callback](bool ok, const QString &name,
                                          const QString &message, const QVariantList &args) {
        if (callback)
            callback(ok, name, message,
                     ok && !args.isEmpty() ? QVariantList{ args.at(0).toBool() } : QVariantList{});
    });
}

// -- パッケージ状態変更 --

/**
 * @brief パッケージの状態を変更する。
 * @param name パッケージ名
 * @param status 新しいステータス
 * @param callback 完了時に呼ばれるコールバック
 */
void DBusClient::setPackageStatus(const QString &name, int status, ReplyCallback callback)
{
    QDBusMessage msg = QDBusMessage::createMethodCall(SERVICE_NAME, OBJECT_PATH, INTERFACE, "SetPackageStatus");
    msg << name << status;
    callAsync(msg, kDefaultTimeout, [callback](bool ok, const QString &errorName,
                                               const QString &message, const QVariantList &args) {
        if (callback)
            callback(ok, errorName, message,
                     ok && !args.isEmpty() ? QVariantList{ args.at(0).toBool() } : QVariantList{});
    });
}

/**
 * @brief パッケージのバージョンを変更する。
 * @param name パッケージ名
 * @param version バージョン
 * @param arch アーキテクチャ
 * @param repoAlias リポジトリエイリアス
 * @param callback 完了時に呼ばれるコールバック
 */
void DBusClient::setPackageVersion(const QString &name, const QString &version,
                                   const QString &arch, const QString &repoAlias,
                                   ReplyCallback callback)
{
    QDBusMessage msg = QDBusMessage::createMethodCall(SERVICE_NAME, OBJECT_PATH, INTERFACE, "SetPackageVersion");
    msg << name << version << arch << repoAlias;
    callAsync(msg, kDefaultTimeout, [callback](bool ok, const QString &errorName,
                                               const QString &message, const QVariantList &args) {
        if (callback)
            callback(ok, errorName, message,
                     ok && !args.isEmpty() ? QVariantList{ args.at(0).toBool() } : QVariantList{});
    });
}

/**
 * @brief パターンの状態を変更する。
 * @param name パターン名
 * @param status 新しいステータス
 * @param callback 完了時に呼ばれるコールバック
 */
void DBusClient::setPatternStatus(const QString &name, int status, ReplyCallback callback)
{
    QDBusMessage msg = QDBusMessage::createMethodCall(SERVICE_NAME, OBJECT_PATH, INTERFACE, "SetPatternStatus");
    msg << name << status;
    callAsync(msg, kDefaultTimeout, [callback](bool ok, const QString &errorName,
                                               const QString &message, const QVariantList &args) {
        if (callback)
            callback(ok, errorName, message,
                     ok && !args.isEmpty() ? QVariantList{ args.at(0).toBool() } : QVariantList{});
    });
}

/**
 * @brief パッチの適用状態を変更する。
 * @param patchId パッチID
 * @param status 新しいステータス (Install=1 / NoInst=0)
 * @param callback 完了時に呼ばれるコールバック (args[0] に bool)
 */
void DBusClient::setPatchStatus(const QString &patchId, int status, ReplyCallback callback)
{
    QDBusMessage msg = QDBusMessage::createMethodCall(SERVICE_NAME, OBJECT_PATH, INTERFACE, "SetPatchStatus");
    msg << patchId << status;
    callAsync(msg, kDefaultTimeout, [callback](bool ok, const QString &errorName,
                                               const QString &message, const QVariantList &args) {
        if (callback)
            callback(ok, errorName, message,
                     ok && !args.isEmpty() ? QVariantList{ args.at(0).toBool() } : QVariantList{});
    });
}

/**
 * @brief 必要な全パッチを選択する。
 * @param options オプションマップ (securityOnly / includeOptional / skipInteractive)
 * @param callback 完了時に呼ばれるコールバック (args[0] に QVariantMap)
 */
void DBusClient::selectAllNeededPatches(const QVariantMap &options, ReplyCallback callback)
{
    QDBusMessage msg = QDBusMessage::createMethodCall(SERVICE_NAME, OBJECT_PATH, INTERFACE, "SelectAllNeededPatches");
    msg << options;
    callAsync(msg, kNoTimeout, [callback](bool ok, const QString &name,
                                          const QString &message, const QVariantList &args) {
        if (callback)
            callback(ok, name, message,
                     ok && !args.isEmpty() ? QVariantList{ demarshallMap(args.at(0)) } : QVariantList{});
    });
}

// -- 依存関係解決 --

/**
 * @brief 全パッケージの更新を実行する。
 * @param callback 完了時に呼ばれるコールバック (args[0] に QVariantMap)
 */
void DBusClient::updateAllPackages(ReplyCallback callback)
{
    QDBusMessage msg = QDBusMessage::createMethodCall(SERVICE_NAME, OBJECT_PATH, INTERFACE, "UpdateAllPackages");
    callAsync(msg, kDefaultTimeout, [callback](bool ok, const QString &name,
                                               const QString &message, const QVariantList &args) {
        if (callback)
            callback(ok, name, message,
                     ok && !args.isEmpty() ? QVariantList{ demarshallMap(args.at(0)) } : QVariantList{});
    });
}

/**
 * @brief ディストリビューション更新 (dist-upgrade) を実行する。
 * @param options オプションマップ (allowVendorChange)
 * @param callback 完了時に呼ばれるコールバック (args[0] に QVariantMap)
 */
void DBusClient::distUpgrade(const QVariantMap &options, ReplyCallback callback)
{
    QDBusMessage msg = QDBusMessage::createMethodCall(SERVICE_NAME, OBJECT_PATH, INTERFACE, "DistUpgrade");
    msg << options;
    callAsync(msg, kNoTimeout, [callback](bool ok, const QString &name,
                                          const QString &message, const QVariantList &args) {
        if (callback)
            callback(ok, name, message,
                     ok && !args.isEmpty() ? QVariantList{ demarshallMap(args.at(0)) } : QVariantList{});
    });
}

/**
 * @brief システムの整合性を検証する (VerifySystem)。
 * @param callback 完了時に呼ばれるコールバック (args[0] に QVariantMap)
 */
void DBusClient::verifySystem(ReplyCallback callback)
{
    QDBusMessage msg = QDBusMessage::createMethodCall(SERVICE_NAME, OBJECT_PATH, INTERFACE, "VerifySystem");
    callAsync(msg, kNoTimeout, [callback](bool ok, const QString &name,
                                          const QString &message, const QVariantList &args) {
        if (callback)
            callback(ok, name, message,
                     ok && !args.isEmpty() ? QVariantList{ demarshallMap(args.at(0)) } : QVariantList{});
    });
}

/**
 * @brief 現在の解決済みトランザクションの概要を取得する。
 * @param callback 完了時に呼ばれるコールバック (args[0] に QVariantMap)
 */
void DBusClient::getTransactionSummary(ReplyCallback callback)
{
    QDBusMessage msg = QDBusMessage::createMethodCall(SERVICE_NAME, OBJECT_PATH, INTERFACE, "GetTransactionSummary");
    callAsync(msg, kDefaultTimeout, [callback](bool ok, const QString &name,
                                               const QString &message, const QVariantList &args) {
        if (callback)
            callback(ok, name, message,
                     ok && !args.isEmpty() ? QVariantList{ demarshallMap(args.at(0)) } : QVariantList{});
    });
}

/**
 * @brief 依存関係を解決する。
 * @param callback 完了時に呼ばれるコールバック (args[0] に QVariantMap)
 */
void DBusClient::resolveDependencies(ReplyCallback callback)
{
    QDBusMessage msg = QDBusMessage::createMethodCall(SERVICE_NAME, OBJECT_PATH, INTERFACE, "ResolveDependencies");
    callAsync(msg, kDefaultTimeout, [callback](bool ok, const QString &name,
                                               const QString &message, const QVariantList &args) {
        if (callback)
            callback(ok, name, message,
                     ok && !args.isEmpty() ? QVariantList{ demarshallMap(args.at(0)) } : QVariantList{});
    });
}

/**
 * @brief 選択された解決策を適用する。
 * @param problemIndex 問題インデックス
 * @param solutionIndex 解決策インデックス
 * @param callback 完了時に呼ばれるコールバック
 */
void DBusClient::applySolution(int problemIndex, int solutionIndex, ReplyCallback callback)
{
    QDBusMessage msg = QDBusMessage::createMethodCall(SERVICE_NAME, OBJECT_PATH, INTERFACE, "ApplySolution");
    msg << problemIndex << solutionIndex;
    callAsync(msg, kDefaultTimeout, [callback](bool ok, const QString &errorName,
                                               const QString &message, const QVariantList &args) {
        if (callback)
            callback(ok, errorName, message,
                     ok && !args.isEmpty() ? QVariantList{ args.at(0).toBool() } : QVariantList{});
    });
}

// -- コミット --

/**
 * @brief 現在の選択リビジョンを取得する。
 * @param callback 完了時に呼ばれるコールバック (args[0] に quint64)
 */
void DBusClient::getSelectionRevision(ReplyCallback callback)
{
    QDBusMessage msg = QDBusMessage::createMethodCall(SERVICE_NAME, OBJECT_PATH, INTERFACE, "GetSelectionRevision");
    callAsync(msg, kDefaultTimeout, [callback](bool ok, const QString &name,
                                               const QString &message, const QVariantList &args) {
        if (callback)
            callback(ok, name, message,
                     ok && !args.isEmpty() ? QVariantList{ args.at(0) } : QVariantList{});
    });
}

/**
 * @brief 選択状態を非同期でコミットする。
 * @param expectedRevision コミット対象の選択リビジョン
 * @param callback 完了時に呼ばれるコールバック (args[0] に QVariantMap)
 */
void DBusClient::commitAsync(quint64 expectedRevision, ReplyCallback callback)
{
    QDBusMessage msg = QDBusMessage::createMethodCall(SERVICE_NAME, OBJECT_PATH, INTERFACE, "Commit");
    msg << expectedRevision;
    callAsync(msg, kNoTimeout, [callback](bool ok, const QString &name,
                                          const QString &message, const QVariantList &args) {
        if (callback)
            callback(ok, name, message,
                     ok && !args.isEmpty() ? QVariantList{ demarshallMap(args.at(0)) } : QVariantList{});
    });
}

// -- 署名鍵 --

/**
 * @brief 署名鍵を承認する。
 * @param fingerprint 鍵のフィンガープリント
 * @param callback 完了時に呼ばれるコールバック
 */
void DBusClient::trustKey(const QString &fingerprint, ReplyCallback callback)
{
    QDBusMessage msg = QDBusMessage::createMethodCall(SERVICE_NAME, OBJECT_PATH, INTERFACE, "TrustKey");
    msg << fingerprint;
    callAsync(msg, kNoTimeout, [callback, this](bool ok, const QString &errorName,
                                                const QString &message, const QVariantList &args) {
        // 元実装はエラーメッセージが空の場合エラー名を表示していた
        QString effective = message;
        if (!ok && effective.isEmpty())
            effective = m_lastError;
        if (callback)
            callback(ok, errorName, effective,
                     ok && !args.isEmpty() ? QVariantList{ args.at(0).toBool() } : QVariantList{});
    });
}

// -- 読み取り操作 --

/**
 * @brief リポジトリ一覧を取得する。
 * @param callback 完了時に呼ばれるコールバック (args[0] に QVariantList)
 */
void DBusClient::getRepos(ReplyCallback callback)
{
    QDBusMessage msg = QDBusMessage::createMethodCall(SERVICE_NAME, OBJECT_PATH, INTERFACE, "GetRepos");
    callAsync(msg, kDefaultTimeout, [callback](bool ok, const QString &name,
                                               const QString &message, const QVariantList &args) {
        if (callback)
            callback(ok, name, message,
                     ok && !args.isEmpty() ? QVariantList{ demarshallArrayOfMaps(args.at(0)) } : QVariantList{});
    });
}

/**
 * @brief パッケージを検索する。
 * @param query 検索クエリ
 * @param flags 検索フラグ
 * @param callback 完了時に呼ばれるコールバック (args[0] に QVariantList)
 */
void DBusClient::searchPackages(const QString &query, int flags, ReplyCallback callback)
{
    QDBusMessage msg = QDBusMessage::createMethodCall(SERVICE_NAME, OBJECT_PATH, INTERFACE, "SearchPackages");
    msg << query << flags;
    callAsync(msg, kDefaultTimeout, [callback](bool ok, const QString &name,
                                               const QString &message, const QVariantList &args) {
        if (callback)
            callback(ok, name, message,
                     ok && !args.isEmpty() ? QVariantList{ demarshallArrayOfMaps(args.at(0)) } : QVariantList{});
    });
}

/**
 * @brief 高度なパッケージ検索を実行する (マッチ方式・インストール済み限定・ページング対応)。
 * @param options オプションマップ (query / flags / match / offset / limit / installedOnly / repoAlias)
 * @param callback 完了時に呼ばれるコールバック (args[0] に QVariantList)
 */
void DBusClient::searchPackagesEx(const QVariantMap &options, ReplyCallback callback)
{
    QDBusMessage msg = QDBusMessage::createMethodCall(SERVICE_NAME, OBJECT_PATH, INTERFACE, "SearchPackagesEx");
    msg << options;
    callAsync(msg, kDefaultTimeout, [callback](bool ok, const QString &name,
                                               const QString &message, const QVariantList &args) {
        if (callback)
            callback(ok, name, message,
                     ok && !args.isEmpty() ? QVariantList{ demarshallArrayOfMaps(args.at(0)) } : QVariantList{});
    });
}

/**
 * @brief 分類別のパッケージ一覧を取得する。
 * @param classification 分類名 (recommended / orphaned / unneeded 等)
 * @param callback 完了時に呼ばれるコールバック (args[0] に QVariantList)
 */
void DBusClient::getPackagesByClassification(const QString &classification, ReplyCallback callback)
{
    QDBusMessage msg = QDBusMessage::createMethodCall(SERVICE_NAME, OBJECT_PATH, INTERFACE, "GetPackagesByClassification");
    msg << classification;
    callAsync(msg, kDefaultTimeout, [callback](bool ok, const QString &name,
                                               const QString &message, const QVariantList &args) {
        if (callback)
            callback(ok, name, message,
                     ok && !args.isEmpty() ? QVariantList{ demarshallArrayOfMaps(args.at(0)) } : QVariantList{});
    });
}

/**
 * @brief パッケージ詳細を取得する。
 * @param name パッケージ名
 * @param callback 完了時に呼ばれるコールバック (args[0] に QVariantMap)
 */
void DBusClient::getPackageDetails(const QString &name, ReplyCallback callback)
{
    QDBusMessage msg = QDBusMessage::createMethodCall(SERVICE_NAME, OBJECT_PATH, INTERFACE, "GetPackageDetails");
    msg << name;
    callAsync(msg, kDefaultTimeout, [callback](bool ok, const QString &errorName,
                                               const QString &message, const QVariantList &args) {
        if (callback)
            callback(ok, errorName, message,
                     ok && !args.isEmpty() ? QVariantList{ demarshallMap(args.at(0)) } : QVariantList{});
    });
}

/**
 * @brief 指定リポジトリのパッケージ一覧を取得する。
 * @param repoAlias リポジトリエイリアス
 * @param callback 完了時に呼ばれるコールバック (args[0] に QVariantList)
 */
void DBusClient::getPackagesByRepo(const QString &repoAlias, ReplyCallback callback)
{
    QDBusMessage msg = QDBusMessage::createMethodCall(SERVICE_NAME, OBJECT_PATH, INTERFACE, "GetPackagesByRepo");
    msg << repoAlias;
    callAsync(msg, kDefaultTimeout, [callback](bool ok, const QString &name,
                                               const QString &message, const QVariantList &args) {
        if (callback)
            callback(ok, name, message,
                     ok && !args.isEmpty() ? QVariantList{ demarshallArrayOfMaps(args.at(0)) } : QVariantList{});
    });
}

/**
 * @brief パターン一覧を取得する。
 * @param callback 完了時に呼ばれるコールバック (args[0] に QVariantList)
 */
void DBusClient::getPatterns(ReplyCallback callback)
{
    QDBusMessage msg = QDBusMessage::createMethodCall(SERVICE_NAME, OBJECT_PATH, INTERFACE, "GetPatterns");
    callAsync(msg, kDefaultTimeout, [callback](bool ok, const QString &name,
                                               const QString &message, const QVariantList &args) {
        if (callback)
            callback(ok, name, message,
                     ok && !args.isEmpty() ? QVariantList{ demarshallArrayOfMaps(args.at(0)) } : QVariantList{});
    });
}

/**
 * @brief 指定パターンのパッケージ一覧を取得する。
 * @param patternName パターン名
 * @param callback 完了時に呼ばれるコールバック (args[0] に QVariantList)
 */
void DBusClient::getPackagesByPattern(const QString &patternName, ReplyCallback callback)
{
    QDBusMessage msg = QDBusMessage::createMethodCall(SERVICE_NAME, OBJECT_PATH, INTERFACE, "GetPackagesByPattern");
    msg << patternName;
    callAsync(msg, kDefaultTimeout, [callback](bool ok, const QString &name,
                                               const QString &message, const QVariantList &args) {
        if (callback)
            callback(ok, name, message,
                     ok && !args.isEmpty() ? QVariantList{ demarshallArrayOfMaps(args.at(0)) } : QVariantList{});
    });
}

/**
 * @brief パッチ一覧を取得する。
 * @param category パッチカテゴリ
 * @param callback 完了時に呼ばれるコールバック (args[0] に QVariantList)
 */
void DBusClient::getPatches(int category, ReplyCallback callback)
{
    QDBusMessage msg = QDBusMessage::createMethodCall(SERVICE_NAME, OBJECT_PATH, INTERFACE, "GetPatches");
    msg << category;
    callAsync(msg, kDefaultTimeout, [callback](bool ok, const QString &name,
                                               const QString &message, const QVariantList &args) {
        if (callback)
            callback(ok, name, message,
                     ok && !args.isEmpty() ? QVariantList{ demarshallArrayOfMaps(args.at(0)) } : QVariantList{});
    });
}

/**
 * @brief パッケージロック一覧を取得する。
 * @param callback 完了時に呼ばれるコールバック (args[0] に QVariantList)
 */
void DBusClient::getLocks(ReplyCallback callback)
{
    QDBusMessage msg = QDBusMessage::createMethodCall(SERVICE_NAME, OBJECT_PATH, INTERFACE, "GetLocks");
    callAsync(msg, kDefaultTimeout, [callback](bool ok, const QString &name,
                                               const QString &message, const QVariantList &args) {
        if (callback)
            callback(ok, name, message,
                     ok && !args.isEmpty() ? QVariantList{ demarshallArrayOfMaps(args.at(0)) } : QVariantList{});
    });
}

/**
 * @brief パッケージロックを追加する。
 * @param lock ロック定義マップ (name / kind / match / repoAlias / arch / version / comment)
 * @param callback 完了時に呼ばれるコールバック (args[0] に bool)
 */
void DBusClient::addLock(const QVariantMap &lock, ReplyCallback callback)
{
    QDBusMessage msg = QDBusMessage::createMethodCall(SERVICE_NAME, OBJECT_PATH, INTERFACE, "AddLock");
    msg << lock;
    callAsync(msg, kDefaultTimeout, [callback](bool ok, const QString &name,
                                               const QString &message, const QVariantList &args) {
        if (callback)
            callback(ok, name, message,
                     ok && !args.isEmpty() ? QVariantList{ args.at(0).toBool() } : QVariantList{});
    });
}

/**
 * @brief パッケージロックを削除する。
 * @param lock 削除対象のロック定義マップ (追加時のキーと同じ)
 * @param callback 完了時に呼ばれるコールバック (args[0] に bool)
 */
void DBusClient::removeLock(const QVariantMap &lock, ReplyCallback callback)
{
    QDBusMessage msg = QDBusMessage::createMethodCall(SERVICE_NAME, OBJECT_PATH, INTERFACE, "RemoveLock");
    msg << lock;
    callAsync(msg, kDefaultTimeout, [callback](bool ok, const QString &name,
                                               const QString &message, const QVariantList &args) {
        if (callback)
            callback(ok, name, message,
                     ok && !args.isEmpty() ? QVariantList{ args.at(0).toBool() } : QVariantList{});
    });
}

/**
 * @brief zypp 操作履歴を取得する。
 * @param filter フィルタマップ (since / until / limit / offset / actions)
 * @param callback 完了時に呼ばれるコールバック (args[0] に QVariantList)
 */
void DBusClient::getHistory(const QVariantMap &filter, ReplyCallback callback)
{
    QDBusMessage msg = QDBusMessage::createMethodCall(SERVICE_NAME, OBJECT_PATH, INTERFACE, "GetHistory");
    msg << filter;
    callAsync(msg, kDefaultTimeout, [callback](bool ok, const QString &name,
                                               const QString &message, const QVariantList &args) {
        if (callback)
            callback(ok, name, message,
                     ok && !args.isEmpty() ? QVariantList{ demarshallArrayOfMaps(args.at(0)) } : QVariantList{});
    });
}

/**
 * @brief 現在のソルバー問題のテストケースをファイルへ保存する。
 * @param callback 完了時に呼ばれるコールバック (args[0] に保存パス QString)
 */
void DBusClient::createSolverTestcase(ReplyCallback callback)
{
    QDBusMessage msg = QDBusMessage::createMethodCall(SERVICE_NAME, OBJECT_PATH, INTERFACE, "CreateSolverTestcase");
    callAsync(msg, kDefaultTimeout, [callback](bool ok, const QString &name,
                                               const QString &message, const QVariantList &args) {
        if (callback)
            callback(ok, name, message,
                     ok && !args.isEmpty() ? QVariantList{ args.at(0).toString() } : QVariantList{});
    });
}

/**
 * @brief サービス一覧を取得する。
 * @param callback 完了時に呼ばれるコールバック (args[0] に QVariantList)
 */
void DBusClient::getServices(ReplyCallback callback)
{
    QDBusMessage msg = QDBusMessage::createMethodCall(SERVICE_NAME, OBJECT_PATH, INTERFACE, "GetServices");
    callAsync(msg, kDefaultTimeout, [callback](bool ok, const QString &name,
                                               const QString &message, const QVariantList &args) {
        if (callback)
            callback(ok, name, message,
                     ok && !args.isEmpty() ? QVariantList{ demarshallArrayOfMaps(args.at(0)) } : QVariantList{});
    });
}

/**
 * @brief ディスク使用量を取得する。
 * @param callback 完了時に呼ばれるコールバック (args[0] に QVariantList)
 */
void DBusClient::getDiskUsage(ReplyCallback callback)
{
    QDBusMessage msg = QDBusMessage::createMethodCall(SERVICE_NAME, OBJECT_PATH, INTERFACE, "GetDiskUsage");
    callAsync(msg, kDefaultTimeout, [callback](bool ok, const QString &name,
                                               const QString &message, const QVariantList &args) {
        if (callback)
            callback(ok, name, message,
                     ok && !args.isEmpty() ? QVariantList{ demarshallArrayOfMaps(args.at(0)) } : QVariantList{});
    });
}

/**
 * @brief 変更予定パッケージ一覧を取得する。
 * @param callback 完了時に呼ばれるコールバック (args[0] に QVariantList)
 */
void DBusClient::getPendingChanges(ReplyCallback callback)
{
    QDBusMessage msg = QDBusMessage::createMethodCall(SERVICE_NAME, OBJECT_PATH, INTERFACE, "GetPendingChanges");
    callAsync(msg, kDefaultTimeout, [callback](bool ok, const QString &name,
                                               const QString &message, const QVariantList &args) {
        if (callback)
            callback(ok, name, message,
                     ok && !args.isEmpty() ? QVariantList{ demarshallArrayOfMaps(args.at(0)) } : QVariantList{});
    });
}

// -- 状態保存・復元 --

/**
 * @brief 選択状態を保存する (fire-and-forget)。
 */
void DBusClient::saveState()
{
    if (!isConnected())
        return;
    QDBusMessage msg = QDBusMessage::createMethodCall(SERVICE_NAME, OBJECT_PATH, INTERFACE, "SaveState");
    QDBusConnection::systemBus().send(msg);
}

/**
 * @brief 選択状態を復元する。
 * @param callback 完了時に呼ばれるコールバック
 */
void DBusClient::restoreState(ReplyCallback callback)
{
    QDBusMessage msg = QDBusMessage::createMethodCall(SERVICE_NAME, OBJECT_PATH, INTERFACE, "RestoreState");
    callAsync(msg, kDefaultTimeout, [callback](bool ok, const QString &name,
                                               const QString &message, const QVariantList &args) {
        if (callback)
            callback(ok, name, message,
                     ok && !args.isEmpty() ? QVariantList{ args.at(0).toBool() } : QVariantList{});
    });
}

// -- 操作状態 (Wave1) --

/**
 * @brief 現在の操作状態 (a{sv}) を非同期で取得する。
 * @param callback 完了時に呼ばれるコールバック (args[0] に QVariantMap)
 */
void DBusClient::getOperationState(ReplyCallback callback)
{
    QDBusMessage msg = QDBusMessage::createMethodCall(SERVICE_NAME, OBJECT_PATH, INTERFACE, "GetOperationState");
    callAsync(msg, kDefaultTimeout, [callback](bool ok, const QString &name,
                                               const QString &message, const QVariantList &args) {
        if (callback)
            callback(ok, name, message,
                     ok && !args.isEmpty() ? QVariantList{ demarshallMap(args.at(0)) } : QVariantList{});
    });
}

// -- 対話プロンプト (Wave1) --

/**
 * @brief 対話プロンプトへ応答する。
 * @param promptId プロンプトID
 * @param answer 応答マップ (action キーを含む)
 * @param callback 完了時に呼ばれるコールバック (args[0] に bool)
 */
void DBusClient::answerPrompt(uint promptId, const QVariantMap &answer, ReplyCallback callback)
{
    QDBusMessage msg = QDBusMessage::createMethodCall(SERVICE_NAME, OBJECT_PATH, INTERFACE, "AnswerPrompt");
    msg << promptId;
    msg << answer;
    callAsync(msg, kDefaultTimeout, [callback](bool ok, const QString &name,
                                               const QString &message, const QVariantList &args) {
        if (callback)
            callback(ok, name, message,
                     ok && !args.isEmpty() ? QVariantList{ args.at(0).toBool() } : QVariantList{});
    });
}

/**
 * @brief 編集セッションを解放する。
 * @param callback 完了時に呼ばれるコールバック (args[0] に bool)
 */
void DBusClient::releaseSession(ReplyCallback callback)
{
    QDBusMessage msg = QDBusMessage::createMethodCall(SERVICE_NAME, OBJECT_PATH, INTERFACE, "ReleaseSession");
    callAsync(msg, kDefaultTimeout, [callback](bool ok, const QString &name,
                                               const QString &message, const QVariantList &args) {
        if (callback)
            callback(ok, name, message,
                     ok && !args.isEmpty() ? QVariantList{ args.at(0).toBool() } : QVariantList{});
    });
}


void DBusClient::onUntrustedKeyDetected(const QVariantMap &keyInfo)
{
    emit untrustedKeyDetected(demarshallMap(keyInfo));
}

void DBusClient::onOperationStateChanged(const QVariantMap &state)
{
    emit operationStateChanged(demarshallMap(state));
}

void DBusClient::onPromptRequested(uint promptId, const QString &promptType, const QVariantMap &data)
{
    emit promptRequested(promptId, promptType, demarshallMap(data));
}

void DBusClient::onScriptOutput(const QVariantMap &line)
{
    emit scriptOutput(demarshallMap(line));
}

/**
 * @brief ローカルRPMファイルをインストール対象として選択し、ソルバーを実行する。
 * @param paths RPMファイルの絶対パス一覧
 * @param callback 完了時に呼ばれるコールバック (args[0] に結果 QVariantMap)
 */
void DBusClient::installLocalRpms(const QList<QDBusUnixFileDescriptor> &files,
                                  const QStringList &fileNames, ReplyCallback callback)
{
    QDBusMessage msg = QDBusMessage::createMethodCall(SERVICE_NAME, OBJECT_PATH, INTERFACE, "InstallLocalRpms");
    msg << QVariant::fromValue(files) << fileNames;
    callAsync(msg, kNoTimeout, [callback](bool ok, const QString &name,
                                          const QString &message, const QVariantList &args) {
        if (callback)
            callback(ok, name, message,
                     ok && !args.isEmpty() ? QVariantList{ demarshallMap(args.at(0)) } : QVariantList{});
    });
}

/**
 * @brief .repo ファイルの内容を解析してリポジトリ一覧を返す (システムは変更しない)。
 * @param content .repo ファイルの内容
 * @param callback 完了時に呼ばれるコールバック (args[0] に QVariantList)
 */
void DBusClient::parseRepoFile(const QString &content, ReplyCallback callback)
{
    QDBusMessage msg = QDBusMessage::createMethodCall(SERVICE_NAME, OBJECT_PATH, INTERFACE, "ParseRepoFile");
    msg << content;
    callAsync(msg, kDefaultTimeout, [callback](bool ok, const QString &name,
                                               const QString &message, const QVariantList &args) {
        if (callback)
            callback(ok, name, message,
                     ok && !args.isEmpty() ? QVariantList{ demarshallArrayOfMaps(args.at(0)) } : QVariantList{});
    });
}

/**
 * @brief .repo ファイルの内容からリポジトリを追加する。
 * @param content .repo ファイルの内容
 * @param callback 完了時に呼ばれるコールバック (args[0] に結果 QVariantMap)
 */
void DBusClient::importRepoFile(const QString &content, ReplyCallback callback)
{
    QDBusMessage msg = QDBusMessage::createMethodCall(SERVICE_NAME, OBJECT_PATH, INTERFACE, "ImportRepoFile");
    msg << content;
    callAsync(msg, kNoTimeout, [callback](bool ok, const QString &name,
                                          const QString &message, const QVariantList &args) {
        if (callback)
            callback(ok, name, message,
                     ok && !args.isEmpty() ? QVariantList{ demarshallMap(args.at(0)) } : QVariantList{});
    });
}

} // namespace qZypper
