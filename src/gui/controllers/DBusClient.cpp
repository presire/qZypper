#include <QDBusConnection>
#include <QDBusError>
#include <QDBusMessage>
#include <QDBusArgument>
#include <QDBusPendingReply>
#include <QDebug>
#include "DBusClient.h"

namespace qZypper {

/**
 * @brief D-Bus a{sv} を QDBusArgument から読み取り QVariantMap に変換する。
 * @param arg 読み取り元の QDBusArgument
 * @return 変換された QVariantMap
 */
static QVariantMap readSvMap(const QDBusArgument &arg)
{
    QVariantMap map;
    arg.beginMap();
    while (!arg.atEnd()) {
        QString key;
        QDBusVariant dbusValue;
        arg.beginMapEntry();
        arg >> key >> dbusValue;
        arg.endMapEntry();
        map.insert(key, dbusValue.variant());
    }
    arg.endMap();
    return map;
}

/**
 * @brief QVariant を QVariantMap に変換する (QDBusArgument / QVariantMap 両対応)。
 * @param v 変換元の QVariant
 * @return 変換された QVariantMap (変換不可の場合は空)
 */
static QVariantMap variantToMap(const QVariant &v)
{
    if (v.canConvert<QDBusArgument>())
        return readSvMap(v.value<QDBusArgument>());
    if (v.canConvert<QVariantMap>())
        return v.toMap();
    return {};
}

/**
 * @brief D-Bus av / aa{sv} を QVariantList (of QVariantMap) に変換する。
 *
 * Qt は QVariantList を av (array of variants) としてマーシャルするため、
 * 各要素は QDBusVariant で包まれている。
 * @param variant 変換元の QVariant
 * @return QVariantMap のリスト
 */
static QVariantList demarshallArrayOfMaps(const QVariant &variant)
{
    QVariantList result;

    if (!variant.canConvert<QDBusArgument>())
        return variant.toList();

    const QDBusArgument arg = variant.value<QDBusArgument>();
    arg.beginArray();
    while (!arg.atEnd()) {
        if (arg.currentType() == QDBusArgument::VariantType) {
            // av 形式: 各要素が variant で包まれている
            QDBusVariant dbusVar;
            arg >> dbusVar;
            result.append(variantToMap(dbusVar.variant()));
        } else if (arg.currentType() == QDBusArgument::MapType) {
            // aa{sv} 形式: 直接 map
            result.append(readSvMap(arg));
        } else {
            // 不明な型はスキップ
            QVariant skip;
            arg >> skip;
        }
    }
    arg.endArray();

    return result;
}

/**
 * @brief D-Bus a{sv} / v(a{sv}) を QVariantMap に変換する。
 * @param variant 変換元の QVariant
 * @return 変換された QVariantMap
 */
static QVariantMap demarshallMap(const QVariant &variant)
{
    // QDBusVariant でラップされている場合アンラップ
    if (variant.canConvert<QDBusVariant>()) {
        return variantToMap(variant.value<QDBusVariant>().variant());
    }
    return variantToMap(variant);
}

/**
 * @brief DBusClient のコンストラクタ。
 *
 * D-Busサービスウォッチャーを初期化し、バックエンドの登録/解除を監視する。
 * @param parent 親オブジェクト
 */
DBusClient::DBusClient(QObject *parent)
    : QObject(parent)
{
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
 * @return 接続成功時 true
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
    m_iface->setTimeout(120000);

    // シグナルを接続
    bus.connect(
        SERVICE_NAME, OBJECT_PATH, INTERFACE,
        "RepoRefreshProgress",
        this, SIGNAL(repoRefreshProgress(QString, int))
    );
    bus.connect(
        SERVICE_NAME, OBJECT_PATH, INTERFACE,
        "ProgressChanged",
        this, SIGNAL(progressChanged(QString, int, QString))
    );
    bus.connect(
        SERVICE_NAME, OBJECT_PATH, INTERFACE,
        "CommitProgressChanged",
        this, SIGNAL(commitProgressChanged(QString, int, QString, int, int, int))
    );
    bus.connect(
        SERVICE_NAME, OBJECT_PATH, INTERFACE,
        "TransactionFinished",
        this, SIGNAL(transactionFinished(bool, QString))
    );
    bus.connect(
        SERVICE_NAME, OBJECT_PATH, INTERFACE,
        "ErrorOccurred",
        this, SIGNAL(errorOccurred(QString))
    );

    m_connected = true;
    qDebug() << "DBusClient: connected to backend";
    emit backendConnected();
    return true;
}

/**
 * @brief バックエンドとの接続状態を返す。
 * @return 接続中の場合 true
 */
bool DBusClient::isConnected() const
{
    return m_connected && m_iface && m_iface->isValid();
}

/**
 * @brief D-Busサービス登録時のハンドラ。
 * @param serviceName 登録されたサービス名
 */
void DBusClient::onServiceRegistered(const QString &serviceName)
{
    Q_UNUSED(serviceName)
    qDebug() << "DBusClient: backend service registered";
    // 再起動後のバックエンドは未初期化。接続→Initialize→再ロードを促す
    if (connectToBackend()) {
        if (initializeBackend()) {
            qDebug() << "DBusClient: backend re-initialized after restart";
            emit backendReconnected();
        } else {
            qWarning() << "DBusClient: backend re-initialization failed:" << m_lastError;
        }
    }
}

/**
 * @brief D-Busサービス解除時のハンドラ。
 * @param serviceName 解除されたサービス名
 */
void DBusClient::onServiceUnregistered(const QString &serviceName)
{
    Q_UNUSED(serviceName)
    qDebug() << "DBusClient: backend service unregistered";
    m_connected = false;
    emit backendDisconnected();
}

// -- 初期化 --

/**
 * @brief バックエンドの初期化を要求する。
 * @return 初期化成功時 true
 */
bool DBusClient::initializeBackend()
{
    if (!isConnected()) {
        m_lastError = "Not connected to backend";
        return false;
    }

    QDBusReply<bool> reply = m_iface->call("Initialize");
    if (!reply.isValid()) {
        m_lastError = reply.error().message();
        if (m_lastError.isEmpty())
            m_lastError = QStringLiteral("D-Bus call failed (error type: %1)").arg(reply.error().name());
        qWarning() << "DBusClient::initializeBackend:" << m_lastError;
        return false;
    }
    return reply.value();
}

// -- リポジトリ操作 --

/**
 * @brief 全リポジトリの同期リフレッシュを実行する。
 * @return リフレッシュ成功時 true
 */
bool DBusClient::refreshRepos()
{
    if (!isConnected()) {
        m_lastError = "Not connected to backend";
        return false;
    }

    // リフレッシュは時間がかかるためタイムアウトを長めに設定 (5分)
    QDBusMessage msg = QDBusMessage::createMethodCall(SERVICE_NAME, OBJECT_PATH, INTERFACE, "RefreshRepos");
    QDBusMessage replyMsg = QDBusConnection::systemBus().call(msg, QDBus::BlockWithGui, 600000);
    QDBusReply<bool> reply(replyMsg);

    if (!reply.isValid()) {
        m_lastError = reply.error().message();
        qWarning() << "DBusClient::refreshRepos:" << m_lastError;
        return false;
    }
    return reply.value();
}

/**
 * @brief 指定リポジトリの同期リフレッシュを実行する。
 * @param alias リフレッシュ対象のリポジトリエイリアス
 * @return リフレッシュ成功時 true
 */
bool DBusClient::refreshSingleRepo(const QString &alias)
{
    if (!isConnected()) {
        m_lastError = "Not connected to backend";
        return false;
    }

    QDBusMessage msg = QDBusMessage::createMethodCall(SERVICE_NAME, OBJECT_PATH, INTERFACE, "RefreshSingleRepo");
    msg << alias;
    QDBusMessage replyMsg = QDBusConnection::systemBus().call(msg, QDBus::BlockWithGui, 600000);
    QDBusReply<bool> reply(replyMsg);

    if (!reply.isValid()) {
        m_lastError = reply.error().message();
        qWarning() << "DBusClient::refreshSingleRepo:" << m_lastError;
        return false;
    }
    return reply.value();
}

/**
 * @brief リポジトリを追加する。
 * @param url リポジトリURL
 * @param name リポジトリ名
 * @return 結果を格納した QVariantMap (success, errorMessage)
 */
QVariantMap DBusClient::addRepo(const QString &url, const QString &name)
{
    QVariantMap result;
    if (!isConnected()) {
        m_lastError = "Not connected to backend";
        result["success"] = false;
        result["errorMessage"] = m_lastError;
        return result;
    }

    QDBusMessage reply = m_iface->call("AddRepo", url, name);
    if (reply.type() == QDBusMessage::ErrorMessage) {
        m_lastError = reply.errorMessage();
        result["success"] = false;
        result["errorMessage"] = m_lastError;
        return result;
    }
    return reply.arguments().isEmpty() ? result : demarshallMap(reply.arguments().at(0));
}

/**
 * @brief 全プロパティを指定してリポジトリを追加する。
 * @param properties リポジトリプロパティ (url, name, alias, enabled 等)
 * @return 結果を格納した QVariantMap (success, errorMessage)
 */
QVariantMap DBusClient::addRepoFull(const QVariantMap &properties)
{
    QVariantMap result;
    if (!isConnected()) {
        m_lastError = "Not connected to backend";
        result["success"] = false;
        result["errorMessage"] = m_lastError;
        return result;
    }

    QDBusMessage reply = m_iface->call("AddRepoFull", properties);
    if (reply.type() == QDBusMessage::ErrorMessage) {
        m_lastError = reply.errorMessage();
        result["success"] = false;
        result["errorMessage"] = m_lastError;
        return result;
    }
    return reply.arguments().isEmpty() ? result : demarshallMap(reply.arguments().at(0));
}

/**
 * @brief リポジトリを削除する。
 * @param alias 削除対象のリポジトリエイリアス
 * @return 削除成功時 true
 */
bool DBusClient::removeRepo(const QString &alias)
{
    if (!isConnected()) {
        m_lastError = "Not connected to backend";
        return false;
    }

    QDBusReply<bool> reply = m_iface->call("RemoveRepo", alias);
    if (!reply.isValid()) {
        m_lastError = reply.error().message();
        return false;
    }
    return reply.value();
}

/**
 * @brief リポジトリの有効/無効を切り替える。
 * @param alias 対象のリポジトリエイリアス
 * @param enabled 有効にする場合 true
 * @return 変更成功時 true
 */
bool DBusClient::setRepoEnabled(const QString &alias, bool enabled)
{
    if (!isConnected()) {
        m_lastError = "Not connected to backend";
        return false;
    }

    QDBusReply<bool> reply = m_iface->call("SetRepoEnabled", alias, enabled);
    if (!reply.isValid()) {
        m_lastError = reply.error().message();
        return false;
    }
    return reply.value();
}

/**
 * @brief リポジトリのプロパティを変更する。
 * @param alias 対象のリポジトリエイリアス
 * @param properties 変更するプロパティ
 * @return 変更成功時 true
 */
bool DBusClient::modifyRepo(const QString &alias, const QVariantMap &properties)
{
    if (!isConnected()) {
        m_lastError = "Not connected to backend";
        return false;
    }

    QDBusReply<bool> reply = m_iface->call("ModifyRepo", alias, properties);
    if (!reply.isValid()) {
        m_lastError = reply.error().message();
        return false;
    }
    return reply.value();
}

// -- サービス操作 --

/**
 * @brief サービスを追加する。
 * @param url サービスURL
 * @param alias サービスエイリアス
 * @return 追加成功時 true
 */
bool DBusClient::addService(const QString &url, const QString &alias)
{
    if (!isConnected()) {
        m_lastError = "Not connected to backend";
        return false;
    }

    QDBusReply<bool> reply = m_iface->call("AddService", url, alias);
    if (!reply.isValid()) {
        m_lastError = reply.error().message();
        return false;
    }
    return reply.value();
}

/**
 * @brief サービスを削除する。
 * @param alias 削除対象のサービスエイリアス
 * @return 削除成功時 true
 */
bool DBusClient::removeService(const QString &alias)
{
    if (!isConnected()) {
        m_lastError = "Not connected to backend";
        return false;
    }

    QDBusReply<bool> reply = m_iface->call("RemoveService", alias);
    if (!reply.isValid()) {
        m_lastError = reply.error().message();
        return false;
    }
    return reply.value();
}

/**
 * @brief サービスのプロパティを変更する。
 * @param alias 対象のサービスエイリアス
 * @param properties 変更するプロパティ
 * @return 変更成功時 true
 */
bool DBusClient::modifyService(const QString &alias, const QVariantMap &properties)
{
    if (!isConnected()) {
        m_lastError = "Not connected to backend";
        return false;
    }

    QDBusReply<bool> reply = m_iface->call("ModifyService", alias, properties);
    if (!reply.isValid()) {
        m_lastError = reply.error().message();
        return false;
    }
    return reply.value();
}

/**
 * @brief サービスをリフレッシュする。
 * @param alias リフレッシュ対象のサービスエイリアス
 * @return リフレッシュ成功時 true
 */
bool DBusClient::refreshService(const QString &alias)
{
    if (!isConnected()) {
        m_lastError = "Not connected to backend";
        return false;
    }

    QDBusMessage msg = QDBusMessage::createMethodCall(SERVICE_NAME, OBJECT_PATH, INTERFACE, "RefreshService");
    msg << alias;
    QDBusMessage replyMsg = QDBusConnection::systemBus().call(msg, QDBus::BlockWithGui, 600000);
    QDBusReply<bool> reply(replyMsg);

    if (!reply.isValid()) {
        m_lastError = reply.error().message();
        return false;
    }
    return reply.value();
}

// -- パッケージ状態変更 --

/**
 * @brief パッケージのステータスを変更する。
 * @param name パッケージ名
 * @param status 新しいステータス (PackageStatus)
 * @return 変更成功時 true
 */
bool DBusClient::setPackageStatus(const QString &name, int status)
{
    if (!isConnected()) {
        m_lastError = "Not connected to backend";
        return false;
    }

    QDBusReply<bool> reply = m_iface->call("SetPackageStatus", name, status);
    if (!reply.isValid()) {
        m_lastError = reply.error().message();
        return false;
    }
    return reply.value();
}

/**
 * @brief パッケージのバージョンを変更する。
 * @param name パッケージ名
 * @param version 選択バージョン (edition 文字列)
 * @param arch アーキテクチャ
 * @param repoAlias リポジトリエイリアス
 * @return 変更成功時 true
 */
bool DBusClient::setPackageVersion(const QString &name, const QString &version,
                                   const QString &arch, const QString &repoAlias)
{
    if (!isConnected()) {
        m_lastError = "Not connected to backend";
        return false;
    }

    QDBusReply<bool> reply = m_iface->call("SetPackageVersion", name, version, arch, repoAlias);
    if (!reply.isValid()) {
        m_lastError = reply.error().message();
        return false;
    }
    return reply.value();
}

/**
 * @brief パターンのステータスを変更する。
 * @param name パターン名
 * @param status 新しいステータス (PackageStatus)
 * @return 変更成功時 true
 */
bool DBusClient::setPatternStatus(const QString &name, int status)
{
    if (!isConnected()) {
        m_lastError = "Not connected to backend";
        return false;
    }

    QDBusReply<bool> reply = m_iface->call("SetPatternStatus", name, status);
    if (!reply.isValid()) {
        m_lastError = reply.error().message();
        return false;
    }
    return reply.value();
}

// -- 依存関係解決 --

/**
 * @brief 全パッケージの更新を実行する。
 *
 * バックエンドの UpdateAllPackages (doUpdate + 依存解決) を呼び出す。
 * @return 解決結果 (success, problems)
 */
QVariantMap DBusClient::updateAllPackages()
{
    QVariantMap result;
    if (!isConnected()) {
        m_lastError = "Not connected to backend";
        result["success"] = false;
        return result;
    }

    QDBusMessage reply = m_iface->call("UpdateAllPackages");
    if (reply.type() == QDBusMessage::ErrorMessage) {
        m_lastError = reply.errorMessage();
        qWarning() << "DBusClient::updateAllPackages:" << m_lastError;
        result["success"] = false;
        return result;
    }

    if (reply.arguments().isEmpty())
        return result;

    result = demarshallMap(reply.arguments().at(0));

    // ネストされた problems 配列を再帰的にデマーシャリング
    if (result.contains("problems")) {
        QVariantList problems = demarshallArrayOfMaps(result.value("problems"));
        QVariantList demarshalled;
        for (const auto &p : std::as_const(problems)) {
            QVariantMap problem = variantToMap(p);
            if (problem.contains("solutions")) {
                problem["solutions"] = demarshallArrayOfMaps(problem.value("solutions"));
            }
            demarshalled.append(problem);
        }
        result["problems"] = demarshalled;
    }

    return result;
}

/**
 * @brief 依存関係を解決する。
 *
 * ソルバーを実行し、衝突がある場合は problems 配列を再帰的にデマーシャリングする。
 * @return 解決結果 (success, problems)
 */
QVariantMap DBusClient::resolveDependencies()
{
    QVariantMap result;
    if (!isConnected()) {
        m_lastError = "Not connected to backend";
        result["success"] = false;
        return result;
    }

    QDBusMessage reply = m_iface->call("ResolveDependencies");
    if (reply.type() == QDBusMessage::ErrorMessage) {
        m_lastError = reply.errorMessage();
        qWarning() << "DBusClient::resolveDependencies:" << m_lastError;
        result["success"] = false;
        return result;
    }

    if (reply.arguments().isEmpty())
        return result;

    result = demarshallMap(reply.arguments().at(0));

    // ネストされた problems 配列を再帰的にデマーシャリング
    if (result.contains("problems")) {
        QVariantList problems = demarshallArrayOfMaps(result.value("problems"));
        QVariantList demarshalled;
        for (const auto &p : std::as_const(problems)) {
            QVariantMap problem = variantToMap(p);
            // 各 problem 内の solutions もデマーシャリング
            if (problem.contains("solutions")) {
                problem["solutions"] = demarshallArrayOfMaps(problem.value("solutions"));
            }
            demarshalled.append(problem);
        }
        result["problems"] = demarshalled;
    }

    return result;
}

/**
 * @brief 依存関係の衝突に対する解決策を適用する。
 * @param problemIndex 問題のインデックス
 * @param solutionIndex 解決策のインデックス
 * @return 適用成功時 true
 */
bool DBusClient::applySolution(int problemIndex, int solutionIndex)
{
    if (!isConnected()) {
        m_lastError = "Not connected to backend";
        return false;
    }

    QDBusReply<bool> reply = m_iface->call("ApplySolution", problemIndex, solutionIndex);
    if (!reply.isValid()) {
        m_lastError = reply.error().message();
        return false;
    }
    return reply.value();
}

// -- コミット --

/**
 * @brief パッケージの変更をコミットする。
 * @return コミット結果 (success, installed, updated, removed 等)
 */
QVariantMap DBusClient::commit()
{
    QVariantMap result;
    if (!isConnected()) {
        m_lastError = "Not connected to backend";
        result["success"] = false;
        return result;
    }

    QDBusMessage reply = m_iface->call(QDBus::Block, "Commit");
    if (reply.type() == QDBusMessage::ErrorMessage) {
        m_lastError = reply.errorMessage();
        result["success"] = false;
        return result;
    }
    return reply.arguments().isEmpty() ? result : demarshallMap(reply.arguments().at(0));
}

/**
 * @brief パッケージの変更を非同期でコミットする。
 *
 * 完了時に commitFinished シグナルを発行する。タイムアウトは600秒。
 */
void DBusClient::commitAsync()
{
    if (!isConnected()) {
        QVariantMap result;
        result["success"] = false;
        emit commitFinished(result);
        return;
    }

    QDBusMessage msg = QDBusMessage::createMethodCall(
        SERVICE_NAME, OBJECT_PATH, INTERFACE, "Commit");
    QDBusPendingCall pending = QDBusConnection::systemBus().asyncCall(msg, 600000);
    auto *watcher = new QDBusPendingCallWatcher(pending, this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher *w) {
        QDBusPendingReply<> reply = *w;
        QVariantMap result;
        if (reply.isError()) {
            m_lastError = reply.error().message();
            result["success"] = false;
        } else {
            auto args = reply.reply().arguments();
            if (!args.isEmpty())
                result = demarshallMap(args.at(0));
        }
        emit commitFinished(result);
        w->deleteLater();
    });
}

