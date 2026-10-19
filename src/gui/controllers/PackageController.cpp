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
        refreshUpdateCount();
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
        // 切断後の遅延処理が死んだバックエンドへ要求しないよう、先に接続状態と保留を無効化する
        const bool wasConnected = m_connected;
        m_connected = false;
        m_initialized = false;
        m_startupPending = false;
        m_updateCountPending = false;
        if (wasConnected) {
            emit connectedChanged();
            emit initializedChanged();
        }
        // 旧セッションの一覧要求と一括操作の完了通知を二重に発行しないよう先に無効化する
        ++m_batchGeneration;
        abortListRequests();
        // 切断したバックエンドのプロンプトへの回答が別プロセスの同一IDへ届かないよう全破棄
        clearPrompts();
        m_commitRevision = 0;
        // 旧セッションで開始した.repoプレビューの結果を、再接続後に表示しない
        ++m_repoPreviewRequest;
        // 実行中の一括操作と起動時処理は結果だけを収集し、通知はbusy解除後に行う
        const QVariantMap batchResult = takeAbortedBatchResult();
        bool startupRefreshRan = false;
        const bool startupFinished = takeStartupFinish(m_startupGeneration, startupRefreshRan);
        setStatusMessage(tr("Backend disconnected"));
        // 破棄されたGetPendingChangesの応答では取得中フラグが解除されないため、
        // 未接続として取得状態とキャッシュを初期化し、待機者へ失敗を通知する
        refreshPendingChanges();
        // バックエンド消滅後は実行中操作の応答が来ないためビジー状態を1回だけ解除する
        setBusy(false);
        // 通知先が新しい操作を開始してもbusyを解放しないよう、完了通知は最後に発行する
        if (startupFinished && startupRefreshRan) {
            emit refreshFinished(false);
        }
        if (!batchResult.isEmpty()) {
            emit packagesActionFinished(batchResult);
        }
        if (startupFinished) {
            emit startupTasksFinished();
        }
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
    if (m_busy == busy) {
        return;
    }

    m_busy = busy;
    emit busyChanged();

    if (busy) {
        return;
    }
    // 通知先が新たな操作を開始した場合は、その操作の完了時に改めて保留分を処理する
    if (m_busy || !m_connected) {
        return;
    }
    // 起動時処理の開始待ちがあればbusy解除直後に開始する
    if (m_startupPending && !m_startupRunning) {
        m_startupPending = false;
        startStartupTasks();
        return;
    }
    // ビジー中に要求された更新可能数の取得をここで一度だけ実行する
    if (m_updateCountPending) {
        qDebug() << "[PackageController] running deferred refreshUpdateCount";
        requestUpdateCount();
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

// -- 起動時処理 --

/**
 * @brief 起動時処理を開始する
 * @param installRecommends installRecommendsの指定 (-1は変更しない、0/1は設定値)
 * @param refreshOnStartup trueでリポジトリのリフレッシュを行う
 *
 * ビジー中の場合は開始を保留しbusy解除後に一度だけ実行する
 * 未接続の場合は処理を行わずstartupTasksFinishedのみ発行する
 */
void PackageController::runStartupTasks(int installRecommends, bool refreshOnStartup)
{
    // 実行中の世代が参照する引数を壊さないよう、開始待ちの引数として保持する
    m_startupPendingInstallRecommends = installRecommends;
    m_startupPendingRefreshOnStartup = refreshOnStartup;

    if (!m_connected) {
        emit startupTasksFinished();
        return;
    }
    if (m_busy || m_startupRunning) {
        m_startupPending = true;
        qDebug() << "[PackageController] runStartupTasks deferred (busy)";
        return;
    }

    startStartupTasks();
}

/**
 * @brief 起動時処理の先頭段階を開始してbusyを保持する
 *
 * 以降の段階はコールバックで直列に実行し途中でbusyを解除しない
 * 各段階の失敗はerrorOccurredで通知し処理は継続する
 */
void PackageController::startStartupTasks()
{
    // この世代の引数を開始時点で確定し、実行中の再要求は次の世代へ回す
    m_startupInstallRecommends = m_startupPendingInstallRecommends;
    m_startupRefreshOnStartup = m_startupPendingRefreshOnStartup;
    m_startupPendingInstallRecommends = -1;
    m_startupPendingRefreshOnStartup = false;
    m_startupRunning = true;
    ++m_startupGeneration;
    m_startupRefreshStageRan = false;
    setBusy(true);
    setStatusMessage(tr("Loading package manager settings..."));

    runStartupSolverStage(m_startupGeneration);
}

/**
 * @brief 起動時処理のソルバー設定段階を実行する
 * @param generation 起動時処理の世代
 *
 * GetSolverSettingsで現行設定を取得しsolverSettingsへ反映する
 * installRecommendsの指定がある場合は設定変更と依存解決を行い設定を再取得する
 */
void PackageController::runStartupSolverStage(quint64 generation)
{
    m_dbus->getSolverSettings([this, generation](bool ok, const QString &,
                                                  const QString &errorText, const QVariantList &args) {
        if (!m_startupRunning || generation != m_startupGeneration) {
            return;
        }
        if (!ok) {
            emit errorOccurred(errorText.isEmpty() ? tr("Failed to load solver settings") : errorText);
        } else {
            m_solverSettings = args.isEmpty() ? QVariantMap() : args.at(0).toMap();
            emit solverSettingsChanged();
        }
        if (!m_connected) {
            finishStartupTasks(generation, false);
            return;
        }
        if (m_startupInstallRecommends < 0) {
            runStartupRefreshStage(generation);
            return;
        }

        QVariantMap settings;
        settings[QStringLiteral("installRecommends")] = (m_startupInstallRecommends == 1);
        // 指定されたinstallRecommendsを適用し失敗しても起動処理は継続する
        m_dbus->setSolverSettings(settings, [this, generation](bool setOk, const QString &,
                                                                const QString &setError, const QVariantList &) {
            if (!m_startupRunning || generation != m_startupGeneration) {
                return;
            }
            if (!setOk) {
                emit errorOccurred(setError.isEmpty() ? tr("Failed to update solver settings") : setError);
            }
            if (!m_connected) {
                finishStartupTasks(generation, false);
                return;
            }
            m_dbus->resolveDependencies([this, generation](bool rok, const QString &,
                                                            const QString &resolveError,
                                                            const QVariantList &resolveArgs) {
                if (!m_startupRunning || generation != m_startupGeneration) {
                    return;
                }
                if (!rok) {
                    emit errorOccurred(resolveError.isEmpty()
                                           ? tr("Dependency resolution failed") : resolveError);
                } else {
                    const QVariantMap result = resolveArgs.isEmpty()
                                                   ? QVariantMap() : resolveArgs.at(0).toMap();
                    if (!applySolverResult(result)) {
                        emit errorOccurred(tr("Dependency resolution failed"));
                    }
                }
                if (!m_connected) {
                    finishStartupTasks(generation, false);
                    return;
                }
                // 適用後の設定を再取得してから次の段階へ進む
                m_dbus->getSolverSettings([this, generation](bool getOk, const QString &,
                                                              const QString &getError, const QVariantList &getArgs) {
                    if (!m_startupRunning || generation != m_startupGeneration) {
                        return;
                    }
                    if (getOk) {
                        m_solverSettings = getArgs.isEmpty() ? QVariantMap() : getArgs.at(0).toMap();
                        emit solverSettingsChanged();
                    } else {
                        emit errorOccurred(getError.isEmpty() ? tr("Failed to load solver settings") : getError);
                    }
                    runStartupRefreshStage(generation);
                });
            });
        });
    });
}

/**
 * @brief 起動時処理のリフレッシュ段階を実行する
 * @param generation 起動時処理の世代
 *
 * リフレッシュ指定がある場合のみRefreshReposIfNeededを呼び出し成功時にloadReposする
 * リフレッシュの成否は保持して完了時にrefreshFinishedへ渡す
 */
void PackageController::runStartupRefreshStage(quint64 generation)
{
    if (!m_startupRunning || generation != m_startupGeneration) {
        return;
    }
    if (!m_startupRefreshOnStartup) {
        finishStartupUpdatableStage(generation, true);
        return;
    }

    m_startupRefreshStageRan = true;
    m_dbus->refreshReposIfNeeded([this, generation](bool ok, const QString &, const QString &errorText,
                                                     const QVariantList &args) {
        if (!m_startupRunning || generation != m_startupGeneration) {
            return;
        }
        const bool success = ok && !args.isEmpty() && args.at(0).toBool();
        if (!success) {
            emit errorOccurred(errorText.isEmpty() ? tr("Repository refresh failed") : errorText);
        } else {
            setStatusMessage(tr("Repository refresh completed"));
            loadRepos();
        }
        if (!m_connected) {
            finishStartupTasks(generation, false);
            return;
        }
        finishStartupUpdatableStage(generation, success);
    });
}

/**
 * @brief 起動時処理の更新可能数取得段階を実行する
 * @param generation 起動時処理の世代
 * @param refreshOk リフレッシュ段階の成否
 *
 * 更新可能数のみを取得してupdateCountへ反映し一覧モデルには触れない
 * 取得後にfinishStartupTasksを一度だけ呼ぶ
 */
void PackageController::finishStartupUpdatableStage(quint64 generation, bool refreshOk)
{
    if (!m_startupRunning || generation != m_startupGeneration) {
        return;
    }
    if (!m_connected) {
        finishStartupTasks(generation, false);
        return;
    }

    // この取得が起動中に要求された更新可能数の取得を兼ねるため、保留をここで消費する
    m_updateCountPending = false;
    m_dbus->getPackagesByClassification(QStringLiteral("updatable"),
                                        [this, generation, refreshOk](bool ok, const QString &errorName,
                                                                       const QString &, const QVariantList &args) {
        if (!m_startupRunning || generation != m_startupGeneration) {
            return;
        }
        applyUpdateCountResult(ok, errorName, args);
        finishStartupTasks(generation, refreshOk);
    });
}

/**
 * @brief 起動時処理の完了状態を収集して世代を進める
 * @param generation 起動時処理の世代
 * @param refreshRan リフレッシュ段階を実行した場合はtrueを受け取る
 * @return 完了処理を実行した場合はtrue
 *
 * busy解除とシグナル発行は行わず、世代を進めて遅延応答だけを無効化する
 */
bool PackageController::takeStartupFinish(quint64 generation, bool &refreshRan)
{
    if (!m_startupRunning || generation != m_startupGeneration) {
        return false;
    }

    m_startupRunning = false;
    ++m_startupGeneration;
    refreshRan = m_startupRefreshStageRan;
    m_startupRefreshStageRan = false;
    return true;
}

/**
 * @brief 起動時処理を1回だけ完了させる
 * @param generation 起動時処理の世代
 * @param refreshOk リフレッシュ段階の成否
 *
 * リフレッシュ段階を実行した場合のみrefreshFinishedを発行する
 * 完了時はbusyを解除しstartupTasksFinishedを発行する
 * 世代を進めることで遅延して届く応答を無視する
 */
void PackageController::finishStartupTasks(quint64 generation, bool refreshOk)
{
    bool refreshRan = false;
    if (!takeStartupFinish(generation, refreshRan)) {
        return;
    }
    if (refreshRan) {
        emit refreshFinished(refreshOk);
    }
    setBusy(false);
    emit startupTasksFinished();
}

/**
 * @brief 起動時処理を中断する
 *
 * 実行中の場合のみ完了処理を一度だけ実行する
 * 世代を進めるため中断後に届いた応答は無視される
 */
void PackageController::abortStartupTasks()
{
    if (!m_startupRunning) {
        return;
    }

    finishStartupTasks(m_startupGeneration, false);
}

} // namespace qZypper
