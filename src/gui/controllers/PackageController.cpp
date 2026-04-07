#include <QDebug>
#include <QCoreApplication>
#include <QGuiApplication>
#include <PolkitQt1/Authority>
#include <PolkitQt1/Subject>
#include "PackageController.h"
#include "DBusClient.h"


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

    // D-Busシグナルを中継
    connect(m_dbus, &DBusClient::repoRefreshProgress,
            this, [this](const QString &alias, int pct) {
        if (pct >= 100)
            setStatusMessage(tr("Repository refresh completed"));
        else
            setStatusMessage(tr("Refreshing: %1 (%2%)").arg(alias).arg(pct));
    });
    connect(m_dbus, &DBusClient::refreshReposFinished,
            this, [this](bool success) {
        if (success) {
            setStatusMessage(tr("Repository refresh completed"));
            loadRepos();
        } else {
            setStatusMessage(tr("Refresh cancelled"));
        }
        setBusy(false);
        emit refreshFinished(success);
    });
    connect(m_dbus, &DBusClient::progressChanged,
            this, &PackageController::progressChanged);
    connect(m_dbus, &DBusClient::commitProgressChanged,
            this, &PackageController::commitProgressChanged);
    connect(m_dbus, &DBusClient::commitFinished,
            this, [this](const QVariantMap &result) {
        m_commitResult = result;
        emit commitResultChanged();

        if (result["success"].toBool())
            setStatusMessage(tr("Changes applied successfully"));
        else
            setStatusMessage(tr("Some operations encountered errors"));

        setBusy(false);
    });
    connect(m_dbus, &DBusClient::transactionFinished,
            this, &PackageController::transactionFinished);
    connect(m_dbus, &DBusClient::errorOccurred,
            this, &PackageController::errorOccurred);

    // バックエンドが再起動した場合、接続状態と一覧を復元する
    connect(m_dbus, &DBusClient::backendReconnected,
            this, [this]() {
        m_connected = true;
        emit connectedChanged();
        setStatusMessage(tr("Backend reconnected"));
        loadRepos();
    });
    connect(m_dbus, &DBusClient::backendDisconnected,
            this, [this]() {
        if (m_connected) {
            m_connected = false;
            emit connectedChanged();
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
 * @brief パッケージマネージャを初期化する。
 *
 * D-Busバックエンドに接続し、libzypp を初期化する。
 * @return 初期化成功時 true
 */
bool PackageController::initialize()
{
    setBusy(true);
    setStatusMessage(tr("Initializing package manager..."));

    if (!m_dbus->connectToBackend()) {
        m_connected = false;
        emit connectedChanged();
        QString err = m_dbus->lastError();
        setStatusMessage(tr("Failed to connect to backend: %1").arg(err));
        emit errorOccurred(err);
        qWarning() << "D-Bus backend connection failed:" << err;
        setBusy(false);
        return false;
    }

    if (!m_dbus->initializeBackend()) {
        m_connected = false;
        emit connectedChanged();
        QString err = m_dbus->lastError();
        setStatusMessage(tr("Failed to initialize backend: %1").arg(err));
        emit errorOccurred(err);
        qWarning() << "D-Bus backend initialization failed:" << err;
        setBusy(false);
        return false;
    }

    m_connected = true;
    emit connectedChanged();
    setStatusMessage(tr("Ready"));
    qDebug() << "D-Bus backend initialized successfully";
    setBusy(false);
    return true;
}

// -- パッケージ検索・一覧 --

/**
 * @brief パッケージを検索する。
 * @param query 検索クエリ文字列
 * @param flags 検索フラグ (SearchFlag のビットマスク)
 */
void PackageController::searchPackages(const QString &query, int flags)
{
    if (!m_connected) {
        emit errorOccurred(tr("Package manager is not initialized"));
        return;
    }

    setBusy(true);
    setStatusMessage(tr("Searching packages..."));

    m_lastSource = PackageSource::Search;
    m_lastSourceParam = query;
    m_lastSearchFlags = flags;

    m_packages = m_dbus->searchPackages(query, flags);

    emit packagesChanged();
    setStatusMessage(tr("%1 packages found").arg(m_packages.size()));
    setBusy(false);
}

/**
 * @brief リポジトリ一覧を読み込む。
 */
void PackageController::loadRepos()
{
    m_repos = m_dbus->getRepos();
    emit reposChanged();
    qDebug() << "Loaded" << m_repos.size() << "repositories";
}

/**
 * @brief 指定リポジトリのパッケージ一覧を読み込む。
 * @param repoAlias リポジトリエイリアス
 */
void PackageController::loadPackagesByRepo(const QString &repoAlias)
{
    if (!m_connected) return;

    setBusy(true);
    setStatusMessage(tr("Loading packages..."));

    m_lastSource = PackageSource::Repo;
    m_lastSourceParam = repoAlias;

    m_packages = m_dbus->getPackagesByRepo(repoAlias);

    emit packagesChanged();
    setStatusMessage(tr("%1 packages loaded").arg(m_packages.size()));
    setBusy(false);
}

/**
 * @brief パッケージの詳細情報を読み込む。
 * @param name パッケージ名
 */
void PackageController::loadPackageDetails(const QString &name)
{
    if (!m_connected) return;

    m_currentDetails = m_dbus->getPackageDetails(name);
    emit currentPackageDetailsChanged();
}

/**
 * @brief パターン一覧を読み込む。
 */
void PackageController::loadPatterns()
{
    if (!m_connected) return;

    m_patterns = m_dbus->getPatterns();
    emit patternsChanged();
}

/**
 * @brief 指定パターンに属するパッケージ一覧を読み込む。
 * @param patternName パターン名
 */
void PackageController::loadPackagesByPattern(const QString &patternName)
{
    if (!m_connected) return;

    setBusy(true);
    setStatusMessage(tr("Loading pattern packages..."));

    m_lastSource = PackageSource::Pattern;
    m_lastSourceParam = patternName;

    m_packages = m_dbus->getPackagesByPattern(patternName);

    emit packagesChanged();
    setStatusMessage(tr("%1 packages loaded").arg(m_packages.size()));
    setBusy(false);
}

/**
 * @brief パッチ一覧を読み込む。
 * @param category パッチカテゴリ (PatchCategory)
 */
void PackageController::loadPatches(int category)
{
    if (!m_connected) return;

    m_lastSource = PackageSource::Patches;
    m_lastSourceParam = QString::number(category);

    m_packages = m_dbus->getPatches(category);
    emit packagesChanged();
}

// -- 状態変更 --

/**
 * @brief パッケージのステータスを変更し、依存関係を解決する。
 *
 * ステータス変更後にソルバーを実行し、衝突がある場合は conflictsDetected を発行する。
 * @param name パッケージ名
 * @param status 新しいステータス (PackageStatus)
 * @return 変更成功時 true
 */
bool PackageController::setPackageStatus(const QString &name, int status)
{
    if (!m_connected) return false;

    setBusy(true);
    setStatusMessage(tr("Resolving dependencies..."));

    if (!m_dbus->setPackageStatus(name, status)) {
        setBusy(false);
        setStatusMessage({});
        return false;
    }

    // ソルバー実行 (fork保護済み — 依存パッケージの自動選択/衝突検出)
    auto result = m_dbus->resolveDependencies();

    if (!m_dbus->isConnected()) {
        // バックエンドがクラッシュした場合、ローカルのみ更新
        qWarning() << "Backend disconnected during resolve";
        for (int i = 0; i < m_packages.size(); ++i) {
            auto map = m_packages[i].toMap();
            if (map["name"].toString() == name) {
                map["status"] = status;
                m_packages[i] = map;
                break;
            }
        }
        emit packagesChanged();
        setBusy(false);
        return true;
    }

    if (result.value("success").toBool()) {
        // 成功 — 自動依存パッケージのステータスを含むリストを再取得
        refreshPackageStatuses();
        setStatusMessage(tr("Dependencies resolved"));
    } else {
        // 衝突あり — リストを再取得してからコンフリクトダイアログを表示
        refreshPackageStatuses();
        setStatusMessage(tr("Dependency problems found"));
        auto problems = result.value("problems").toList();
        if (!problems.isEmpty()) {
            emit conflictsDetected(problems);
        }
    }

    setBusy(false);
    return true;
}

/**
 * @brief パッケージのバージョンを変更し、依存関係を解決する。
 *
 * 指定バージョンを候補に設定し、インストール済みパッケージは S_Update に変更する。
 * ソルバー実行後にパッケージリストとパッケージ詳細を再取得する。
 * @param name パッケージ名
 * @param version 選択バージョン (edition 文字列)
 * @param arch アーキテクチャ
 * @param repoAlias リポジトリエイリアス
 * @return 変更成功時 true
 */
bool PackageController::setPackageVersion(const QString &name, const QString &version,
                                          const QString &arch, const QString &repoAlias)
{
    if (!m_connected) return false;

    setBusy(true);
    setStatusMessage(tr("Changing version..."));

    if (!m_dbus->setPackageVersion(name, version, arch, repoAlias)) {
        setBusy(false);
        setStatusMessage({});
        return false;
    }

    // ソルバー実行
    auto result = m_dbus->resolveDependencies();

    if (!m_dbus->isConnected()) {
        qWarning() << "Backend disconnected during resolve";
        emit packagesChanged();
        setBusy(false);
        return true;
    }

    if (result.value("success").toBool()) {
        refreshPackageStatuses();
        loadPackageDetails(name);
        setStatusMessage(tr("Version changed"));
    } else {
        refreshPackageStatuses();
        loadPackageDetails(name);
        setStatusMessage(tr("Dependency problems found"));
        auto problems = result.value("problems").toList();
        if (!problems.isEmpty()) {
            emit conflictsDetected(problems);
        }
    }

    setBusy(false);
    return true;
}

/**
 * @brief 現在のソースに基づいてパッケージリストを再取得する。
 *
 * ソルバー実行後、自動ステータスを反映するために使用する。
 */
void PackageController::refreshPackageStatuses()
{
    switch (m_lastSource) {
    case PackageSource::Search:
        m_packages = m_dbus->searchPackages(m_lastSourceParam, m_lastSearchFlags);
        break;
    case PackageSource::Repo:
        m_packages = m_dbus->getPackagesByRepo(m_lastSourceParam);
        break;
    case PackageSource::Pattern:
        m_packages = m_dbus->getPackagesByPattern(m_lastSourceParam);
        break;
    case PackageSource::Patches:
        m_packages = m_dbus->getPatches(m_lastSourceParam.toInt());
        break;
    default:
        return;
    }
    emit packagesChanged();
}

/**
 * @brief パターンのステータスをトグルする。
 *
 * YaSTのcycleStatus()と同じロジックでステータスを切り替え、ソルバーを実行して依存関係を解決する。
 * @param patternName パターン名
 * @return 変更成功時 true
 */
bool PackageController::togglePatternStatus(const QString &patternName)
{
    if (!m_connected) return false;

    // 現在のパターンステータスを取得
    int currentStatus = 0;
    for (const auto &p : std::as_const(m_patterns)) {
        auto map = p.toMap();
        if (map["name"].toString() == patternName) {
            currentStatus = map["status"].toInt();
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
            return false;
    }

    // パターンステータスを変更
    if (!m_dbus->setPatternStatus(patternName, newStatus))
        return false;

    // パターンリストのローカルステータスを即座に更新
    for (int i = 0; i < m_patterns.size(); ++i) {
        auto map = m_patterns[i].toMap();
        if (map["name"].toString() == patternName) {
            map["status"] = newStatus;
            m_patterns[i] = map;
            break;
        }
    }
    emit patternsChanged();

    // ソースを更新 (refreshPackageStatuses で使用)
    m_lastSource      = PackageSource::Pattern;
    m_lastSourceParam = patternName;

    // ソルバー実行 (fork保護済み — 依存パッケージの自動選択/衝突検出)
    auto result = m_dbus->resolveDependencies();

    if (!m_dbus->isConnected()) {
        // バックエンドがクラッシュした場合、パッケージリストのみ再取得
        qWarning() << "Backend disconnected during pattern resolve";
        m_packages = {};
        emit packagesChanged();
        return true;
    }

    if (result.value("success").toBool()) {
        // 成功 — 自動依存パッケージのステータスを含むリストを再取得
        refreshPackageStatuses();
    } else {
        // 衝突あり — リストを再取得してからコンフリクトダイアログを表示
        refreshPackageStatuses();
        auto problems = result.value("problems").toList();
        if (!problems.isEmpty()) {
            emit conflictsDetected(problems);
        }
    }

    // パターン一覧もソルバー結果を反映して再取得
    m_patterns = m_dbus->getPatterns();
    emit patternsChanged();

    return true;
}

/**
 * @brief 全パッケージの更新を実行する。
 *
 * バックエンドの doUpdate() を呼び出し、全更新可能パッケージをマークして依存解決を行う。
 * バックエンドがクラッシュした場合は自動再接続を試みる。
 * @return 解決結果 (success, problems, backendCrashed)
 */
QVariantMap PackageController::updateAllPackages()
{
    if (!m_connected) return {};

    setBusy(true);
    setStatusMessage(tr("Updating all packages..."));

    QVariantMap result = m_dbus->updateAllPackages();

    // バックエンドがクラッシュした場合の復旧
    if (!m_dbus->isConnected()) {
        setStatusMessage(tr("Disconnected from backend. Reconnecting..."));
        if (m_dbus->connectToBackend() && m_dbus->initializeBackend()) {
            m_connected = true;
            emit connectedChanged();
            setStatusMessage(tr("Reconnected. Please try again"));
        } else {
            m_connected = false;
            emit connectedChanged();
            setStatusMessage(tr("Failed to reconnect to backend"));
        }
        setBusy(false);
        result["success"] = false;
        result["backendCrashed"] = true;
        return result;
    }

    // ソルバー実行後、パッケージリストを再読み込みしてステータスを反映
    refreshPackageStatuses();

    if (result["success"].toBool()) {
        setStatusMessage(tr("Update check completed"));
    } else {
        setStatusMessage(tr("Dependency problems found"));
    }

    setBusy(false);
    return result;
}

/**
 * @brief 依存関係を解決する。
 *
 * バックエンドがクラッシュした場合は自動再接続を試みる。
 * @return 解決結果 (success, problems, backendCrashed)
 */
QVariantMap PackageController::resolveDependencies()
{
    if (!m_connected) return {};

    setBusy(true);
    setStatusMessage(tr("Resolving dependencies..."));

    QVariantMap result = m_dbus->resolveDependencies();

    // バックエンドがクラッシュした場合の復旧
    if (!m_dbus->isConnected()) {
        setStatusMessage(tr("Disconnected from backend. Reconnecting..."));
        // D-Bus activation により自動再起動される — 再初期化を試みる
        if (m_dbus->connectToBackend() && m_dbus->initializeBackend()) {
            m_connected = true;
            emit connectedChanged();
            setStatusMessage(tr("Reconnected. Please try again"));
        } else {
            m_connected = false;
            emit connectedChanged();
            setStatusMessage(tr("Failed to reconnect to backend"));
        }
        setBusy(false);
        result["success"] = false;
        result["backendCrashed"] = true;
        return result;
    }

    if (result["success"].toBool()) {
        setStatusMessage(tr("Dependencies resolved"));
        // ソルバー実行後、パッケージリストを再読み込みして自動ステータスを反映
        refreshPackageStatuses();
    } else {
        setStatusMessage(tr("Dependency problems found"));
    }

    setBusy(false);
    return result;
}

/**
 * @brief 依存関係の衝突に対する解決策を適用する。
 * @param problemIndex 問題のインデックス
 * @param solutionIndex 解決策のインデックス
 * @return 適用成功時 true
 */
bool PackageController::applySolution(int problemIndex, int solutionIndex)
{
    if (!m_connected) return false;
    return m_dbus->applySolution(problemIndex, solutionIndex);
}

/**
 * @brief パッケージの変更をコミットする。
 * @return コミット結果 (success 等)
 */
QVariantMap PackageController::commit()
{
    if (!m_connected) return {};

    setBusy(true);
    setStatusMessage(tr("Applying changes..."));

    QVariantMap result = m_dbus->commit();

    if (result["success"].toBool())
        setStatusMessage(tr("Changes applied successfully"));
    else
        setStatusMessage(tr("Some operations encountered errors"));

    setBusy(false);
    return result;
}

/**
 * @brief パッケージの変更を非同期でコミットする。
 *
 * GUIスレッドをブロックしないため、進捗シグナルをリアルタイムで受信できる。
 * 結果は commitResultChanged シグナルと transactionFinished シグナルで通知される。
 */
void PackageController::commitAsync()
{
    if (!m_connected) return;

    setBusy(true);
    setStatusMessage(tr("Applying changes..."));
    m_dbus->commitAsync();
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
 * @brief リポジトリを追加する。
 * @param url リポジトリURL
 * @param name リポジトリ名
 * @return 追加成功時 true
 */
bool PackageController::addRepo(const QString &url, const QString &name)
{
    if (!m_connected) return false;

    auto result = m_dbus->addRepo(url, name);
    bool ok = result["success"].toBool();
    if (ok)
        loadRepos();
    else
        emit errorOccurred(result["errorMessage"].toString());
    return ok;
}

/**
 * @brief 全プロパティを指定してリポジトリを追加する。
 * @param properties リポジトリプロパティ
 * @return 追加成功時 true
 */
bool PackageController::addRepoFull(const QVariantMap &properties)
{
    if (!m_connected) return false;

    setBusy(true);
    setStatusMessage(tr("Adding repository..."));

    auto result = m_dbus->addRepoFull(properties);
    bool ok = result["success"].toBool();

    if (ok) {
        loadRepos();
        setStatusMessage(tr("Repository added: %1").arg(properties["name"].toString()));
    } else {
        setStatusMessage(tr("Failed to add repository"));
        emit errorOccurred(result["errorMessage"].toString());
    }

    setBusy(false);
    return ok;
}

/**
 * @brief リポジトリを削除する。
 * @param alias 削除対象のリポジトリエイリアス
 * @return 削除成功時 true
 */
bool PackageController::removeRepo(const QString &alias)
{
    if (!m_connected) return false;

    bool ok = m_dbus->removeRepo(alias);
    if (ok)
        loadRepos();
    else
        emit errorOccurred(m_dbus->lastError());
    return ok;
}

/**
 * @brief リポジトリの有効/無効を切り替える。
 * @param alias 対象のリポジトリエイリアス
 * @param enabled 有効にする場合 true
 * @return 変更成功時 true
 */
bool PackageController::setRepoEnabled(const QString &alias, bool enabled)
{
    if (!m_connected) return false;

    bool ok = m_dbus->setRepoEnabled(alias, enabled);
    if (ok) loadRepos();
    return ok;
}

/**
 * @brief リポジトリのプロパティを変更する。
 * @param alias 対象のリポジトリエイリアス
 * @param properties 変更するプロパティ
 * @return 変更成功時 true
 */
bool PackageController::modifyRepo(const QString &alias, const QVariantMap &properties)
{
    if (!m_connected) return false;

    bool ok = m_dbus->modifyRepo(alias, properties);
    if (ok)
        loadRepos();
    else
        emit errorOccurred(m_dbus->lastError());
    return ok;
}

/**
 * @brief 全リポジトリの同期リフレッシュを実行する。
 * @return リフレッシュ成功時 true
 */
bool PackageController::refreshRepos()
{
    if (!m_connected) return false;

    setBusy(true);
    setStatusMessage(tr("Refreshing repositories..."));

    bool ok = m_dbus->refreshRepos();

    if (ok) {
        setStatusMessage(tr("Repositories refreshed"));
        loadRepos();
    } else {
        setStatusMessage(tr("Refresh failed"));
        emit errorOccurred(m_dbus->lastError());
    }

    setBusy(false);
    return ok;
}

/**
 * @brief 全リポジトリの非同期リフレッシュを実行する。
 *
 * 完了時に refreshFinished シグナルが発行される。
 */
void PackageController::refreshReposAsync()
{
    if (!m_connected) return;

    setBusy(true);
    setStatusMessage(tr("Refreshing repositories..."));
    m_dbus->refreshReposAsync();
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
 * @brief 指定リポジトリを個別にリフレッシュする。
 * @param alias リフレッシュ対象のリポジトリエイリアス
 * @return リフレッシュ成功時 true
 */
bool PackageController::refreshSingleRepo(const QString &alias)
{
    if (!m_connected) return false;

    setBusy(true);
    setStatusMessage(tr("Refreshing repository: %1").arg(alias));

    bool ok = m_dbus->refreshSingleRepo(alias);

    if (ok) {
        setStatusMessage(tr("Refresh completed: %1").arg(alias));
        loadRepos();
    } else {
        setStatusMessage(tr("Refresh failed"));
        emit errorOccurred(m_dbus->lastError());
    }

    setBusy(false);
    return ok;
}

/**
 * @brief URL からリポジトリタイプを検出する (未実装)。
 * @param url 検出対象のURL
 * @return リポジトリタイプ文字列 (現在は空)
 */
QString PackageController::probeRepoType(const QString &url)
{
    if (!m_connected) return {};
    // TODO: バックエンドにProbeRepoTypeメソッドを追加
    Q_UNUSED(url)
    return {};
}

// -- サービス管理 --

/**
 * @brief サービス一覧を読み込む。
 */
void PackageController::loadServices()
{
    m_services = m_dbus->getServices();
    emit servicesChanged();
}

/**
 * @brief サービスを追加する。
 * @param url サービスURL
 * @param alias サービスエイリアス
 * @return 追加成功時 true
 */
bool PackageController::addService(const QString &url, const QString &alias)
{
    if (!m_connected) return false;

    setBusy(true);
    setStatusMessage(tr("Adding service..."));

    bool ok = m_dbus->addService(url, alias);

    if (ok) {
        loadServices();
        loadRepos();
        setStatusMessage(tr("Service added: %1").arg(alias));
    } else {
        setStatusMessage(tr("Failed to add service"));
        emit errorOccurred(m_dbus->lastError());
    }

    setBusy(false);
    return ok;
}

/**
 * @brief サービスを削除する。
 * @param alias 削除対象のサービスエイリアス
 * @return 削除成功時 true
 */
bool PackageController::removeService(const QString &alias)
{
    if (!m_connected) return false;

    bool ok = m_dbus->removeService(alias);
    if (ok) {
        loadServices();
        loadRepos();
    } else {
        emit errorOccurred(m_dbus->lastError());
    }
    return ok;
}

/**
 * @brief サービスのプロパティを変更する。
 * @param alias 対象のサービスエイリアス
 * @param properties 変更するプロパティ
 * @return 変更成功時 true
 */
bool PackageController::modifyService(const QString &alias, const QVariantMap &properties)
{
    if (!m_connected) return false;

    bool ok = m_dbus->modifyService(alias, properties);
    if (ok)
        loadServices();
    else
        emit errorOccurred(m_dbus->lastError());
    return ok;
}

/**
 * @brief サービスをリフレッシュする。
 * @param alias リフレッシュ対象のサービスエイリアス
 * @return リフレッシュ成功時 true
 */
bool PackageController::refreshService(const QString &alias)
{
    if (!m_connected) return false;

    setBusy(true);
    setStatusMessage(tr("Refreshing service: %1").arg(alias));

    bool ok = m_dbus->refreshService(alias);

    if (ok) {
        loadServices();
        loadRepos();
        setStatusMessage(tr("Service refreshed"));
    } else {
        setStatusMessage(tr("Failed to refresh service"));
        emit errorOccurred(m_dbus->lastError());
    }

    setBusy(false);
    return ok;
}

// -- 状態保存・復元 --

/**
 * @brief 変更予定のパッケージ一覧を取得する。
 * @return 変更予定パッケージ情報のリスト
 */
QVariantList PackageController::getPendingChanges()
{
    if (!m_connected) return {};
    return m_dbus->getPendingChanges();
}

/**
 * @brief ディスク使用量を取得する。
 * @return ディスク使用量情報のリスト (QVariantMap の QVariantList)
 */
QVariantList PackageController::getDiskUsage()
{
    if (!m_connected) return {};
    return m_dbus->getDiskUsage();
}

/**
 * @brief 変更予定のパッケージがあるか確認する。
 * @return 変更予定がある場合 true
 */
bool PackageController::hasPendingChanges()
{
    if (!m_connected) return false;
    return !m_dbus->getPendingChanges().isEmpty();
}

/**
 * @brief Polkit 認証を実行する。
 *
 * PolkitQt6-1 が利用可能な場合は同期認証を実行し、
 * 利用不可の場合は常に true を返す。
 * @param actionId Polkit アクションID
 * @return 認証成功時 true
 */
bool PackageController::checkAuth(const QString &actionId)
{
    auto authority = PolkitQt1::Authority::instance();
    auto result = authority->checkAuthorizationSync(
        actionId,
        PolkitQt1::UnixProcessSubject(QCoreApplication::applicationPid()),
        PolkitQt1::Authority::AllowUserInteraction
    );
    return result == PolkitQt1::Authority::Yes;
}

/**
 * @brief パッケージの選択状態を保存する。
 */
void PackageController::saveState()
{
    m_dbus->saveState();
}

/**
 * @brief 保存された選択状態を復元し、パッケージリストを再取得する。
 */
void PackageController::restoreState()
{
    m_dbus->restoreState();
    refreshPackageStatuses();
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
    const QColor window          = dark ? QColor("#353535") : QColor("#efefef");
    const QColor windowText      = dark ? QColor("#ffffff") : QColor("#000000");
    const QColor base            = dark ? QColor("#252525") : QColor("#ffffff");
    const QColor alternateBase   = dark ? QColor("#353535") : QColor("#f7f7f7");
    const QColor text            = dark ? QColor("#ffffff") : QColor("#000000");
    const QColor button          = dark ? QColor("#454545") : QColor("#e0e0e0");
    const QColor buttonText      = dark ? QColor("#ffffff") : QColor("#000000");
    const QColor highlight       = dark ? QColor("#2196F3") : QColor("#308cc6");
    const QColor highlightedText = dark ? QColor("#ffffff") : QColor("#ffffff");
    const QColor placeholderText = dark ? QColor("#888888") : QColor("#595959");
    const QColor mid             = dark ? QColor("#555555") : QColor("#b0b0b0");
    const QColor light           = dark ? QColor("#555555") : QColor("#ffffff");
    const QColor midlight        = dark ? QColor("#404040") : QColor("#e3e3e3");
    const QColor darkColor       = dark ? QColor("#232323") : QColor("#9f9f9f");
    const QColor shadow          = dark ? QColor("#141414") : QColor("#767676");
    const QColor link            = dark ? QColor("#64b5f6") : QColor("#0000ff");
    const QColor linkVisited     = dark ? QColor("#ce93d8") : QColor("#ff00ff");
    const QColor toolTipBase     = dark ? QColor("#454545") : QColor("#ffffdc");
    const QColor toolTipText     = dark ? QColor("#ffffff") : QColor("#000000");

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
    const QColor disWindowText      = dark ? QColor("#808080") : QColor("#a0a0a0");
    const QColor disBase            = dark ? QColor("#2a2a2a") : QColor("#efefef");
    const QColor disText            = dark ? QColor("#808080") : QColor("#a0a0a0");
    const QColor disButtonText      = dark ? QColor("#808080") : QColor("#a0a0a0");
    const QColor disHighlight       = dark ? QColor("#444444") : QColor("#c0c0c0");
    const QColor disHighlightedText = dark ? QColor("#808080") : QColor("#a0a0a0");
    const QColor disPlaceholderText = dark ? QColor("#606060") : QColor("#c0c0c0");
    const QColor disLink            = dark ? QColor("#506888") : QColor("#8080c0");
    const QColor disLinkVisited     = dark ? QColor("#7a6888") : QColor("#c080c0");
    const QColor disToolTipText     = dark ? QColor("#808080") : QColor("#a0a0a0");

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

/**
 * @brief バックエンドプロセスを終了させる。
 */
void PackageController::quitBackend()
{
    m_dbus->quit();
}

} // namespace qZypper