/**
 * @brief 全リポジトリの非同期リフレッシュを実行する。
 *
 * 完了時に refreshReposFinished シグナルを発行する。
 */
void DBusClient::refreshReposAsync()
{
    if (!isConnected()) {
        emit refreshReposFinished(false);
        return;
    }

    QDBusMessage msg = QDBusMessage::createMethodCall(SERVICE_NAME, OBJECT_PATH, INTERFACE, "RefreshRepos");
    QDBusPendingCall pending = QDBusConnection::systemBus().asyncCall(msg, 600000);
    auto *watcher = new QDBusPendingCallWatcher(pending, this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher *w) {
        QDBusPendingReply<bool> reply = *w;
        bool success = !reply.isError() && reply.value();
        if (reply.isError())
            m_lastError = reply.error().message();
        emit refreshReposFinished(success);
        w->deleteLater();
    });
}

/**
 * @brief 実行中の操作をキャンセルする。
 */
void DBusClient::cancelOperation()
{
    if (!isConnected()) return;
    m_iface->call(QDBus::NoBlock, "CancelOperation");
}

// -- 読み取り操作 --

/**
 * @brief リポジトリ一覧を取得する。
 * @return リポジトリ情報の QVariantList
 */
QVariantList DBusClient::getRepos()
{
    if (!isConnected()) return {};

    QDBusMessage reply = m_iface->call("GetRepos");
    if (reply.type() == QDBusMessage::ErrorMessage) {
        m_lastError = reply.errorMessage();
        return {};
    }

    return reply.arguments().isEmpty() ? QVariantList{} : demarshallArrayOfMaps(reply.arguments().at(0));
}

