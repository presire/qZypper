#ifndef QZYPPER_BACKEND_PACKAGEMANAGERADAPTOR_H
#define QZYPPER_BACKEND_PACKAGEMANAGERADAPTOR_H

#include <QObject>
#include <QDBusAbstractAdaptor>
#include <QDBusConnection>
#include <QDBusContext>
#include <QDBusMessage>
#include <QVariantMap>
#include <QVariantList>
#include <QTimer>

namespace qZypper {

/**
 * @brief D-Busアダプタ。バックエンドの全D-Busメソッドを公開する。
 *
 * QDBusAbstractAdaptorを継承し、system bus上で
 * org.presire.qzypper.PackageManagerインターフェースを提供する。
 */
class PackageManagerAdaptor : public QDBusAbstractAdaptor, protected QDBusContext
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.presire.qzypper.PackageManager")

public:
    explicit PackageManagerAdaptor(QObject *parent);

public slots:
    // 初期化
    bool Initialize();                                             // libzypp初期化

    // リポジトリ操作
    bool RefreshRepos();                                           // 全リポリフレッシュ
    bool RefreshSingleRepo(const QString &alias);                  // 個別リポリフレッシュ
    QVariantList GetRepos();                                       // リポジトリ一覧取得
    QVariantMap AddRepo(const QString &url, const QString &name);  // リポ追加
    QVariantMap AddRepoFull(const QVariantMap &properties);        // リポ追加 (全属性)
    bool RemoveRepo(const QString &alias);                         // リポ削除
    bool SetRepoEnabled(const QString &alias, bool enabled);       // リポ有効/無効
    bool ModifyRepo(const QString &alias, const QVariantMap &properties);  // リポ変更

    // サービス操作
    QVariantList GetServices();                                               // サービス一覧取得
    bool AddService(const QString &url, const QString &alias);                // サービス追加
    bool RemoveService(const QString &alias);                                 // サービス削除
    bool ModifyService(const QString &alias, const QVariantMap &properties);  // サービス変更
    bool RefreshService(const QString &alias);                                // サービスリフレッシュ

    // パッケージ検索・一覧
    QVariantList SearchPackages(const QString &query, int searchFlags);  // パッケージ検索
    QVariantMap GetPackageDetails(const QString &name);                  // パッケージ詳細取得
    QVariantList GetPackagesByRepo(const QString &repoAlias);            // リポ別パッケージ
    QVariantList GetPatterns();                                          // パターン一覧取得
    QVariantList GetPackagesByPattern(const QString &patternName);       // パターン別パッケージ
    QVariantList GetPatches(int category);                               // パッチ一覧取得

    // 変更予定パッケージ一覧
    QVariantList GetPendingChanges();                          // 変更予定一覧取得

    // 状態変更
    bool SetPackageStatus(const QString &name, int status);   // パッケージ状態変更
    bool SetPackageVersion(const QString &name,              // パッケージバージョン変更
                           const QString &version,
                           const QString &arch,
                           const QString &repoAlias);
    bool SetPatternStatus(const QString &name, int status);   // パターン状態変更

    // 全パッケージ更新
    QVariantMap UpdateAllPackages();                          // 全パッケージ更新 (doUpdate)

    // 依存関係解決
    QVariantMap ResolveDependencies();                        // ソルバー実行
    bool ApplySolution(int problemIndex, int solutionIndex);  // 解決策適用

    // コミット
    QVariantMap Commit();                               // 変更をコミット

    // 状態保存・復元
    void SaveState();                                   // 選択状態を保存
    void RestoreState();                                // 選択状態を復元

    // ディスク使用量
    QVariantList GetDiskUsage();                        // ディスク使用量取得

    // キャンセル
    void CancelOperation();                             // 操作キャンセル

    // 終了
    void Quit();                                        // バックエンド終了

signals:
    void ProgressChanged(const QString &packageName, int percentage, const QString &stage);  // 操作進捗
    void CommitProgressChanged(const QString &packageName, int percentage,                   // コミット進捗 (詳細版)
                               const QString &stage, int totalSteps,
                               int completedSteps, int overallPercentage);
    void TransactionFinished(bool success, const QString &summary);                          // トランザクション完了
    void RepoRefreshProgress(const QString &repoAlias, int percentage);                      // リフレッシュ進捗
    void ErrorOccurred(const QString &errorMessage);                                         // エラー通知

private:
    void resetIdleTimer();                              // アイドルタイマリセット
    QTimer m_idleTimer;                                 // アイドル自動終了タイマ

    /**
     * @brief 長時間操作中にアイドルタイマを停止するRAIIガード。
     *
     * コミット/リフレッシュ等、5分を超える可能性のある処理の間、
     * アイドルタイマの発火によるバックエンド自動終了を防ぐ。
     */
    class LongOperationGuard
    {
    public:
        explicit LongOperationGuard(PackageManagerAdaptor *adaptor)
            : m_adaptor(adaptor) { m_adaptor->m_idleTimer.stop(); }
        ~LongOperationGuard() { m_adaptor->m_idleTimer.start(); }
        LongOperationGuard(const LongOperationGuard &) = delete;
        LongOperationGuard &operator=(const LongOperationGuard &) = delete;
    private:
        PackageManagerAdaptor *m_adaptor;
    };
};

} // namespace qZypper

#endif // QZYPPER_BACKEND_PACKAGEMANAGERADAPTOR_H
