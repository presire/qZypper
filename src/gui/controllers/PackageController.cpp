#include <QDebug>
#include <QFile>
#include <unistd.h>
#include <fcntl.h>
#include <cstring>
#include <cerrno>
#include <QFileInfo>
#include <QUrl>
#include <QGuiApplication>
#include <QDesktopServices>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <utility>
#include "PackageController.h"
#include "DBusClient.h"
#include "PackageListModel.h"
#include "PackageFilterProxyModel.h"


namespace qZypper {

/**
 * @brief PackageController のコンストラクタ。
 *
 * DBusClient を初期化し、D-Busシグナルの中継を設定する。
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
        setStatusMessage(tr("Backend reconnected"));
        // 新しいバックエンドは prompt ID を 1 から再利用するため、旧プロンプトは破棄する
        clearPrompts();
        loadRepos();
        refreshPendingChanges();
    });
    connect(m_dbus, &DBusClient::backendDisconnected,
            this, [this]() {
        // 切断したバックエンドのプロンプトへの回答が別プロセスの同一IDへ届かないよう全破棄
        clearPrompts();
        if (m_connected) {
            m_connected = false;
            m_initialized = false;
            emit connectedChanged();
            emit initializedChanged();
            setStatusMessage(tr("Backend disconnected"));
        }
    });
}

/**
 * @brief デストラクタ。キャッシュデータをクリアする。
 */
PackageController::~PackageController()
{
    m_packages.clear();
    m_repos.clear();
    m_services.clear();
    m_patterns.clear();
    m_currentDetails.clear();
}

/**
 * @brief ビジー状態を設定する。
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
 * @brief ステータスメッセージを設定する。
 * @param msg 表示するメッセージ
 */
void PackageController::setStatusMessage(const QString &msg)
{
    if (m_statusMessage != msg) {
        m_statusMessage = msg;
        emit statusMessageChanged();
    }
}

// -- 初期化 --

/**
 * @brief パッケージマネージャを初期化する (非同期)。
 *
 * D-Busバックエンドに接続し、libzypp を非同期で初期化する。
 * 完了すると initializedChanged シグナルが発行され、成功時は
 * m_initialized が true になる。GUIスレッドはブロックしない。
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

// -- パッケージ検索・一覧 --

/**
 * @brief パッケージを検索する (非同期)。
 * @param query 検索クエリ文字列
 * @param flags 検索フラグ (SearchFlag のビットマスク)
 */
void PackageController::searchPackages(const QString &query, int flags)
{
    if (!m_connected) {
        emit errorOccurred(tr("Package manager is not initialized"));
        return;
    }
    if (m_busy) return;

    setBusy(true);
    setStatusMessage(tr("Searching packages..."));

    m_lastSource = PackageSource::Search;
    m_lastSourceParam = query;
    m_lastSearchFlags = flags;
    m_useExSearch = false;
    m_lastSearchMatch = QStringLiteral("substring");
    m_lastSearchInstalledOnly = false;
    setSearchError(QString());

    requestPackageList(true, [this](bool ok, const QVariantList &list) {
        setBusy(false);
        if (ok)
            setStatusMessage(tr("%1 packages found").arg(list.size()));
    });
}

/**
 * @brief 現在のソースに応じてパッケージ一覧を非同期で再取得する。
 *
 * リクエストシリアルを進め、古い応答は破棄する。要求の連打は
 * 最新の要求のみが適用されるため UI への反映が乱れない。
 * @param newGeneration 取得後に世代カウンタを進める場合 true
 * @param onDone 完了コールバック (成功可否と取得済みリスト)
 */
void PackageController::requestPackageList(bool newGeneration,
                                           std::function<void(bool, const QVariantList &)> onDone)
{
    ++m_listRequestSerial;
    const quint64 serial = m_listRequestSerial;

    auto apply = [this, serial, newGeneration, onDone](
                     bool ok, const QString &errorName, const QString &errorText, const QVariantList &args) {
        if (serial != m_listRequestSerial) {
            if (onDone) onDone(false, QVariantList());
            return;
        }
        if (!ok) {
            if (m_lastSource == PackageSource::Search
                && errorName.contains(QStringLiteral("InvalidArgs")))
                setSearchError(errorText);
            if (onDone) onDone(false, QVariantList());
            return;
        }
        setSearchError(QString());
        m_packages = args.isEmpty() ? QVariantList() : args.at(0).toList();
        m_packageModel->applyPackages(m_packages);
        if (newGeneration)
            ++m_listGeneration;
        emit packagesChanged();
        if (onDone) onDone(true, m_packages);
    };

    switch (m_lastSource) {
    case PackageSource::Search:
        if (m_useExSearch) {
            QVariantMap opts;
            opts[QStringLiteral("query")] = m_lastSourceParam;
            opts[QStringLiteral("flags")] = m_lastSearchFlags;
            opts[QStringLiteral("match")] = m_lastSearchMatch;
            opts[QStringLiteral("installedOnly")] = m_lastSearchInstalledOnly;
            m_dbus->searchPackagesEx(opts, apply);
        } else {
            m_dbus->searchPackages(m_lastSourceParam, m_lastSearchFlags, apply);
        }
        break;
    case PackageSource::Repo:
        m_dbus->getPackagesByRepo(m_lastSourceParam, apply);
        break;
    case PackageSource::Pattern:
        m_dbus->getPackagesByPattern(m_lastSourceParam, apply);
        break;
    case PackageSource::Patches:
        m_dbus->getPatches(m_lastSourceParam.toInt(), apply);
        break;
    case PackageSource::Classification:
        m_dbus->getPackagesByClassification(m_lastSourceParam, apply);
        break;
    case PackageSource::None:
        break;
    }
}

/**
 * @brief パッケージ一覧を現在のソースで再取得する (サイレント)。
 * @param newGeneration 取得後に世代カウンタを進める場合 true
 */
void PackageController::refreshPackageStatuses(bool newGeneration)
{
    requestPackageList(newGeneration, nullptr);
}

/**
 * @brief リポジトリ一覧を読み込む (非同期)。
 */
void PackageController::loadRepos()
{
    m_dbus->getRepos([this](bool ok, const QString &, const QString &, const QVariantList &args) {
        if (!ok)
            return;
        m_repos = args.isEmpty() ? QVariantList() : args.at(0).toList();
        emit reposChanged();
        qDebug() << "Loaded" << m_repos.size() << "repositories";
    });
}

/**
 * @brief 指定リポジトリのパッケージ一覧を読み込む (非同期)。
 * @param repoAlias リポジトリエイリアス
 */
void PackageController::loadPackagesByRepo(const QString &repoAlias)
{
    if (!m_connected) return;
    if (m_busy) return;

    setBusy(true);
    setStatusMessage(tr("Loading packages..."));

    m_lastSource = PackageSource::Repo;
    m_lastSourceParam = repoAlias;

    requestPackageList(true, [this](bool ok, const QVariantList &list) {
        setBusy(false);
        if (ok)
            setStatusMessage(tr("%1 packages loaded").arg(list.size()));
    });
}

/**
 * @brief パッケージの詳細情報を読み込む (非同期)。
 * @param name パッケージ名
 */
void PackageController::loadPackageDetails(const QString &name)
{
    if (!m_connected) return;

    ++m_detailsRequestSerial;
    const quint64 serial = m_detailsRequestSerial;

    m_dbus->getPackageDetails(name, [this, serial](
                                   bool ok, const QString &, const QString &, const QVariantList &args) {
        if (serial != m_detailsRequestSerial || !ok)
            return;
        m_currentDetails = args.isEmpty() ? QVariantMap() : args.at(0).toMap();
        emit currentPackageDetailsChanged();
    });
}

/**
 * @brief 選択中パッケージの詳細表示をクリアする。
 *
 * パッケージ一覧から選択中のパッケージが無くなった場合に使用する。
 * 保留中の詳細応答は無効化する。
 */
void PackageController::clearPackageDetails()
{
    ++m_detailsRequestSerial;
    if (m_currentDetails.isEmpty()) return;

    m_currentDetails.clear();
    emit currentPackageDetailsChanged();
}

/**
 * @brief パターン一覧を読み込む (非同期)。
 */
void PackageController::loadPatterns()
{
    if (!m_connected) return;

    m_dbus->getPatterns([this](bool ok, const QString &, const QString &, const QVariantList &args) {
        if (!ok)
            return;
        m_patterns = args.isEmpty() ? QVariantList() : args.at(0).toList();
        emit patternsChanged();
    });
}

/**
 * @brief 指定パターンに属するパッケージ一覧を読み込む (非同期)。
 * @param patternName パターン名
 */
void PackageController::loadPackagesByPattern(const QString &patternName)
{
    if (!m_connected) return;
    if (m_busy) return;

    setBusy(true);
    setStatusMessage(tr("Loading pattern packages..."));

    m_lastSource = PackageSource::Pattern;
    m_lastSourceParam = patternName;

    requestPackageList(true, [this](bool ok, const QVariantList &list) {
        setBusy(false);
        if (ok)
            setStatusMessage(tr("%1 packages loaded").arg(list.size()));
    });
}

/**
 * @brief パッチ一覧を読み込む (非同期)。
 * @param category パッチカテゴリ (PatchCategory)
 */
void PackageController::loadPatches(int category)
{
    if (!m_connected) return;

    // パッチはパッケージ一覧モデルとは別に保持する (パッケージ表へ混入させない)
    m_patchCategory = category;
    m_dbus->getPatches(category, [this, category](bool ok, const QString &, const QString &errorText,
                                                  const QVariantList &args) {
        if (category != m_patchCategory)
            return;   // 古い応答は破棄
        if (!ok) {
            emit errorOccurred(errorText);
            return;
        }
        m_patches = args.isEmpty() ? QVariantList() : args.at(0).toList();
        emit patchesChanged();
    });
}

/**
 * @brief 検索パラメータエラーを設定して通知する。
 * @param err エラー文言 (空文字列でクリア)
 */
void PackageController::setSearchError(const QString &err)
{
    if (m_searchError == err)
        return;
    m_searchError = err;
    emit searchErrorChanged();
}

/**
 * @brief ソルバー結果に含まれるトランザクション概要をキャッシュする。
 * @param result distUpgrade / updateAll / selectAll 等の結果マップ (summary キー込み)
 */
void PackageController::cacheTransactionSummary(const QVariantMap &result)
{
    const QVariantMap summary = result.value(QStringLiteral("summary")).toMap();
    if (summary.isEmpty())
        return;
    m_transactionSummary = summary;
    emit transactionSummaryChanged();
}

/**
 * @brief 高度なパッケージ検索を行う (非同期)。
 *
 * マッチ方式 (substring/exact/glob/regex) とインストール済み限定を指定できる。
 * 正則表現エラー等の InvalidArgs は searchError プロパティへ通知される。
 * @param query 検索クエリ文字列
 * @param flags 検索フラグ (SearchFlag のビットマスク)
 * @param match マッチ方式
 * @param installedOnly true でインストール済みパッケージのみ
 */
void PackageController::searchPackagesEx(const QString &query, int flags,
                                          const QString &match, bool installedOnly)
{
    if (!m_connected) {
        emit errorOccurred(tr("Package manager is not initialized"));
        return;
    }
    if (m_busy) return;

    setBusy(true);
    setStatusMessage(tr("Searching packages..."));

    m_lastSource = PackageSource::Search;
    m_lastSourceParam = query;
    m_lastSearchFlags = flags;
    m_lastSearchMatch = match;
    m_lastSearchInstalledOnly = installedOnly;
    m_useExSearch = true;
    setSearchError(QString());

    requestPackageList(true, [this](bool ok, const QVariantList &list) {
        setBusy(false);
        if (ok)
            setStatusMessage(tr("%1 packages found").arg(list.size()));
    });
}

/**
 * @brief 分類別パッケージ一覧を読み込む (非同期)。
 * @param classification 分類名 (recommended/suggested/orphaned/unneeded/multiversion/retracted/retractedInstalled/ptf/locked)
 */
void PackageController::loadClassification(const QString &classification)
{
    if (!m_connected) return;
    if (m_busy) return;

    setBusy(true);
    setStatusMessage(tr("Loading packages..."));

    m_lastSource = PackageSource::Classification;
    m_lastSourceParam = classification;
    m_useExSearch = false;

    requestPackageList(true, [this](bool ok, const QVariantList &list) {
        setBusy(false);
        if (ok)
            setStatusMessage(tr("%1 packages loaded").arg(list.size()));
    });
}

/**
 * @brief ディストリビューション更新 (dist-upgrade) を非同期で実行する。
 *
 * 結果は updateAllFinished と同様に distUpgradeFinished で通知され、
 * QML 側でコンフリクト/概要確認フローへ同じ経路で渡される。
 * @param allowVendorChange ベンダー変更を許可する場合 true
 */
void PackageController::distUpgrade(bool allowVendorChange)
{
    if (!m_connected) return;
    if (m_busy) return;

    setBusy(true);
    setStatusMessage(tr("Preparing distribution upgrade..."));

    QVariantMap options;
    options[QStringLiteral("allowVendorChange")] = allowVendorChange;

    m_dbus->distUpgrade(options, [this](bool ok, const QString &, const QString &, const QVariantList &args) {
        QVariantMap result = (ok && !args.isEmpty()) ? args.at(0).toMap() : QVariantMap();

        if (!m_dbus->isConnected()) {
            setStatusMessage(tr("Disconnected from backend. Reconnecting..."));
            reconnectBackend([this, result](bool reconnected) mutable {
                if (reconnected)
                    setStatusMessage(tr("Reconnected. Please try again"));
                else
                    setStatusMessage(tr("Failed to reconnect to backend"));
                setBusy(false);
                QVariantMap failed = result;
                failed[QStringLiteral("success")] = false;
                failed[QStringLiteral("backendCrashed")] = true;
                emit distUpgradeFinished(failed);
            });
            return;
        }

        refreshPackageStatuses(true);
        cacheTransactionSummary(result);

        if (result[QStringLiteral("success")].toBool())
            setStatusMessage(tr("Distribution upgrade check completed"));
        else
            setStatusMessage(tr("Dependency problems found"));

        setBusy(false);
        // 変更予定一覧の更新完了後に通知する (古い一覧で判定しないため)
        refreshPendingChangesThen([this, result](bool) { emit distUpgradeFinished(result); });
    });
}

/**
 * @brief システムの依存関係整合性検証 (zypper verify 相当) を非同期で実行する。
 *
 * 結果は verifyFinished で通知される (problems / toClean を含む)。
 */
void PackageController::verifySystem()
{
    if (!m_connected) return;
    if (m_busy) return;

    setBusy(true);
    setStatusMessage(tr("Verifying system integrity..."));

    m_dbus->verifySystem([this](bool ok, const QString &, const QString &errorText, const QVariantList &args) {
        setBusy(false);
        if (!ok) {
            emit errorOccurred(errorText.isEmpty() ? tr("Verify system failed") : errorText);
            return;
        }
        const QVariantMap result = args.isEmpty() ? QVariantMap() : args.at(0).toMap();
        refreshPackageStatuses(true);
        setStatusMessage(tr("System verification completed"));
        // 変更予定一覧の更新完了後に通知する (古い一覧で判定しないため)
        refreshPendingChangesThen([this, result](bool) { emit verifyFinished(result); });
    });
}

/**
 * @brief パッチの選択状態を変更する (非同期)。
 * @param patchId パッチ ID
 * @param status 新ステータス (Install=1 / NoInst=0)
 */
void PackageController::setPatchStatus(const QString &patchId, int status)
{
    if (!m_connected) return;

    m_dbus->setPatchStatus(patchId, status, [this](bool ok, const QString &, const QString &errorText, const QVariantList &) {
        if (!ok) {
            emit errorOccurred(errorText);
            return;
        }
        refreshPendingChanges();
        refreshPackageStatuses();
        loadPatches(m_patchCategory);
    });
}

/**
 * @brief 必要なパッチを一括選択する (非同期)。
 *
 * 対話型パッチは skipInteractive で自動スキップし、選択不可とする。
 * 結果は selectPatchesFinished で通知され、QML 側で概要確認フローへ渡す。
 * @param securityOnly セキュリティパッチのみ選択する場合 true
 * @param includeOptional オプションパッチも選択する場合 true
 */
void PackageController::selectAllNeededPatches(bool securityOnly, bool includeOptional)
{
    if (!m_connected) return;
    if (m_busy) return;

    setBusy(true);
    setStatusMessage(tr("Selecting needed patches..."));

    QVariantMap options;
    options[QStringLiteral("securityOnly")] = securityOnly;
    options[QStringLiteral("includeOptional")] = includeOptional;
    options[QStringLiteral("skipInteractive")] = true;

    m_dbus->selectAllNeededPatches(options, [this](bool ok, const QString &, const QString &errorText, const QVariantList &args) {
        setBusy(false);
        if (!ok) {
            emit errorOccurred(errorText);
            return;
        }
        QVariantMap result = args.isEmpty() ? QVariantMap() : args.at(0).toMap();
        refreshPackageStatuses(true);
        cacheTransactionSummary(result);
        loadPatches(m_patchCategory);
        // 変更予定一覧の更新完了後に通知する (古い一覧で判定しないため)
        refreshPendingChangesThen([this, result](bool) { emit selectPatchesFinished(result); });
    });
}

/**
 * @brief パッケージロック一覧を読み込む (非同期)。
 */
void PackageController::loadLocks()
{
    if (!m_connected) return;

    m_dbus->getLocks([this](bool ok, const QString &, const QString &errorText, const QVariantList &args) {
        if (!ok) {
            emit errorOccurred(errorText);
            return;
        }
        m_locks = args.isEmpty() ? QVariantList() : args.at(0).toList();
        emit locksChanged();
    });
}

/**
 * @brief パッケージロックを追加する (非同期)。
 * @param lock ロック定義マップ (name/kind/match/repoAlias/arch/version/comment)
 */
void PackageController::addLock(const QVariantMap &lock)
{
    if (!m_connected) return;

    m_dbus->addLock(lock, [this](bool ok, const QString &, const QString &errorText, const QVariantList &) {
        if (!ok)
            emit errorOccurred(errorText);
        emit lockFinished(ok);
        if (ok)
            loadLocks();
    });
}

/**
 * @brief パッケージロックを削除する (非同期)。
 * @param lock 削除対象のロック定義マップ
 */
void PackageController::removeLock(const QVariantMap &lock)
{
    if (!m_connected) return;

    m_dbus->removeLock(lock, [this](bool ok, const QString &, const QString &errorText, const QVariantList &) {
        if (!ok)
            emit errorOccurred(errorText);
        emit lockFinished(ok);
        if (ok)
            loadLocks();
    });
}

/**
 * @brief zypp インストール履歴を読み込む (非同期)。
 * @param filter フィルタマップ (since/until/limit/offset/actions)
 */
void PackageController::loadHistory(const QVariantMap &filter)
{
    if (!m_connected) return;

    m_dbus->getHistory(filter, [this](bool ok, const QString &, const QString &errorText, const QVariantList &args) {
        if (!ok) {
            emit errorOccurred(errorText);
            emit historyResultReady(QVariantList());
            return;
        }
        emit historyResultReady(args.isEmpty() ? QVariantList() : args.at(0).toList());
    });
}

/**
 * @brief サーバー側のトランザクション概要を取得する (非同期)。
 *
 * 取得結果は transactionSummary プロパティへキャッシュされる。
 */
void PackageController::requestTransactionSummary()
{
    if (!m_connected) return;

    m_dbus->getTransactionSummary([this](bool ok, const QString &, const QString &, const QVariantList &args) {
        // 取得失敗時も空マップで通知する (QML 側は概要不明として明示承認を要求する)
        m_transactionSummary = (ok && !args.isEmpty()) ? args.at(0).toMap() : QVariantMap();
        emit transactionSummaryChanged();
    });
}

/**
 * @brief ソルバーテストケースを作成してパスを通知する (非同期)。
 */
void PackageController::saveSolverTestcase()
{
    if (!m_connected) return;

    m_dbus->createSolverTestcase([this](bool ok, const QString &, const QString &errorText, const QVariantList &args) {
        if (!ok) {
            emit errorOccurred(errorText);
            return;
        }
        emit solverTestcaseCreated(args.isEmpty() ? QString() : args.at(0).toString());
    });
}

// -- 状態変更 --

/**
 * @brief パッケージのステータスを変更し、依存関係を非同期で解決する。
 *
 * 変更後はソルバーを実行し、成功時にパッケージ一覧と変更予定を再取得する。
 * @param name パッケージ名
 * @param status 新しいステータス (PackageStatus)
 */
void PackageController::setPackageStatus(const QString &name, int status)
{
    if (!m_connected) return;
    if (m_busy) return;

    setBusy(true);
    m_dbus->setPackageStatus(name, status, [this](
                                 bool ok, const QString &, const QString &, const QVariantList &) {
        if (!ok) {
            emit errorOccurred(m_dbus->lastError());
            setBusy(false);
            return;
        }
        m_dbus->resolveDependencies([this](bool rok, const QString &, const QString &, const QVariantList &) {
            if (!rok)
                emit errorOccurred(m_dbus->lastError());
            refreshPackageStatuses();
            refreshPendingChanges();
            setBusy(false);
        });
    });
}

/**
 * @brief パッケージのバージョン/アーキテクチャ/リポジトリを変更する (非同期)。
 *
 * 変更後はソルバーを実行し、成功時に一覧と変更予定を再取得する。
 * @param name パッケージ名
 * @param version バージョン
 * @param arch アーキテクチャ
 * @param repoAlias リポジトリアリアス
 */
void PackageController::setPackageVersion(const QString &name,
                                          const QString &version,
                                          const QString &arch,
                                          const QString &repoAlias)
{
    if (!m_connected) return;
    if (m_busy) return;

    setBusy(true);
    m_dbus->setPackageVersion(name, version, arch, repoAlias,
                              [this](bool ok, const QString &, const QString &, const QVariantList &) {
        if (!ok) {
            emit errorOccurred(m_dbus->lastError());
            setBusy(false);
            return;
        }
        m_dbus->resolveDependencies([this](bool rok, const QString &, const QString &, const QVariantList &) {
            if (!rok)
                emit errorOccurred(m_dbus->lastError());
            refreshPackageStatuses();
            refreshPendingChanges();
            setBusy(false);
        });
    });
}

/**
 * @brief パターンのステータスをトグルし、依存関係を非同期で解決する。
 *
 * YaSTのcycleStatus()と同じロジックでステータスを切り替え、ソルバーを実行する。
 * @param patternName パターン名
 */
void PackageController::togglePatternStatus(const QString &patternName)
{
    if (!m_connected) return;
    if (m_busy) return;

    // 現在のパターンステータスを取得
    int currentStatus = 0;
    for (const auto &p : std::as_const(m_patterns)) {
        auto map = p.toMap();
        if (map[QStringLiteral("name")].toString() == patternName) {
            currentStatus = map[QStringLiteral("status")].toInt();
            break;
        }
    }

    // YaSTのcycleStatus()と同じトグルロジック
    int newStatus;
    switch (currentStatus) {
        case 0:  // NoInst → Install
            newStatus = 1;
            break;
        case 1:  // Install → NoInst
            newStatus = 0;
            break;
        case 2:  // AutoInstall → NoInst
            newStatus = 0;
            break;
        case 3:  // KeepInstalled → Del
            newStatus = 6;
            break;
        case 6:  // Del → KeepInstalled
            newStatus = 3;
            break;
        default:
            return;
    }

    // ソースが別のパターンへ切り替わる場合は新しい世代として通知する
    const bool sourceChanged = m_lastSource != PackageSource::Pattern
                            || m_lastSourceParam != patternName;
    m_lastSource      = PackageSource::Pattern;
    m_lastSourceParam = patternName;

    setBusy(true);

    m_dbus->setPatternStatus(patternName, newStatus,
                             [this, patternName, newStatus, sourceChanged](
                                 bool ok, const QString &, const QString &, const QVariantList &) {
        if (!ok) {
            emit errorOccurred(m_dbus->lastError());
            setBusy(false);
            return;
        }

        // パターンリストのローカルステータスを即座に更新
        for (int i = 0; i < m_patterns.size(); ++i) {
            auto map = m_patterns[i].toMap();
            if (map[QStringLiteral("name")].toString() == patternName) {
                map[QStringLiteral("status")] = newStatus;
                m_patterns[i] = map;
                break;
            }
        }
        emit patternsChanged();

        // ソルバー実行 (fork保護済み — 依存パッケージの自動選択/衝突検出)
        m_dbus->resolveDependencies(
            [this, sourceChanged](bool rok, const QString &, const QString &, const QVariantList &rargs) {
            QVariantMap result = (rok && !rargs.isEmpty()) ? rargs.at(0).toMap() : QVariantMap();

            if (!m_dbus->isConnected()) {
                // バックエンドがクラッシュした場合、パッケージリストのみ再取得
                qWarning() << "Backend disconnected during pattern resolve";
                m_packages = QVariantList();
                if (sourceChanged)
                    ++m_listGeneration;
                emit packagesChanged();
                setBusy(false);
                return;
            }

            if (result.value(QStringLiteral("success")).toBool()) {
                // 成功 — 自動依存パッケージのステータスを含むリストを再取得
                refreshPackageStatuses(sourceChanged);
            } else {
                // 衝突あり — リストを再取得してからコンフリクトダイアログを表示
                refreshPackageStatuses(sourceChanged);
                auto problems = result.value(QStringLiteral("problems")).toList();
                if (!problems.isEmpty())
                    emit conflictsDetected(problems);
            }

            // パターン一覧もソルバー結果を反映して再取得
            loadPatterns();
            refreshPendingChanges();
            setBusy(false);
        });
    });
}

/**
 * @brief 全パッケージを非同期で更新する。
 *
 * 完了すると updateAllFinished シグナルで結果を通知する。
 * バックエンドがクラッシュした場合は再接続を試み、
 * backendCrashed=true を含む結果を通知する。
 */
void PackageController::updateAllPackages()
{
    if (!m_connected) return;
    if (m_busy) return;

    setBusy(true);
    setStatusMessage(tr("Updating all packages..."));

    m_dbus->updateAllPackages([this](bool ok, const QString &, const QString &, const QVariantList &args) {
        QVariantMap result = (ok && !args.isEmpty()) ? args.at(0).toMap() : QVariantMap();

        // バックエンドがクラッシュした場合の復旧
        if (!m_dbus->isConnected()) {
            setStatusMessage(tr("Disconnected from backend. Reconnecting..."));
            reconnectBackend([this, result](bool reconnected) mutable {
                if (reconnected)
                    setStatusMessage(tr("Reconnected. Please try again"));
                else
                    setStatusMessage(tr("Failed to reconnect to backend"));
                setBusy(false);
                QVariantMap failed = result;
                failed[QStringLiteral("success")] = false;
                failed[QStringLiteral("backendCrashed")] = true;
                emit updateAllFinished(failed);
            });
            return;
        }

        refreshPackageStatuses(true);
        cacheTransactionSummary(result);

        if (result[QStringLiteral("success")].toBool())
            setStatusMessage(tr("Update check completed"));
        else
            setStatusMessage(tr("Dependency problems found"));

        setBusy(false);
        // 変更予定一覧の更新完了後に通知する (古い一覧で判定しないため)
        refreshPendingChangesThen([this, result](bool) { emit updateAllFinished(result); });
    });
}

/**
 * @brief バックエンド切断時に再接続と再初期化を非同期で試みる。
 * @param onDone 完了コールバック (再接続・初期化成功時 true)
 */
void PackageController::reconnectBackend(std::function<void(bool)> onDone)
{
    clearPrompts();  // 旧バックエンドの prompt ID は新プロセスで再利用されるため破棄
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
 * @brief 依存関係を非同期で解決する。
 *
 * 完了すると resolveFinished シグナルで結果を通知する。
 * 成功時にパッケージ一覧と変更予定を再取得する。
 */
void PackageController::resolveDependencies()
{
    if (!m_connected) return;
    if (m_busy) return;

    setBusy(true);
    setStatusMessage(tr("Resolving dependencies..."));

    m_dbus->resolveDependencies([this](bool ok, const QString &, const QString &, const QVariantList &args) {
        QVariantMap result = (ok && !args.isEmpty()) ? args.at(0).toMap() : QVariantMap();

        // バックエンドがクラッシュした場合の復旧
        if (!m_dbus->isConnected()) {
            setStatusMessage(tr("Disconnected from backend. Reconnecting..."));
            reconnectBackend([this, result](bool reconnected) mutable {
                if (reconnected)
                    setStatusMessage(tr("Reconnected. Please try again"));
                else
                    setStatusMessage(tr("Failed to reconnect to backend"));
                setBusy(false);
                QVariantMap failed = result;
                failed[QStringLiteral("success")] = false;
                failed[QStringLiteral("backendCrashed")] = true;
                emit resolveFinished(failed);
            });
            return;
        }

        if (result[QStringLiteral("success")].toBool()) {
            setStatusMessage(tr("Dependencies resolved"));
            refreshPackageStatuses();
        } else {
            setStatusMessage(tr("Dependency problems found"));
        }
        setBusy(false);
        // 変更予定一覧の更新完了後に通知する (古い一覧で判定しないため)
        refreshPendingChangesThen([this, result](bool) { emit resolveFinished(result); });
    });
}

/**
 * @brief 依存関係の衝突に対する解決策を非同期で適用する。
 * @param problemIndex 問題のインデックス
 * @param solutionIndex 解決策のインデックス
 */
void PackageController::applySolution(int problemIndex, int solutionIndex)
{
    if (!m_connected) return;
    if (m_busy) return;

    m_dbus->applySolution(problemIndex, solutionIndex,
                          [this](bool ok, const QString &, const QString &, const QVariantList &args) {
        if (!ok || args.isEmpty() || !args.at(0).toBool())
            emit errorOccurred(m_dbus->lastError());
    });
}

/**
 * @brief 確認済み選択に Commit を束縛するためのリビジョンを非同期で取得する。
 *
 * 取得したリビジョンは commitAsync() で消費される。
 * 完了すると commitPrepared シグナルで成功可否を通知する。
 */
void PackageController::prepareCommit()
{
    if (!m_connected) {
        m_commitRevision = 0;
        emit commitPrepared(false);
        return;
    }
    fetchCommitRevision(3);
}

/**
 * @brief 確認画面に表示する変更予定とコミット revision を同じ選択状態に揃える。
 *
 * revision 取得 → 変更予定取得 → revision 再取得を行い、前後の revision が
 * 一致した場合のみ確定する。不一致 (途中で選択が変わった) なら再試行する。
 * @param attemptsLeft 残り試行回数
 */
void PackageController::fetchCommitRevision(int attemptsLeft)
{
    auto parseRevision = [](bool ok, const QVariantList &args, quint64 &rev) {
        if (!ok || args.isEmpty())
            return false;
        bool conv = false;
        rev = args.at(0).toULongLong(&conv);
        return conv;
    };
    auto fail = [this]() {
        m_commitRevision = 0;
        emit errorOccurred(m_dbus->lastError().isEmpty()
                               ? tr("Failed to obtain a consistent selection state")
                               : m_dbus->lastError());
        emit commitPrepared(false);
    };

    m_dbus->getSelectionRevision([this, attemptsLeft, parseRevision, fail](
                                     bool ok, const QString &, const QString &, const QVariantList &args) {
        quint64 before = 0;
        if (!parseRevision(ok, args, before)) {
            fail();
            return;
        }
        refreshPendingChangesThen([this, attemptsLeft, before, parseRevision, fail](bool refreshed) {
            // 変更予定を取得できなければ確認画面の一覧が古いままなので確認を中止する
            if (!refreshed || !m_connected) {
                fail();
                return;
            }
            m_dbus->getSelectionRevision([this, attemptsLeft, before, parseRevision, fail](
                                             bool ok2, const QString &, const QString &, const QVariantList &args2) {
                quint64 after = 0;
                if (!parseRevision(ok2, args2, after)) {
                    fail();
                    return;
                }
                if (after != before) {
                    if (attemptsLeft > 1)
                        fetchCommitRevision(attemptsLeft - 1);
                    else
                        fail();
                    return;
                }
                m_commitRevision = after;
                emit commitPrepared(true);
            });
        });
    });
}

/**
 * @brief パッケージの変更を非同期でコミットする。
 *
 * m_commitRevision に束縛して Commit を呼び、呼び出し後は0に戻す。
 * リビジョン未確定時は進捗ダイアログ整合のため失敗結果を設定して終了する。
 * 結果は commitResultChanged シグナルで通知される。
 */
void PackageController::commitAsync()
{
    if (!m_connected) return;
    if (m_busy) return;
    m_retryAction = [this]() { prepareCommit(); };

    if (m_commitRevision == 0) {
        const QString msg = tr("Changes must be confirmed before applying");
        emit errorOccurred(msg);
        m_commitResult = QVariantMap{
            {QStringLiteral("success"), false},
            {QStringLiteral("errorMessage"), msg},
        };
        emit commitResultChanged();
        setBusy(false);
        return;
    }

    setBusy(true);
    setStatusMessage(tr("Applying changes..."));
    const quint64 rev = m_commitRevision;
    m_commitRevision = 0;

    m_dbus->commitAsync(rev, [this](bool ok, const QString &, const QString &, const QVariantList &args) {
        m_commitResult = (ok && !args.isEmpty()) ? args.at(0).toMap() : QVariantMap();
        if (!m_commitResult.contains(QStringLiteral("success")))
            m_commitResult[QStringLiteral("success")] = false;
        emit commitResultChanged();

        if (m_commitResult[QStringLiteral("success")].toBool())
            setStatusMessage(tr("Changes applied successfully"));
        else
            setStatusMessage(tr("Some operations encountered errors"));

        // コミット後は一覧の内容が大きく変わるため、新しい世代として通知する
        refreshPackageStatuses(true);
        loadPatterns();
        refreshPendingChanges();
        setBusy(false);
    });
}

/**
 * @brief GPG署名鍵の指紋を非同期で承認する (鍵の取込は操作の再試行時)。
 *
 * 完了すると keyTrustFinished シグナルで成功可否を通知する。
 * @param fingerprint 鍵フィンガープリント
 */
void PackageController::trustKey(const QString &fingerprint)
{
    if (!m_connected) {
        emit keyTrustFinished(false);
        return;
    }
    if (m_busy) {
        emit keyTrustFinished(false);
        return;
    }

    m_dbus->trustKey(fingerprint, [this](bool ok, const QString &, const QString &, const QVariantList &args) {
        const bool success = ok && !args.isEmpty() && args.at(0).toBool();
        if (success)
            setStatusMessage(tr("Signing key approved. It will be imported when you retry the operation."));
        else
            emit errorOccurred(m_dbus->lastError());
        emit keyTrustFinished(success);
    });
}

/**
 * @brief 実行中の操作をキャンセルする。
 */
void PackageController::cancelOperation()
{
    if (!m_connected) return;
    m_dbus->cancelOperation();
    setStatusMessage(tr("Cancelling..."));
}

// -- リポジトリ管理 --

/**
 * @brief リポジトリを非同期で追加する。
 *
 * 完了すると addRepoFinished シグナルで成功可否を通知する。
 * @param url リポジトリURL
 * @param name リポジトリ名
 */
void PackageController::addRepo(const QString &url, const QString &name)
{
    if (!m_connected) {
        emit addRepoFinished(false);
        return;
    }
    if (m_busy) {
        emit addRepoFinished(false);
        return;
    }

    m_dbus->addRepo(url, name, [this](bool ok, const QString &, const QString &, const QVariantList &args) {
        const bool success = ok && !args.isEmpty() && args.at(0).toMap().value(QStringLiteral("success")).toBool();
        if (success)
            loadRepos();
        else
            emit errorOccurred(m_dbus->lastError());
        emit addRepoFinished(success);
    });
}

/**
 * @brief 全プロパティを指定してリポジトリを非同期で追加する。
 *
 * 完了すると addRepoFinished シグナルで成功可否を通知する。
 * @param properties リポジトリプロパティ
 */
void PackageController::addRepoFull(const QVariantMap &properties)
{
    if (!m_connected) {
        emit addRepoFinished(false);
        return;
    }
    if (m_busy) {
        emit addRepoFinished(false);
        return;
    }

    setBusy(true);
    setStatusMessage(tr("Adding repository..."));

    m_dbus->addRepoFull(properties,
                        [this, properties](bool ok, const QString &, const QString &, const QVariantList &args) {
        const QVariantMap result = (ok && !args.isEmpty()) ? args.at(0).toMap() : QVariantMap();
        const bool success = result.value(QStringLiteral("success")).toBool();

        if (success) {
            loadRepos();
            setStatusMessage(tr("Repository added: %1").arg(properties.value(QStringLiteral("name")).toString()));
        } else {
            setStatusMessage(tr("Failed to add repository"));
            emit errorOccurred(m_dbus->lastError());
        }

        setBusy(false);
        emit addRepoFinished(success);
    });
}

/**
 * @brief リポジトリを非同期で削除する。
 * @param alias 削除対象のリポジトリエイリアス
 */
void PackageController::removeRepo(const QString &alias)
{
    if (!m_connected) return;
    if (m_busy) return;

    m_dbus->removeRepo(alias, [this](bool ok, const QString &, const QString &, const QVariantList &args) {
        const bool success = ok && !args.isEmpty() && args.at(0).toBool();
        if (success)
            loadRepos();
        else
            emit errorOccurred(m_dbus->lastError());
    });
}

/**
 * @brief リポジトリの有効/無効を非同期で切り替える。
 * @param alias 対象のリポジトリエイリアス
 * @param enabled 有効にする場合 true
 */
void PackageController::setRepoEnabled(const QString &alias, bool enabled)
{
    if (!m_connected) return;
    if (m_busy) return;

    m_dbus->setRepoEnabled(alias, enabled, [this](bool ok, const QString &, const QString &, const QVariantList &args) {
        const bool success = ok && !args.isEmpty() && args.at(0).toBool();
        if (success)
            loadRepos();
        else
            emit errorOccurred(m_dbus->lastError());
    });
}

/**
 * @brief リポジトリのプロパティを非同期で変更する。
 * @param alias 対象のリポジトリエイリアス
 * @param properties 変更するプロパティ
 */
void PackageController::modifyRepo(const QString &alias, const QVariantMap &properties)
{
    if (!m_connected) return;
    if (m_busy) return;

    m_dbus->modifyRepo(alias, properties, [this](bool ok, const QString &, const QString &, const QVariantList &args) {
        const bool success = ok && !args.isEmpty() && args.at(0).toBool();
        if (success)
            loadRepos();
        else
            emit errorOccurred(m_dbus->lastError());
    });
}

/**
 * @brief 全リポジトリの非同期リフレッシュを実行する。
 *
 * 完了時に refreshFinished シグナルが発行される。
 */
void PackageController::refreshReposAsync()
{
    if (!m_connected) return;
    if (m_busy) return;

    m_retryAction = [this]() { refreshReposAsync(); };
    setBusy(true);
    setStatusMessage(tr("Refreshing repositories..."));

    m_dbus->refreshReposAsync([this](bool ok, const QString &errorName, const QString &, const QVariantList &args) {
        const bool success = ok && !args.isEmpty() && args.at(0).toBool();
        const bool cancelled = !ok && errorName == QLatin1String("org.presire.qzypper.Error.Cancelled");

        if (success) {
            setStatusMessage(tr("Repository refresh completed"));
            loadRepos();
        } else if (cancelled) {
            setStatusMessage(tr("Refresh cancelled"));
        } else {
            setStatusMessage(tr("Refresh failed"));
            emit errorOccurred(m_dbus->lastError());
        }
        setBusy(false);
        emit refreshFinished(success);
    });
}

/**
 * @brief リフレッシュ操作をキャンセルする。
 */
void PackageController::cancelRefresh()
{
    m_dbus->cancelOperation();
    setStatusMessage(tr("Cancelling... Please wait"));
}

/**
 * @brief 指定リポジトリを非同期でリフレッシュする。
 * @param alias リフレッシュ対象のリポジトリエイリアス
 */
void PackageController::refreshSingleRepo(const QString &alias)
{
    if (!m_connected) return;
    if (m_busy) return;

    setBusy(true);
    setStatusMessage(tr("Refreshing repository: %1").arg(alias));

    m_dbus->refreshSingleRepoAsync(alias, [this](bool ok, const QString &, const QString &, const QVariantList &args) {
        const bool success = ok && !args.isEmpty() && args.at(0).toBool();
        if (success) {
            setStatusMessage(tr("Repository refresh completed"));
            loadRepos();
        } else {
            setStatusMessage(tr("Refresh failed"));
            emit errorOccurred(m_dbus->lastError());
        }
        setBusy(false);
        emit refreshFinished(success);
    });
}

// -- サービス管理 --

/**
 * @brief サービス一覧を読み込む (非同期)。
 */
void PackageController::loadServices()
{
    m_dbus->getServices([this](bool ok, const QString &, const QString &, const QVariantList &args) {
        if (!ok)
            return;
        m_services = args.isEmpty() ? QVariantList() : args.at(0).toList();
        emit servicesChanged();
    });
}

/**
 * @brief サービスを非同期で追加する。
 *
 * 完了すると addServiceFinished シグナルで成功可否を通知する。
 * @param url サービスURL
 * @param alias サービスエイリアス
 */
void PackageController::addService(const QString &url, const QString &alias)
{
    if (!m_connected) {
        emit addServiceFinished(false);
        return;
    }
    if (m_busy) {
        emit addServiceFinished(false);
        return;
    }

    setBusy(true);
    setStatusMessage(tr("Adding service..."));

    m_dbus->addService(url, alias, [this, alias](bool ok, const QString &, const QString &, const QVariantList &args) {
        const bool success = ok && !args.isEmpty() && args.at(0).toBool();

        if (success) {
            loadServices();
            loadRepos();
            setStatusMessage(tr("Service added: %1").arg(alias));
        } else {
            setStatusMessage(tr("Failed to add service"));
            emit errorOccurred(m_dbus->lastError());
        }

        setBusy(false);
        emit addServiceFinished(success);
    });
}

/**
 * @brief サービスを非同期で削除する。
 * @param alias 削除対象のサービスエイリアス
 */
void PackageController::removeService(const QString &alias)
{
    if (!m_connected) return;
    if (m_busy) return;

    m_dbus->removeService(alias, [this](bool ok, const QString &, const QString &, const QVariantList &args) {
        const bool success = ok && !args.isEmpty() && args.at(0).toBool();
        if (success) {
            loadServices();
            loadRepos();
        } else {
            emit errorOccurred(m_dbus->lastError());
        }
    });
}

/**
 * @brief サービスのプロパティを非同期で変更する。
 * @param alias 対象のサービスエイリアス
 * @param properties 変更するプロパティ
 */
void PackageController::modifyService(const QString &alias, const QVariantMap &properties)
{
    if (!m_connected) return;
    if (m_busy) return;

    m_dbus->modifyService(alias, properties, [this](bool ok, const QString &, const QString &, const QVariantList &args) {
        const bool success = ok && !args.isEmpty() && args.at(0).toBool();
        if (success)
            loadServices();
        else
            emit errorOccurred(m_dbus->lastError());
    });
}

/**
 * @brief サービスを非同期でリフレッシュする。
 * @param alias リフレッシュ対象のサービスエイリアス
 */
void PackageController::refreshService(const QString &alias)
{
    if (!m_connected) return;
    if (m_busy) return;

    setBusy(true);
    setStatusMessage(tr("Refreshing service: %1").arg(alias));

    m_dbus->refreshServiceAsync(alias, [this](bool ok, const QString &, const QString &, const QVariantList &args) {
        const bool success = ok && !args.isEmpty() && args.at(0).toBool();
        if (success) {
            loadServices();
            loadRepos();
            setStatusMessage(tr("Service refreshed"));
        } else {
            setStatusMessage(tr("Failed to refresh service"));
            emit errorOccurred(m_dbus->lastError());
        }
        setBusy(false);
        emit refreshFinished(success);
    });
}

// -- 状態保存・復元 --

/**
 * @brief 変更予定のパッケージ一覧を非同期で再取得する (キャッシュ更新)。
 *
 * 実行中に再度呼ばれた場合は合体 (coalesce) し、二重要求を抑止する。
 * hasPendingChanges は本処理でのみ更新され、ゲッターは I/O を行わない。
 */
void PackageController::refreshPendingChanges()
{
    if (!m_connected) {
        m_pendingRefreshInFlight = false;
        m_pendingRefreshQueued = false;
        if (!m_pendingChanges.isEmpty() || m_hasPendingChanges) {
            m_pendingChanges.clear();
            m_hasPendingChanges = false;
            emit pendingChangesChanged();
        }
        auto waiters = std::move(m_pendingRefreshWaiters);
        m_pendingRefreshWaiters.clear();
        for (auto &waiter : waiters)
            waiter(false);
        return;
    }

    if (m_pendingRefreshInFlight) {
        m_pendingRefreshQueued = true;
        return;
    }

    m_pendingRefreshInFlight = true;
    m_dbus->getPendingChanges([this](bool ok, const QString &, const QString &, const QVariantList &args) {
        m_pendingRefreshInFlight = false;

        if (ok) {
            m_pendingChanges = args.isEmpty() ? QVariantList() : args.at(0).toList();
            m_hasPendingChanges = !m_pendingChanges.isEmpty();
            emit pendingChangesChanged();
        }

        if (m_pendingRefreshQueued) {
            m_pendingRefreshQueued = false;
            refreshPendingChanges();
            return;
        }

        // 最新の要求まで反映済み — 待機者へ取得成否を通知
        auto waiters = std::move(m_pendingRefreshWaiters);
        m_pendingRefreshWaiters.clear();
        for (auto &waiter : waiters)
            waiter(ok);
    });
}

/**
 * @brief 変更予定を再取得し、呼出し時点以降の状態が反映された後に onDone を呼ぶ。
 *
 * 取得中の要求は呼出し前の選択状態を返す可能性があるため、その場合は
 * 再取得を予約し、予約分の完了まで待機する。
 * @param onDone 完了時に呼ばれるコールバック (引数は取得成否。失敗・切断時は false)
 */
void PackageController::refreshPendingChangesThen(std::function<void(bool)> onDone)
{
    if (onDone)
        m_pendingRefreshWaiters.push_back(std::move(onDone));
    if (m_pendingRefreshInFlight)
        m_pendingRefreshQueued = true;
    else
        refreshPendingChanges();
}

/**
 * @brief ディスク使用量を非同期で再取得する (キャッシュ更新)。
 *
 * 完了すると diskUsageChanged シグナルが発行される。
 */
void PackageController::requestDiskUsage()
{
    if (!m_connected) return;

    m_dbus->getDiskUsage([this](bool ok, const QString &, const QString &, const QVariantList &args) {
        if (!ok)
            return;
        m_diskUsage = args.isEmpty() ? QVariantList() : args.at(0).toList();
        emit diskUsageChanged();
    });
}

/**
 * @brief パッケージの選択状態を保存する (非同期・fire-and-forget)。
 */
void PackageController::saveState()
{
    if (m_busy) return;
    m_dbus->saveState();
}

/**
 * @brief 保存された選択状態を非同期で復元し、パッケージリストを再取得する。
 */
void PackageController::restoreState()
{
    if (m_busy) return;

    m_dbus->restoreState([this](bool ok, const QString &, const QString &, const QVariantList &) {
        if (!ok && !m_dbus->lastError().isEmpty())
            emit errorOccurred(m_dbus->lastError());
        refreshPackageStatuses();
        refreshPendingChanges();
    });
}

/**
 * @brief ダークモード/ライトモードのパレットを構築して適用する。
 *
 * Qt 6.5 では QML の palette グループ化プロパティのバインディングが
 * 動的変更を子コントロールに正しく伝播しないため、
 * C++ 側で QPalette を構築して QGuiApplication::setPalette() で適用する。
 * @param dark true ならダークモード、false ならライトモード
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

// -- 操作状態 / プロンプト / セッション (Wave1) --

/**
 * @brief パッケージ一覧モデルを返す (QML公開用)。
 * @return PackageListModel への QObject ポインタ
 */
QObject *PackageController::packageModel() const
{
    return m_packageModel;
}

/**
 * @brief フィルタ/ソートプロキシモデルを返す (QML公開用)。
 * @return PackageFilterProxyModel への QObject ポインタ
 */
QObject *PackageController::packageProxy() const
{
    return m_packageProxy;
}

/**
 * @brief 先頭の未応答プロンプトを返す。
 * @return プロンプトマップ (キューが空なら空マップ)
 */
QVariantMap PackageController::currentPrompt() const
{
    return m_prompts.isEmpty() ? QVariantMap() : m_prompts.first().toMap();
}

/**
 * @brief 操作状態を更新し、変更があれば operationStateChanged を発行する。
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
 * @brief 対話プロンプトをキューへ追加し、QMLへ通知する。
 * @param promptId プロンプトID
 * @param type プロンプト種別
 * @param data プロンプトデータ
 */
void PackageController::enqueuePrompt(uint promptId, const QString &type, const QVariantMap &data)
{
    // prompt ID はバックエンドプロセス内で単調増加する。既存以下の ID が届いた場合は
    // 切断通知より先に新プロセスから届いたものと判断し、旧世代のプロンプトを破棄する。
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
 * @brief Zyppロック検出をQMLへ通知する。
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
 * @brief 対話プロンプトへ応答し、キューから除去する。
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
 * @brief 未応答のプロンプトを全て破棄する (バックエンド切断・再接続時)。
 */
void PackageController::clearPrompts()
{
    if (m_prompts.isEmpty())
        return;
    m_prompts.clear();
    emit pendingPromptsChanged();
}

/**
 * @brief 指定IDのプロンプトをキューから除去する (存在しなければ何もしない)。
 * @param promptId 除去するプロンプトID
 * @return キューに存在して除去した場合 true
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
 * @brief 編集セッションを解放し、切断状態へ戻す。
 *
 * 応答時またはタイムアウト時に sessionReleased を発行する。
 * その後は initialize() により再度セッションを開始できる。
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
        emit connectedChanged();
        emit initializedChanged();
        emit pendingPromptsChanged();
        setStatusMessage(tr("Editing session ended"));
        emit sessionReleased(ok);
    });
}

/**
 * @brief Zyppロック等で失敗した直近の操作を再試行する。
 */
void PackageController::retryOperation()
{
    if (m_retryAction)
        m_retryAction();
}


/**
 * @brief ローカルRPMファイルをインストール対象として選択し、依存関係を解決する。
 *
 * 結果は localRpmsFinished で通知される (success / problems / summary / unsignedPackages)。
 * @param fileUrls ファイルダイアログで選択された file:// URL の一覧
 */
void PackageController::installLocalRpms(const QVariantList &fileUrls)
{
    if (!m_connected) return;
    if (m_busy) return;

    // ファイルはユーザー権限で GUI が開き、記述子だけをバックエンドへ渡す
    // (root プロセスにユーザー指定パスを開かせない)。O_NONBLOCK で FIFO による停止を防ぐ。
    QList<QDBusUnixFileDescriptor> files;
    QStringList fileNames;
    for (const QVariant &value : fileUrls) {
        const QUrl url = value.toUrl();
        const QString path = url.isLocalFile() ? url.toLocalFile() : value.toString();
        if (path.isEmpty())
            continue;
        const int fd = ::open(QFile::encodeName(path).constData(), O_RDONLY | O_CLOEXEC | O_NONBLOCK);
        if (fd < 0) {
            QVariantMap failed;
            failed[QStringLiteral("success")] = false;
            failed[QStringLiteral("errorMessage")] =
                tr("Cannot open file: %1").arg(path) + QStringLiteral(" (") + QString::fromLocal8Bit(std::strerror(errno)) + QStringLiteral(")");
            emit localRpmsFinished(failed);
            return;
        }
        files.append(QDBusUnixFileDescriptor(fd));   // 記述子は複製される
        ::close(fd);
        fileNames.append(QFileInfo(path).fileName());
    }
    if (files.isEmpty())
        return;

    setBusy(true);
    setStatusMessage(tr("Adding local RPM files..."));

    m_dbus->installLocalRpms(files, fileNames, [this](bool ok, const QString &, const QString &message,
                                           const QVariantList &args) {
        QVariantMap result = (ok && !args.isEmpty()) ? args.at(0).toMap() : QVariantMap();
        if (!ok) {
            result[QStringLiteral("success")] = false;
            result[QStringLiteral("errorMessage")] = message;
        }

        refreshPackageStatuses(true);
        cacheTransactionSummary(result);

        if (result.value(QStringLiteral("success")).toBool())
            setStatusMessage(tr("Local RPM files selected for installation"));
        else if (!ok)
            setStatusMessage(tr("Failed to add local RPM files"));
        else
            setStatusMessage(tr("Dependency problems found"));

        setBusy(false);
        // 変更予定一覧の更新完了後に通知する (古い一覧で判定しないため)
        refreshPendingChangesThen([this, result](bool) { emit localRpmsFinished(result); });
    });
}

/**
 * @brief .repo ファイルを読み込み、バックエンドで解析してプレビューを通知する。
 *
 * ファイルはユーザ権限で GUI 側が読み込み、内容のみをバックエンドへ渡す
 * (root プロセスにユーザ指定パスを開かせないため)。
 * @param fileUrl 選択された .repo ファイルの URL
 */
void PackageController::previewRepoFile(const QUrl &fileUrl)
{
    if (!m_connected) return;

    constexpr qint64 kMaxRepoFileSize = 64 * 1024;
    QFile file(fileUrl.isLocalFile() ? fileUrl.toLocalFile() : fileUrl.toString());
    if (!file.open(QIODevice::ReadOnly)) {
        emit repoFilePreviewReady(false, {}, QString(), tr("Cannot open file: %1").arg(file.errorString()));
        return;
    }
    if (file.size() > kMaxRepoFileSize) {
        emit repoFilePreviewReady(false, {}, QString(), tr("The file is too large (maximum 64 KiB)"));
        return;
    }
    const QString content = QString::fromUtf8(file.readAll());

    m_dbus->parseRepoFile(content, [this, content](bool ok, const QString &, const QString &message,
                                                   const QVariantList &args) {
        const QVariantList repos = (ok && !args.isEmpty()) ? args.at(0).toList() : QVariantList();
        if (!ok)
            emit repoFilePreviewReady(false, {}, content, message);
        else if (repos.isEmpty())
            emit repoFilePreviewReady(false, {}, content, tr("No valid repository definitions were found"));
        else
            emit repoFilePreviewReady(true, repos, content, QString());
    });
}

/**
 * @brief プレビュー済みの .repo 内容を取り込み、リポジトリを追加する。
 * @param content previewRepoFile で読み込んだ .repo の内容
 */
void PackageController::importRepoFile(const QString &content)
{
    if (!m_connected) return;
    if (m_busy) return;

    setBusy(true);
    setStatusMessage(tr("Importing repositories..."));

    m_dbus->importRepoFile(content, [this](bool ok, const QString &, const QString &message,
                                           const QVariantList &args) {
        QVariantMap result = (ok && !args.isEmpty()) ? args.at(0).toMap() : QVariantMap();
        if (!ok) {
            result[QStringLiteral("success")] = false;
            result[QStringLiteral("errorMessage")] = message;
        }
        setBusy(false);
        if (!result.value(QStringLiteral("added")).toList().isEmpty())
            loadRepos();
        setStatusMessage(result.value(QStringLiteral("success")).toBool()
                             ? tr("Repositories imported")
                             : tr("Repository import failed"));
        emit repoFileImported(result);
    });
}

void PackageController::openExternalUrl(const QString &url)
{
    const QUrl target(url.trimmed());
    const QString scheme = target.scheme().toLower();
    if (!target.isValid() || target.host().isEmpty()
        || (scheme != QLatin1String("http") && scheme != QLatin1String("https"))) {
        qWarning() << "Refusing to open non-http(s) URL:" << url;
        return;
    }

    // GUI は SELinux の qzypper_t ドメインで動作するため、ブラウザを子プロセスとして
    // exec せず、ユーザセッションの xdg-desktop-portal に URL を開かせる。
    // ポータルが無い環境のみ従来の QDesktopServices (xdg-open) にフォールバックする。
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