/**
 * @brief パッケージを検索する。
 * @param query 検索クエリ文字列
 * @param flags 検索フラグ (SearchFlag のビットマスク)
 * @return 検索結果のパッケージ情報リスト
 */
QVariantList DBusClient::searchPackages(const QString &query, int flags)
{
    if (!isConnected()) return {};

    QDBusMessage reply = m_iface->call("SearchPackages", query, flags);
    if (reply.type() == QDBusMessage::ErrorMessage) {
        m_lastError = reply.errorMessage();
        return {};
    }
    return reply.arguments().isEmpty() ? QVariantList{} : demarshallArrayOfMaps(reply.arguments().at(0));
}

/**
 * @brief パッケージの詳細情報を取得する。
 * @param name パッケージ名
 * @return パッケージ詳細の QVariantMap
 */
QVariantMap DBusClient::getPackageDetails(const QString &name)
{
    if (!isConnected()) return {};

    QDBusMessage reply = m_iface->call("GetPackageDetails", name);
    if (reply.type() == QDBusMessage::ErrorMessage) {
        m_lastError = reply.errorMessage();
        return {};
    }
    if (reply.arguments().isEmpty())
        return {};

    QVariantMap result = demarshallMap(reply.arguments().at(0));

    // availableVersions のネストされた配列をデマーシャリング
    if (result.contains("availableVersions"))
        result["availableVersions"] = demarshallArrayOfMaps(result.value("availableVersions"));

    return result;
}

