#include <QDebug>
#include <QUrl>
#include <QGuiApplication>
#include <QDesktopServices>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include "PackageController.h"
#include "DBusClient.h"
#include "PackageListModel.h"
#include "PackageFilterProxyModel.h"


namespace qZypper {

// -- 構築・破棄 --

/**
 * @brief PackageControllerのコンストラクタ
 *
 * DBusClientを初期化し、D-Busシグナルの中継を設定する
 *
 * @param parent 親オブジェクト
 */
PackageController::PackageController(QObject *parent)
    : QObject(parent)
{
    m_dbus = new DBusClient(this);

    m_packageModel = new PackageListModel(this);
    m_packageProxy = new PackageFilterProxyModel(this);
    m_packageProxy->setSourceModel(m_packageModel);

    // D-Busシグナルを中継
    connect(m_dbus, &DBusClient::repoRefreshProgress,
            this, [this](const QString &alias, int pct) {
        if (pct >= 100)
            setStatusMessage(tr("Repository refresh completed"));
        else
            setStatusMessage(tr("Refreshing: %1 (%2%)").arg(alias).arg(pct));
    });
    connect(m_dbus, &DBusClient::progressChanged,
            this, &PackageController::progressChanged);
    connect(m_dbus, &DBusClient::commitProgressChanged,
            this, &PackageController::commitProgressChanged);
    connect(m_dbus, &DBusClient::packageStateChanged,
            this, &PackageController::packageStateChanged);
    connect(m_dbus, &DBusClient::transactionFinished,
            this, &PackageController::transactionFinished);
    connect(m_dbus, &DBusClient::errorOccurred,
            this, &PackageController::errorOccurred);
    connect(m_dbus, &DBusClient::untrustedKeyDetected,
            this, &PackageController::untrustedKeyDetected);
    connect(m_dbus, &DBusClient::operationStateChanged,
            this, &PackageController::setOperationState);
    connect(m_dbus, &DBusClient::promptRequested,
            this, &PackageController::enqueuePrompt);
    // タイムアウト・取消等でバックエンド側が終了したプロンプトは表示から外す
    connect(m_dbus, &DBusClient::promptClosed,
            this, &PackageController::removePrompt);
    connect(m_dbus, &DBusClient::poolReloaded,
            this, [this]() {
        refreshPackageStatuses(true);
        refreshPendingChanges();
        loadPatterns();
        emit poolReloaded();
    });
    connect(m_dbus, &DBusClient::packageLocked,
            this, &PackageController::handlePackageLocked);
    connect(m_dbus, &DBusClient::scriptOutput,
            this, &PackageController::scriptOutputReceived);

    // バックエンドが再起動した場合、接続状態と一覧を復元する
    connect(m_dbus, &DBusClient::backendReconnected,
            this, [this]() {
        m_connected = true;
        m_initialized = true;
        emit connectedChanged();
        emit initializedChanged();
        // 旧バックエンドで実行中だった操作は結果を受け取れないため、ビジー状態と改訂番号を初期化する
        setBusy(false);
        m_commitRevision = 0;
        setStatusMessage(tr("Backend reconnected"));
        // 新しいバックエンドはprompt IDを1から再利用するため、旧プロンプトは破棄する
        clearPrompts();
        loadRepos();
        refreshPendingChanges();
    });
    connect(m_dbus, &DBusClient::backendDisconnected,
            this, [this]() {
        // 切断したバックエンドのプロンプトへの回答が別プロセスの同一IDへ届かないよう全破棄
        clearPrompts();
        // バックエンド消滅後は実行中操作の応答が来ないため、ビジー状態と改訂番号を解除する
        setBusy(false);
        m_commitRevision = 0;
        // 旧セッションで開始した.repoプレビューの結果を、再接続後に表示しない
        ++m_repoPreviewRequest;
        if (m_connected) {
            m_connected = false;
            m_initialized = false;
            emit connectedChanged();
            emit initializedChanged();
            setStatusMessage(tr("Backend disconnected"));
        }
        // 破棄されたGetPendingChangesの応答では取得中フラグが解除されないため、
        // 未接続として取得状態とキャッシュを初期化し、待機者へ失敗を通知する
        refreshPendingChanges();
    });
}

/**
 * @brief デストラクタ
 *        キャッシュデータをクリアする
 */
PackageController::~PackageController()
{
    m_packages.clear();
    m_repos.clear();
    m_services.clear();
    m_patterns.clear();
    m_currentDetails.clear();
}

// -- 状態通知 --

/**
 * @brief ビジー状態を設定する
 * @param busy ビジー状態
 */
void PackageController::setBusy(bool busy)
{
    if (m_busy != busy) {
        m_busy = busy;
        emit busyChanged();
    }
}

/**
 * @brief ステータスメッセージを設定する
 * @param msg 表示するメッセージ
 */
void PackageController::setStatusMessage(const QString &msg)
{
    if (m_statusMessage != msg) {
        m_statusMessage = msg;
        emit statusMessageChanged();
    }
}

// -- 初期化・再接続 --

/**
 * @brief パッケージマネージャを初期化する (非同期)
 *
 * D-Busバックエンドに接続し、libzyppを非同期で初期化する
 * 完了するとinitializedChangedシグナルが発行され、成功時はm_initializedがtrueになる
 *
 * GUIスレッドはブロックしない
 */
void PackageController::initialize()
{
    if (m_busy)
        return;
    m_retryAction = [this]() { initialize(); };
    setBusy(true);
    setStatusMessage(tr("Initializing package manager..."));

    if (!m_dbus->connectToBackend()) {
        m_connected = false;
        m_initialized = false;
        emit connectedChanged();
        emit initializedChanged();
        const QString err = m_dbus->lastError();
        setStatusMessage(tr("Failed to connect to backend: %1").arg(err));
        emit errorOccurred(err);
        qWarning() << "D-Bus backend connection failed:" << err;
        setBusy(false);
        return;
    }

    m_dbus->initializeBackend([this](bool ok, const QString &, const QString &, const QVariantList &) {
        if (ok) {
            m_connected = true;
            m_initialized = true;
            emit connectedChanged();
            emit initializedChanged();
            setStatusMessage(tr("Ready"));
            refreshPendingChanges();
            m_dbus->getOperationState([this](bool ok, const QString &, const QString &, const QVariantList &args) {
                if (ok && !args.isEmpty())
                    setOperationState(args.at(0).toMap());
            });
            qDebug() << "D-Bus backend initialized successfully";
        } else {
            m_connected = false;
            m_initialized = false;
            emit connectedChanged();
            emit initializedChanged();
            const QString err = m_dbus->lastError();
            setStatusMessage(tr("Failed to initialize backend: %1").arg(err));
            emit errorOccurred(err);
            qWarning() << "D-Bus backend initialization failed:" << err;
        }
        setBusy(false);
    });
}

/**
 * @brief バックエンド切断時に再接続と再初期化を非同期で試みる
 * @param onDone 完了コールバック (再接続・初期化成功時はtrue)
 */
void PackageController::reconnectBackend(std::function<void(bool)> onDone)
{
    clearPrompts();  // 旧バックエンドのprompt IDは新プロセスで再利用されるため破棄
    if (!m_dbus->connectToBackend()) {
        m_connected = false;
        m_initialized = false;
        emit connectedChanged();
        emit initializedChanged();
        if (onDone) onDone(false);
        return;
    }

    m_dbus->initializeBackend([this, onDone](bool ok, const QString &, const QString &, const QVariantList &) {
        m_connected = ok;
        m_initialized = ok;
        emit connectedChanged();
        emit initializedChanged();
        if (ok)
            refreshPendingChanges();
        if (onDone) onDone(ok);
    });
}

/**
 * @brief 実行中の操作をキャンセルする
 */
void PackageController::cancelOperation()
{
    if (!m_connected) return;
    m_dbus->cancelOperation();
    setStatusMessage(tr("Cancelling..."));
}

// -- テーマ --

/**
 * @brief ダークモード/ライトモードのパレットを構築して適用する
 *
 * Qt 6.5では、QMLのpaletteグループ化プロパティのバインディングが動的変更を子コントロールに正しく伝播しないため、
 * C++側でQPaletteを構築してQGuiApplication::setPalette()で適用する
 *
 * @param dark trueならダークモード、falseならライトモード
 */
void PackageController::applyDarkMode(bool dark)
{
    QPalette p;

    // Active / Inactive
    const QColor window          = dark ? QColor(0x35, 0x35, 0x35) : QColor(0xef, 0xef, 0xef);
    const QColor windowText      = dark ? QColor(0xff, 0xff, 0xff) : QColor(0x00, 0x00, 0x00);
    const QColor base            = dark ? QColor(0x25, 0x25, 0x25) : QColor(0xff, 0xff, 0xff);
    const QColor alternateBase   = dark ? QColor(0x35, 0x35, 0x35) : QColor(0xf7, 0xf7, 0xf7);
    const QColor text            = dark ? QColor(0xff, 0xff, 0xff) : QColor(0x00, 0x00, 0x00);
    const QColor button          = dark ? QColor(0x45, 0x45, 0x45) : QColor(0xe0, 0xe0, 0xe0);
    const QColor buttonText      = dark ? QColor(0xff, 0xff, 0xff) : QColor(0x00, 0x00, 0x00);
    const QColor highlight       = dark ? QColor(0x21, 0x96, 0xf3) : QColor(0x30, 0x8c, 0xc6);
    const QColor highlightedText = dark ? QColor(0xff, 0xff, 0xff) : QColor(0xff, 0xff, 0xff);
    const QColor placeholderText = dark ? QColor(0x88, 0x88, 0x88) : QColor(0x59, 0x59, 0x59);
    const QColor mid             = dark ? QColor(0x55, 0x55, 0x55) : QColor(0xb0, 0xb0, 0xb0);
    const QColor light           = dark ? QColor(0x55, 0x55, 0x55) : QColor(0xff, 0xff, 0xff);
    const QColor midlight        = dark ? QColor(0x40, 0x40, 0x40) : QColor(0xe3, 0xe3, 0xe3);
    const QColor darkColor       = dark ? QColor(0x23, 0x23, 0x23) : QColor(0x9f, 0x9f, 0x9f);
    const QColor shadow          = dark ? QColor(0x14, 0x14, 0x14) : QColor(0x76, 0x76, 0x76);
    const QColor link            = dark ? QColor(0x64, 0xb5, 0xf6) : QColor(0x00, 0x00, 0xff);
    const QColor linkVisited     = dark ? QColor(0xce, 0x93, 0xd8) : QColor(0xff, 0x00, 0xff);
    const QColor toolTipBase     = dark ? QColor(0x45, 0x45, 0x45) : QColor(0xff, 0xff, 0xdc);
    const QColor toolTipText     = dark ? QColor(0xff, 0xff, 0xff) : QColor(0x00, 0x00, 0x00);

    for (auto group : {QPalette::Active, QPalette::Inactive}) {
        p.setColor(group, QPalette::Window,          window);
        p.setColor(group, QPalette::WindowText,      windowText);
        p.setColor(group, QPalette::Base,            base);
        p.setColor(group, QPalette::AlternateBase,   alternateBase);
        p.setColor(group, QPalette::Text,            text);
        p.setColor(group, QPalette::Button,          button);
        p.setColor(group, QPalette::ButtonText,      buttonText);
        p.setColor(group, QPalette::Highlight,       highlight);
        p.setColor(group, QPalette::HighlightedText, highlightedText);
        p.setColor(group, QPalette::PlaceholderText, placeholderText);
        p.setColor(group, QPalette::Mid,             mid);
        p.setColor(group, QPalette::Light,           light);
        p.setColor(group, QPalette::Midlight,        midlight);
        p.setColor(group, QPalette::Dark,            darkColor);
        p.setColor(group, QPalette::Shadow,          shadow);
        p.setColor(group, QPalette::Link,            link);
        p.setColor(group, QPalette::LinkVisited,     linkVisited);
        p.setColor(group, QPalette::ToolTipBase,     toolTipBase);
        p.setColor(group, QPalette::ToolTipText,     toolTipText);
    }

    // Disabled
    const QColor disWindowText      = dark ? QColor(0x80, 0x80, 0x80) : QColor(0xa0, 0xa0, 0xa0);
    const QColor disBase            = dark ? QColor(0x2a, 0x2a, 0x2a) : QColor(0xef, 0xef, 0xef);
    const QColor disText            = dark ? QColor(0x80, 0x80, 0x80) : QColor(0xa0, 0xa0, 0xa0);
    const QColor disButtonText      = dark ? QColor(0x80, 0x80, 0x80) : QColor(0xa0, 0xa0, 0xa0);
    const QColor disHighlight       = dark ? QColor(0x44, 0x44, 0x44) : QColor(0xc0, 0xc0, 0xc0);
    const QColor disHighlightedText = dark ? QColor(0x80, 0x80, 0x80) : QColor(0xa0, 0xa0, 0xa0);
    const QColor disPlaceholderText = dark ? QColor(0x60, 0x60, 0x60) : QColor(0xc0, 0xc0, 0xc0);
    const QColor disLink            = dark ? QColor(0x50, 0x68, 0x88) : QColor(0x80, 0x80, 0xc0);
    const QColor disLinkVisited     = dark ? QColor(0x7a, 0x68, 0x88) : QColor(0xc0, 0x80, 0xc0);
    const QColor disToolTipText     = dark ? QColor(0x80, 0x80, 0x80) : QColor(0xa0, 0xa0, 0xa0);

    p.setColor(QPalette::Disabled, QPalette::Window,          window);
    p.setColor(QPalette::Disabled, QPalette::WindowText,      disWindowText);
    p.setColor(QPalette::Disabled, QPalette::Base,            disBase);
    p.setColor(QPalette::Disabled, QPalette::AlternateBase,   alternateBase);
    p.setColor(QPalette::Disabled, QPalette::Text,            disText);
    p.setColor(QPalette::Disabled, QPalette::Button,          button);
    p.setColor(QPalette::Disabled, QPalette::ButtonText,      disButtonText);
    p.setColor(QPalette::Disabled, QPalette::Highlight,       disHighlight);
    p.setColor(QPalette::Disabled, QPalette::HighlightedText, disHighlightedText);
    p.setColor(QPalette::Disabled, QPalette::PlaceholderText, disPlaceholderText);
    p.setColor(QPalette::Disabled, QPalette::Mid,             mid);
    p.setColor(QPalette::Disabled, QPalette::Light,           light);
    p.setColor(QPalette::Disabled, QPalette::Midlight,        midlight);
    p.setColor(QPalette::Disabled, QPalette::Dark,            darkColor);
    p.setColor(QPalette::Disabled, QPalette::Shadow,          shadow);
    p.setColor(QPalette::Disabled, QPalette::Link,            disLink);
    p.setColor(QPalette::Disabled, QPalette::LinkVisited,     disLinkVisited);
    p.setColor(QPalette::Disabled, QPalette::ToolTipBase,     toolTipBase);
    p.setColor(QPalette::Disabled, QPalette::ToolTipText,     disToolTipText);

    QGuiApplication::setPalette(p);
}

// -- 操作状態 / プロンプト / セッション --

/**
 * @brief パッケージ一覧モデルを返す (QML公開用)
 * @return PackageListModelへのQObjectポインタ
 */
QObject *PackageController::packageModel() const
{
    return m_packageModel;
}

/**
 * @brief フィルタ/ソートプロキシモデルを返す (QML公開用)
 * @return PackageFilterProxyModelへのQObjectポインタ
 */
QObject *PackageController::packageProxy() const
{
    return m_packageProxy;
}

/**
 * @brief 先頭の未応答プロンプトを返す
 * @return プロンプトマップ (キューが空なら空マップ)
 */
QVariantMap PackageController::currentPrompt() const
{
    return m_prompts.isEmpty() ? QVariantMap() : m_prompts.first().toMap();
}

/**
 * @brief 操作状態を更新し、変更があればoperationStateChangedを発行する
 * @param state バックエンドから受信した状態マップ
 */
void PackageController::setOperationState(const QVariantMap &state)
{
    if (m_operationState == state)
        return;
    m_operationState = state;
    emit operationStateChanged();
}

/**
 * @brief 対話プロンプトをキューへ追加し、QMLへ通知する
 * @param promptId プロンプトID
 * @param type プロンプト種別
 * @param data プロンプトデータ
 */
void PackageController::enqueuePrompt(uint promptId, const QString &type, const QVariantMap &data)
{
    // prompt IDはバックエンドプロセス内で単調増加する
    // 既存以下のIDが届いた場合は、切断通知より先に新プロセスから届いたものと判断し、旧世代のプロンプトを破棄する
    for (const QVariant &entry : std::as_const(m_prompts)) {
        if (entry.toMap().value(QStringLiteral("promptId")).toUInt() >= promptId) {
            clearPrompts();
            break;
        }
    }

    QVariantMap prompt;
    prompt[QStringLiteral("promptId")] = promptId;
    prompt[QStringLiteral("type")] = type;
    prompt[QStringLiteral("data")] = data;
    m_prompts.append(prompt);
    emit pendingPromptsChanged();
    emit promptRequested(prompt);
}

/**
 * @brief Zyppロック検出をQMLへ通知する
 * @param lockerName ロック保持プロセス名
 * @param lockerPid ロック保持プロセスID
 * @param message バックエンドのエラーメッセージ
 */
void PackageController::handlePackageLocked(const QString &lockerName, int lockerPid, const QString &message)
{
    QVariantMap info;
    info[QStringLiteral("name")] = lockerName;
    info[QStringLiteral("pid")] = lockerPid;
    info[QStringLiteral("message")] = message;
    emit packageLocked(info);
}

/**
 * @brief 対話プロンプトへ応答し、キューから除去する
 * @param promptId 応答対象のプロンプトID
 * @param action 選択されたアクション名
 */
void PackageController::answerPrompt(uint promptId, const QString &action)
{
    if (!m_connected)
        return;

    // 現在のキューに存在するプロンプトにのみ回答する (破棄済み・旧バックエンドのIDは送らない)
    if (!removePrompt(promptId))
        return;

    QVariantMap answer;
    answer[QStringLiteral("action")] = action;
    m_dbus->answerPrompt(promptId, answer, nullptr);
}

/**
 * @brief 未応答のプロンプトを全て破棄する (バックエンド切断・再接続時)
 */
void PackageController::clearPrompts()
{
    if (m_prompts.isEmpty())
        return;
    m_prompts.clear();
    emit pendingPromptsChanged();
}

/**
 * @brief 指定IDのプロンプトをキューから除去する (存在しなければ何もしない)
 * @param promptId 除去するプロンプトID
 * @return キューに存在して除去した場合はtrue
 */
bool PackageController::removePrompt(uint promptId)
{
    QVariantList remaining;
    bool found = false;
    for (const QVariant &entry : std::as_const(m_prompts)) {
        const QVariantMap map = entry.toMap();
        if (!found && map.value(QStringLiteral("promptId")).toUInt() == promptId) {
            found = true;
            continue;
        }
        remaining.append(entry);
    }
    if (found) {
        m_prompts = remaining;
        emit pendingPromptsChanged();
    }
    return found;
}

/**
 * @brief 編集セッションを解放し、切断状態へ戻す
 *
 * 応答時またはタイムアウト時にsessionReleasedを発行する
 * その後はinitialize()により、再度セッションを開始できる
 */
void PackageController::releaseSession()
{
    if (!m_connected) {
        emit sessionReleased(false);
        return;
    }

    m_dbus->releaseSession([this](bool ok, const QString &, const QString &, const QVariantList &) {
        m_connected = false;
        m_initialized = false;
        m_prompts.clear();
        m_commitRevision = 0;
        ++m_repoPreviewRequest;
        emit connectedChanged();
        emit initializedChanged();
        emit pendingPromptsChanged();
        setStatusMessage(tr("Editing session ended"));
        emit sessionReleased(ok);
    });
}

/**
 * @brief Zyppロック等で失敗した直近の操作を再試行する
 */
void PackageController::retryOperation()
{
    if (m_retryAction)
        m_retryAction();
}

// -- 外部URL --

/**
 * @brief 外部URLをユーザセッションのWebブラウザで開く
 * @param url 開くURL (http / httpsのみ許可)
 * @note xdg-desktop-portalのOpenURIが失敗した場合は、QDesktopServicesにフォールバックする
 */
void PackageController::openExternalUrl(const QString &url)
{
    const QUrl target(url.trimmed());
    const QString scheme = target.scheme().toLower();
    if (!target.isValid() || target.host().isEmpty()
        || (scheme != QLatin1String("http") && scheme != QLatin1String("https"))) {
        qWarning() << "Refusing to open non-http(s) URL:" << url;
        return;
    }

    // UIはSELinuxのqzypper_tドメインで動作するため、
    // Webブラウザを子プロセスとしてexecせず、ユーザセッションのxdg-desktop-portalがURLを開く
    //
    // ポータルが無い環境のみ、従来のQDesktopServices (xdg-open) にフォールバックする
    QDBusMessage msg = QDBusMessage::createMethodCall(
        QStringLiteral("org.freedesktop.portal.Desktop"),
        QStringLiteral("/org/freedesktop/portal/desktop"),
        QStringLiteral("org.freedesktop.portal.OpenURI"),
        QStringLiteral("OpenURI"));
    msg << QString() << target.toString(QUrl::FullyEncoded) << QVariantMap();

    auto *watcher = new QDBusPendingCallWatcher(
        QDBusConnection::sessionBus().asyncCall(msg), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this,
            [target](QDBusPendingCallWatcher *w) {
        w->deleteLater();
        const QDBusPendingReply<QDBusObjectPath> reply = *w;
        if (reply.isError()) {
            qWarning() << "xdg-desktop-portal OpenURI failed:" << reply.error().message()
                       << "- falling back to QDesktopServices";
            QDesktopServices::openUrl(target);
        }
    });
}

} // namespace qZypper
