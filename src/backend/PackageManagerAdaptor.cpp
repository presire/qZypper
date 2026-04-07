#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QEventLoop>
#include <QThread>
#include "PackageManagerAdaptor.h"
#include "ZyppManager.h"
#include "ZyppCallbackReceiver.h"
#include "RepoInfo.h"

// Polkit認証 (polkit-qt6が利用可能な場合)
// #include <PolkitQt1/Authority>

namespace qZypper {

/**
 * @brief PackageManagerAdaptor のコンストラクタ。
 *
 * アイドルタイムアウト (5分) を設定し、操作なしで自動終了する。
 * @param parent 親オブジェクト
 */
PackageManagerAdaptor::PackageManagerAdaptor(QObject *parent)
    : QDBusAbstractAdaptor(parent)
{
    // アイドルタイムアウト: 5分間操作がなければ終了
    m_idleTimer.setInterval(5 * 60 * 1000);
    m_idleTimer.setSingleShot(true);
    connect(&m_idleTimer, &QTimer::timeout, qApp, &QCoreApplication::quit);
    m_idleTimer.start();
}

/**
 * @brief アイドルタイマをリセットする。
 *
 * D-Busメソッド呼び出し時に毎回呼ばれ、自動終了までの時間を延長する。
 */
void PackageManagerAdaptor::resetIdleTimer()
{
    m_idleTimer.start();
}

// -- 初期化 --

/**
 * @brief libzypp を初期化する。
 * @return 初期化成功時 true
 */
bool PackageManagerAdaptor::Initialize()
{
    resetIdleTimer();

    try {
        bool ok = ZyppManager::instance().initialize();
        if (!ok) {
            QString err = QString::fromStdString(ZyppManager::instance().lastError());
            sendErrorReply(QDBusError::Failed,
                           err.isEmpty() ? QStringLiteral("Unknown initialization error") : err);
        }
        return ok;
    } catch (const std::exception& e) {
        sendErrorReply(QDBusError::Failed,
                       QStringLiteral("Initialization exception: %1").arg(QString::fromLocal8Bit(e.what())));
        return false;
    }
}

// -- リポジトリ操作 --

/**
 * @brief 全リポジトリをリフレッシュする。
 *
 * ワーカースレッドでリフレッシュを実行し、QEventLoopでD-Busイベントを処理しつつ完了を待つ。
 * @return リフレッシュ成功時 true
 */
bool PackageManagerAdaptor::RefreshRepos()
{
    // リフレッシュ後は GUI の Quit 呼び出しまでバックエンドを
    // 生存させ続けるため、アイドルタイマを永続停止する
    m_idleTimer.stop();

    // ワーカースレッドでリフレッシュを実行し、
    // QEventLoopでD-Busイベント(CancelOperation等)を処理しつつスレッド完了を待つ
    bool result = false;
    QEventLoop loop;

    auto *thread = QThread::create([this, &result]() {
        auto& mgr = ZyppManager::instance();
        result = mgr.refreshRepos([this](const std::string& alias, int pct) {
            auto qAlias = QString::fromStdString(alias);
            QMetaObject::invokeMethod(this, [this, qAlias, pct]() {
                emit RepoRefreshProgress(qAlias, pct);
            }, Qt::QueuedConnection);
        });
    });
    connect(thread, &QThread::finished, &loop, &QEventLoop::quit);
    connect(thread, &QThread::finished, thread, &QThread::deleteLater);
    thread->start();

    loop.exec();  // イベント処理しつつスレッド完了を待機

    return result;
}

/**
 * @brief 指定リポジトリを個別にリフレッシュする。
 * @param alias リフレッシュ対象のリポジトリエイリアス
 * @return リフレッシュ成功時 true
 */
bool PackageManagerAdaptor::RefreshSingleRepo(const QString &alias)
{
    m_idleTimer.stop();   // 永続停止 (GUI Quit まで生存)

    bool result = false;
    QEventLoop loop;

    auto *thread = QThread::create([this, &result, alias]() {
        auto& mgr = ZyppManager::instance();
        result = mgr.refreshRepo(alias.toStdString(),
            [this](const std::string& a, int pct) {
                auto qAlias = QString::fromStdString(a);
                QMetaObject::invokeMethod(this, [this, qAlias, pct]() {
                    emit RepoRefreshProgress(qAlias, pct);
                }, Qt::QueuedConnection);
            });
    });
    connect(thread, &QThread::finished, &loop, &QEventLoop::quit);
    connect(thread, &QThread::finished, thread, &QThread::deleteLater);
    thread->start();

    loop.exec();

    return result;
}

/**
 * @brief リポジトリ一覧を取得する。
 * @return リポジトリ情報の QVariantList
 */
QVariantList PackageManagerAdaptor::GetRepos()
{
    resetIdleTimer();

    QVariantList result;
    for (const auto& repo : ZyppManager::instance().getRepos())
        result.append(repo.toVariantMap());
    return result;
}

/**
 * @brief リポジトリを追加する。
 * @param url リポジトリURL
 * @param name リポジトリ名
 * @return 結果を格納した QVariantMap (success, errorMessage)
 */
QVariantMap PackageManagerAdaptor::AddRepo(const QString &url, const QString &name)
{
    resetIdleTimer();
    LongOperationGuard guard(this);
    QVariantMap result;

    auto& mgr = ZyppManager::instance();
    bool ok = mgr.addRepo(url.toStdString(), name.toStdString());
    result["success"] = ok;
    if (!ok)
        result["errorMessage"] = QString::fromStdString(mgr.lastError());
    return result;
}

/**
 * @brief 全プロパティを指定してリポジトリを追加する。
 * @param properties リポジトリプロパティ
 * @return 結果を格納した QVariantMap (success, errorMessage)
 */
QVariantMap PackageManagerAdaptor::AddRepoFull(const QVariantMap &properties)
{
    resetIdleTimer();
    LongOperationGuard guard(this);
    QVariantMap result;

    auto& mgr = ZyppManager::instance();
    RepoInfo info = RepoInfo::fromVariantMap(properties);
    bool ok = mgr.addRepo(info);
    result["success"] = ok;
    if (!ok)
        result["errorMessage"] = QString::fromStdString(mgr.lastError());
    return result;
}

/**
 * @brief リポジトリを削除する。
 * @param alias 削除対象のリポジトリエイリアス
 * @return 削除成功時 true
 */
bool PackageManagerAdaptor::RemoveRepo(const QString &alias)
{
    resetIdleTimer();
    return ZyppManager::instance().removeRepo(alias.toStdString());
}

/**
 * @brief リポジトリの有効/無効を切り替える。
 * @param alias 対象のリポジトリエイリアス
 * @param enabled 有効にする場合 true
 * @return 変更成功時 true
 */
bool PackageManagerAdaptor::SetRepoEnabled(const QString &alias, bool enabled)
{
    resetIdleTimer();
    return ZyppManager::instance().setRepoEnabled(alias.toStdString(), enabled);
}

/**
 * @brief リポジトリのプロパティを変更する。
 * @param alias 対象のリポジトリエイリアス
 * @param properties 変更するプロパティ
 * @return 変更成功時 true
 */
bool PackageManagerAdaptor::ModifyRepo(const QString &alias, const QVariantMap &properties)
{
    resetIdleTimer();

    RepoInfo info = RepoInfo::fromVariantMap(properties);
    return ZyppManager::instance().modifyRepo(alias.toStdString(), info);
}

// -- サービス操作 --

/**
 * @brief サービス一覧を取得する。
 * @return サービス情報の QVariantList
 */
QVariantList PackageManagerAdaptor::GetServices()
{
    resetIdleTimer();

    QVariantList result;
    for (const auto& svc : ZyppManager::instance().getServices())
        result.append(svc.toVariantMap());
    return result;
}

/**
 * @brief サービスを追加する。
 * @param url サービスURL
 * @param alias サービスエイリアス
 * @return 追加成功時 true
 */
bool PackageManagerAdaptor::AddService(const QString &url, const QString &alias)
{
    resetIdleTimer();
    LongOperationGuard guard(this);
    return ZyppManager::instance().addService(url.toStdString(), alias.toStdString());
}

/**
 * @brief サービスを削除する。
 * @param alias 削除対象のサービスエイリアス
 * @return 削除成功時 true
 */
bool PackageManagerAdaptor::RemoveService(const QString &alias)
{
    resetIdleTimer();
    return ZyppManager::instance().removeService(alias.toStdString());
}

/**
 * @brief サービスのプロパティを変更する。
 * @param alias 対象のサービスエイリアス
 * @param properties 変更するプロパティ
 * @return 変更成功時 true
 */
bool PackageManagerAdaptor::ModifyService(const QString &alias, const QVariantMap &properties)
{
    resetIdleTimer();

    ServiceInfo info = ServiceInfo::fromVariantMap(properties);
    return ZyppManager::instance().modifyService(alias.toStdString(), info);
}

/**
 * @brief サービスをリフレッシュする。
 * @param alias リフレッシュ対象のサービスエイリアス
 * @return リフレッシュ成功時 true
 */
bool PackageManagerAdaptor::RefreshService(const QString &alias)
{
    m_idleTimer.stop();   // 永続停止 (GUI Quit まで生存)
    return ZyppManager::instance().refreshService(alias.toStdString());
}

// -- パッケージ検索・一覧 --

/**
 * @brief パッケージを検索する。
 * @param query 検索クエリ文字列
 * @param searchFlags 検索フラグ (SearchFlag のビットマスク)
 * @return 検索結果のパッケージ情報リスト
 */
QVariantList PackageManagerAdaptor::SearchPackages(const QString &query, int searchFlags)
{
    resetIdleTimer();

    QVariantList result;
    for (const auto& pkg : ZyppManager::instance().searchPackages(query.toStdString(), searchFlags))
        result.append(pkg.toVariantMap());
    return result;
}

/**
 * @brief パッケージの詳細情報を取得する。
 * @param name パッケージ名
 * @return パッケージ詳細の QVariantMap
 */
QVariantMap PackageManagerAdaptor::GetPackageDetails(const QString &name)
{
    resetIdleTimer();
    return ZyppManager::instance().getPackageDetails(name.toStdString()).toVariantMap();
}

/**
 * @brief 指定リポジトリに属するパッケージ一覧を取得する。
 * @param repoAlias リポジトリエイリアス
 * @return パッケージ情報のリスト
 */
QVariantList PackageManagerAdaptor::GetPackagesByRepo(const QString &repoAlias)
{
    resetIdleTimer();

    QVariantList result;
    for (const auto& pkg : ZyppManager::instance().getPackagesByRepo(repoAlias.toStdString()))
        result.append(pkg.toVariantMap());
    return result;
}

/**
 * @brief パターン一覧を取得する。
 * @return パターン情報の QVariantList
 */
QVariantList PackageManagerAdaptor::GetPatterns()
{
    resetIdleTimer();

    QVariantList result;
    for (const auto& pat : ZyppManager::instance().getPatterns())
        result.append(pat.toVariantMap());
    return result;
}

/**
 * @brief 指定パターンに属するパッケージ一覧を取得する。
 * @param patternName パターン名
 * @return パッケージ情報のリスト
 */
QVariantList PackageManagerAdaptor::GetPackagesByPattern(const QString &patternName)
{
    resetIdleTimer();

    QVariantList result;
    for (const auto& pkg : ZyppManager::instance().getPackagesByPattern(patternName.toStdString()))
        result.append(pkg.toVariantMap());
    return result;
}

/**
 * @brief パッチ一覧を取得する。
 * @param category パッチカテゴリ (PatchCategory)
 * @return パッチ情報のリスト
 */
QVariantList PackageManagerAdaptor::GetPatches(int category)
{
    resetIdleTimer();

    QVariantList result;
    for (const auto& patch : ZyppManager::instance().getPatches(category))
        result.append(patch.toVariantMap());
    return result;
}

// -- 変更予定パッケージ一覧 --

/**
 * @brief 変更予定のパッケージ一覧を取得する。
 * @return 変更予定パッケージ情報のリスト
 */
QVariantList PackageManagerAdaptor::GetPendingChanges()
{
    resetIdleTimer();

    QVariantList result;
    for (const auto& pkg : ZyppManager::instance().getPendingChanges())
        result.append(pkg.toVariantMap());
    return result;
}

// -- 状態変更 --

/**
 * @brief パッケージのステータスを変更する。
 * @param name パッケージ名
 * @param status 新しいステータス (PackageStatus)
 * @return 変更成功時 true
 */
bool PackageManagerAdaptor::SetPackageStatus(const QString &name, int status)
{
    resetIdleTimer();
    return ZyppManager::instance().setPackageStatus(name.toStdString(), status);
}

/**
 * @brief パッケージのバージョンを変更する。
 * @param name パッケージ名
 * @param version 選択バージョン (edition 文字列)
 * @param arch アーキテクチャ
 * @param repoAlias リポジトリエイリアス
 * @return 変更成功時 true
 */
bool PackageManagerAdaptor::SetPackageVersion(const QString &name, const QString &version,
                                              const QString &arch, const QString &repoAlias)
{
    resetIdleTimer();
    return ZyppManager::instance().setPackageVersion(
        name.toStdString(), version.toStdString(),
        arch.toStdString(), repoAlias.toStdString());
}

/**
 * @brief パターンのステータスを変更する。
 * @param name パターン名
 * @param status 新しいステータス (PackageStatus)
 * @return 変更成功時 true
 */
bool PackageManagerAdaptor::SetPatternStatus(const QString &name, int status)
{
    resetIdleTimer();
    return ZyppManager::instance().setPatternStatus(name.toStdString(), status);
}

// -- 全パッケージ更新 --

/**
 * @brief 全パッケージの更新を実行する (doUpdate + 依存解決)。
 * @return 解決結果 (success, problems)
 */
QVariantMap PackageManagerAdaptor::UpdateAllPackages()
{
    resetIdleTimer();
    LongOperationGuard guard(this);

    QVariantMap result;
    try {
        auto solverResult = ZyppManager::instance().updateAllPackages();

        result["success"] = solverResult.success;

        QVariantList problems;
        for (const auto& problem : solverResult.problems)
            problems.append(problem.toVariantMap());
        result["problems"] = problems;
    } catch (const std::exception& e) {
        qWarning() << "UpdateAllPackages exception:" << e.what();
        result["success"] = false;
    } catch (...) {
        qWarning() << "UpdateAllPackages: unknown exception";
        result["success"] = false;
    }

    return result;
}

// -- 依存関係解決 --

/**
 * @brief 依存関係を解決する。
 * @return 解決結果 (success, problems)
 */
QVariantMap PackageManagerAdaptor::ResolveDependencies()
{
    resetIdleTimer();
    LongOperationGuard guard(this);

    QVariantMap result;
    try {
        auto solverResult = ZyppManager::instance().resolveDependencies();

        result["success"] = solverResult.success;

        QVariantList problems;
        for (const auto& problem : solverResult.problems)
            problems.append(problem.toVariantMap());
        result["problems"] = problems;
    } catch (const std::exception& e) {
        qWarning() << "ResolveDependencies exception:" << e.what();
        result["success"] = false;
    } catch (...) {
        qWarning() << "ResolveDependencies: unknown exception";
        result["success"] = false;
    }

    return result;
}

/**
 * @brief 依存関係の衝突に対する解決策を適用する。
 * @param problemIndex 問題のインデックス
 * @param solutionIndex 解決策のインデックス
 * @return 適用成功時 true
 */
bool PackageManagerAdaptor::ApplySolution(int problemIndex, int solutionIndex)
{
    resetIdleTimer();
    return ZyppManager::instance().applySolution(problemIndex, solutionIndex);
}

// -- コミット --

/**
 * @brief パッケージの変更をコミットする。
 *
 * libzypp のコミットを実行し結果を返す。
 * @return コミット結果 (success, installed, updated, removed, failedPackages)
 */
QVariantMap PackageManagerAdaptor::Commit()
{
    // インストール/アンインストール/更新後は GUI の Quit 呼び出しまで
    // バックエンドを生存させ続けるため、アイドルタイマを永続停止する
    m_idleTimer.stop();

    // ワーカースレッドでコミットを実行し、
    // QEventLoopでD-Busイベント(CancelOperation等)を処理しつつスレッド完了を待つ
    ZyppManager::CommitResult commitResult;
    QEventLoop loop;

    auto *thread = QThread::create([this, &commitResult]() {
        auto& mgr = ZyppManager::instance();
        commitResult = mgr.commit([this](const CommitProgressInfo& info) {
            auto pkgName = QString::fromStdString(info.packageName);
            auto stage   = QString::fromStdString(info.stage);
            int pct      = info.percentage;
            int total    = info.totalSteps;
            int done     = info.completedSteps;
            int overall  = info.overallPercentage;
            QMetaObject::invokeMethod(this, [this, pkgName, pct, stage,
                                             total, done, overall]() {
                emit CommitProgressChanged(pkgName, pct, stage,
                                           total, done, overall);
                emit ProgressChanged(pkgName, overall, stage);
            }, Qt::QueuedConnection);
        });
    });
    connect(thread, &QThread::finished, &loop, &QEventLoop::quit);
    connect(thread, &QThread::finished, thread, &QThread::deleteLater);
    thread->start();

    loop.exec();  // イベント処理しつつスレッド完了を待機

    QVariantMap result;
    result["success"]   = commitResult.success;
    result["installed"] = commitResult.installed;
    result["updated"]   = commitResult.updated;
    result["removed"]   = commitResult.removed;

    // パッケージ名リスト
    QStringList installedList, updatedList, removedList, failed;
    for (const auto& p : commitResult.installedPackages)
        installedList << QString::fromStdString(p);
    for (const auto& p : commitResult.updatedPackages)
        updatedList << QString::fromStdString(p);
    for (const auto& p : commitResult.removedPackages)
        removedList << QString::fromStdString(p);
    for (const auto& f : commitResult.failedPackages)
        failed << QString::fromStdString(f);
    result["installedPackages"] = installedList;
    result["updatedPackages"]   = updatedList;
    result["removedPackages"]   = removedList;
    result["failedPackages"]    = failed;

    // サイズ・時間
    result["totalInstalledSize"] = static_cast<qulonglong>(commitResult.totalInstalledSize);
    result["totalDownloadSize"]  = static_cast<qulonglong>(commitResult.totalDownloadSize);
    result["elapsedSeconds"]     = commitResult.elapsedSeconds;

    // エラーメッセージ
    if (!commitResult.success) {
        auto errMsg = commitResult.errorMessage.empty()
            ? ZyppManager::instance().lastError()
            : commitResult.errorMessage;
        result["errorMessage"] = QString::fromStdString(errMsg);
    }

    emit TransactionFinished(commitResult.success,
        QString("Installed: %1, Updated: %2, Removed: %3")
            .arg(commitResult.installed)
            .arg(commitResult.updated)
            .arg(commitResult.removed));

    return result;
}

// -- 状態保存・復元 --

/**
 * @brief パッケージの選択状態を保存する。
 */
void PackageManagerAdaptor::SaveState()
{
    resetIdleTimer();
    ZyppManager::instance().saveState();
}

/**
 * @brief 保存されたパッケージ選択状態を復元する。
 */
void PackageManagerAdaptor::RestoreState()
{
    resetIdleTimer();
    ZyppManager::instance().restoreState();
}

// -- ディスク使用量 --

/**
 * @brief ディスク使用量を取得する。
 * @return ディスク使用量情報のリスト
 */
QVariantList PackageManagerAdaptor::GetDiskUsage()
{
    resetIdleTimer();

    QVariantList result;
    for (const auto& du : ZyppManager::instance().getDiskUsage())
        result.append(du.toVariantMap());
    return result;
}

// -- キャンセル --

/**
 * @brief 実行中の操作をキャンセルする。
 */
void PackageManagerAdaptor::CancelOperation()
{
    ZyppManager::instance().cancelOperation();
}

// -- 終了 --

/**
 * @brief バックエンドプロセスを終了する。
 */
void PackageManagerAdaptor::Quit()
{
    QCoreApplication::quit();
}

} // namespace qZypper