/**
 * @brief 指定リポジトリに属するパッケージ一覧を取得する。
 * @param repoAlias リポジトリエイリアス
 * @return パッケージ情報のリスト
 */
QVariantList DBusClient::getPackagesByRepo(const QString &repoAlias)
{
    if (!isConnected()) return {};

    QDBusMessage reply = m_iface->call("GetPackagesByRepo", repoAlias);
    if (reply.type() == QDBusMessage::ErrorMessage) {
        m_lastError = reply.errorMessage();
        return {};
    }
    return reply.arguments().isEmpty() ? QVariantList{} : demarshallArrayOfMaps(reply.arguments().at(0));
}

/**
 * @brief パターン一覧を取得する。
 * @return パターン情報の QVariantList
 */
QVariantList DBusClient::getPatterns()
{
    if (!isConnected()) return {};

    QDBusMessage reply = m_iface->call("GetPatterns");
    if (reply.type() == QDBusMessage::ErrorMessage) {
        m_lastError = reply.errorMessage();
        return {};
    }
    return reply.arguments().isEmpty() ? QVariantList{} : demarshallArrayOfMaps(reply.arguments().at(0));
}

/**
 * @brief 指定パターンに属するパッケージ一覧を取得する。
 * @param patternName パターン名
 * @return パッケージ情報のリスト
 */
QVariantList DBusClient::getPackagesByPattern(const QString &patternName)
{
    if (!isConnected()) return {};

    QDBusMessage reply = m_iface->call("GetPackagesByPattern", patternName);
    if (reply.type() == QDBusMessage::ErrorMessage) {
        m_lastError = reply.errorMessage();
        return {};
    }
    return reply.arguments().isEmpty() ? QVariantList{} : demarshallArrayOfMaps(reply.arguments().at(0));
}

/**
 * @brief パッチ一覧を取得する。
 * @param category パッチカテゴリ (PatchCategory)
 * @return パッチ情報のリスト
 */
QVariantList DBusClient::getPatches(int category)
{
    if (!isConnected()) return {};

    QDBusMessage reply = m_iface->call("GetPatches", category);
    if (reply.type() == QDBusMessage::ErrorMessage) {
        m_lastError = reply.errorMessage();
        return {};
    }
    return reply.arguments().isEmpty() ? QVariantList{} : demarshallArrayOfMaps(reply.arguments().at(0));
}

/**
 * @brief サービス一覧を取得する。
 * @return サービス情報の QVariantList
 */
QVariantList DBusClient::getServices()
{
    if (!isConnected()) return {};

    QDBusMessage reply = m_iface->call("GetServices");
    if (reply.type() == QDBusMessage::ErrorMessage) {
        m_lastError = reply.errorMessage();
        return {};
    }
    return reply.arguments().isEmpty() ? QVariantList{} : demarshallArrayOfMaps(reply.arguments().at(0));
}

/**
 * @brief ディスク使用量を取得する。
 * @return ディスク使用量情報のリスト
 */
QVariantList DBusClient::getDiskUsage()
{
    if (!isConnected()) return {};

    QDBusMessage reply = m_iface->call("GetDiskUsage");
    if (reply.type() == QDBusMessage::ErrorMessage) {
        m_lastError = reply.errorMessage();
        return {};
    }
    return reply.arguments().isEmpty() ? QVariantList{} : demarshallArrayOfMaps(reply.arguments().at(0));
}

// -- 変更予定パッケージ一覧 --

/**
 * @brief 変更予定のパッケージ一覧を取得する。
 * @return 変更予定パッケージ情報のリスト
 */
QVariantList DBusClient::getPendingChanges()
{
    if (!isConnected()) return {};

    QDBusMessage reply = m_iface->call("GetPendingChanges");
    if (reply.type() == QDBusMessage::ErrorMessage) {
        m_lastError = reply.errorMessage();
        return {};
    }
    return reply.arguments().isEmpty() ? QVariantList{} : demarshallArrayOfMaps(reply.arguments().at(0));
}

// -- 状態保存・復元 --

/**
 * @brief パッケージの選択状態を保存する。
 */
void DBusClient::saveState()
{
    if (!isConnected()) return;
    m_iface->call(QDBus::NoBlock, "SaveState");
}

/**
 * @brief 保存されたパッケージ選択状態を復元する。
 */
void DBusClient::restoreState()
{
    if (!isConnected()) return;
    m_iface->call(QDBus::NoBlock, "RestoreState");
}

/**
 * @brief バックエンドプロセスを終了させる。
 */
void DBusClient::quit()
{
    if (!isConnected()) return;
    m_iface->call(QDBus::NoBlock, "Quit");
}

} // namespace qZypper
