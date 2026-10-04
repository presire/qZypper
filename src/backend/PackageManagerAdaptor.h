#ifndef QZYPPER_BACKEND_PACKAGEMANAGERADAPTOR_H
#define QZYPPER_BACKEND_PACKAGEMANAGERADAPTOR_H

#include <QObject>
#include <QDBusConnection>
#include <QDBusContext>
#include <QDBusMessage>
#include <QDBusServiceWatcher>
#include <QDBusPendingCallWatcher>
#include <QMap>
#include <QVariantMap>
#include <QVariantList>
#include <QTimer>
#include <atomic>
#include <functional>
#include <memory>

namespace qZypper {

/**
 * @brief D-Busアダプタ
 *        バックエンドの全D-Busメソッドを公開する
 *
 * system busに直接登録し、呼出コンテキスト・所有者・Polkit認可を管理する
 * libzyppに触れる呼出は単一ワーカーとの同時実行を禁止する
 */
class PackageManagerAdaptor : public QObject, protected QDBusContext
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.presire.qzypper.PackageManager")

public:
    explicit PackageManagerAdaptor(QObject *parent = nullptr);

public slots:
    // 初期化
    bool Initialize();                                             // libzypp初期化

    // リポジトリ操作
    bool RefreshRepos();                                           // 全リポリフレッシュ
    bool RefreshSingleRepo(const QString &alias);                  // 個別リポリフレッシュ
    bool TrustKey(const QString &fingerprint);                     // リポ署名鍵を明示承認
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
    QVariantList GetPendingChanges();                         // 変更予定一覧取得

    // 状態変更
    bool SetPackageStatus(const QString &name, int status);   // パッケージ状態変更
    bool SetPackageVersion(const QString &name,               // パッケージバージョン変更
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
    qulonglong GetSelectionRevision();                  // 選択改訂番号取得 (処理中も可)
    QVariantMap Commit(qulonglong expectedRevision);    // 確認した選択の変更をコミット

    // 状態保存・復元
    void SaveState();                                   // 選択状態を保存
    void RestoreState();                                // 選択状態を復元

    // ディスク使用量
    QVariantList GetDiskUsage();                        // ディスク使用量取得

    // キャンセル
    void CancelOperation();                             // 操作キャンセル

signals:
    void ProgressChanged(const QString &packageName, int percentage, const QString &stage);  // 操作進捗
    void CommitProgressChanged(const QString &packageName, int percentage,                   // コミット進捗 (詳細版)
                               const QString &stage, int totalSteps,
                               int completedSteps, int overallPercentage);
    void TransactionFinished(bool success, const QString &summary);                          // トランザクション完了
    void RepoRefreshProgress(const QString &repoAlias, int percentage);                      // リフレッシュ進捗
    void ErrorOccurred(const QString &errorMessage);                                         // エラー通知
    void PackageStateChanged(const QString &packageName, const QString &event);              // パッケージ状態遷移
    void UntrustedKeyDetected(const QVariantMap &keyInfo);                                   // 未承認署名鍵の通知

private:
    /**
     * @brief ワーカーから主スレッドへ引き渡す遅延返信の結果。
     */
    struct AsyncOutcome {
        QVariant value;                                // 成功時の返信値 (結果マップを含む)
        QString errorName;                             // D-Busエラー名
        QString errorText;                             // D-Busエラー本文
        bool transactionFinished = false;              // コミット完了通知の有無
        bool transactionSuccess = false;               // コミット成功フラグ
        QString transactionSummary;                    // コミット結果要約
    };

    bool checkNotBusy();                               // libzypp呼出の並行実行を拒否
    bool checkRead();                                  // 読取呼出の検査
    bool checkOwner();                                 // 生存する所有者の呼出か検査
    bool preparePrivileged();                          // 処理中・所有者・初期化を順に検査
    void ownerVanished(const QString &owner);          // 所有者消失時に安全な終了を予約
    void resetIdleTimer();                             // 所有者の呼出だけでタイマをリセット
    void bumpRevision();                               // 選択改訂を進め解決済み状態を無効化
    QVariant runOwnerSync(const std::function<QVariant()> &job,
                          bool bump = true, bool solver = false);   // 同期操作と例外・改訂の管理
    bool runPrivilegedBool(const QString &actionId,
                           const std::function<bool()> &job);       // 真偽値を返す特権操作
    void startWorker(const QDBusMessage &msg,
                     const std::function<AsyncOutcome()> &job);     // 単一ワーカーで実行 (認可なし)
    void startPrivileged(const QString &actionId,
                         const std::function<AsyncOutcome()> &job); // 認可後に単一ワーカーで実行
    void finishPrivileged(const QDBusMessage &msg,
                          const AsyncOutcome &outcome);             // 主スレッドから返信して処理を完了
    static bool checkAuthorization(const QString &caller,
                                   const QString &actionId);        // バス名を対象に Polkit 認可
    /**
     * @brief Polkit 認可要求を構築する。
     * @param caller 呼出元の一意バス名
     * @param actionId Polkit アクションID
     * @return CheckAuthorizationのD-Bus要求
     */
    static QDBusMessage buildAuthRequest(const QString &caller, const QString &actionId);
    /**
     * @brief Polkit認可応答を検証し認可の有無を返す (異常時は拒否)
     * @param reply Polkitからの応答
     * @return is_authorizedがtrueの場合のみtrue
     */
    static bool parseAuthReply(const QDBusMessage &reply);
    /**
     * @brief libzypp初期化のワーカー処理を生成する (失敗時は通知してfalseを返す)
     * @return 初期化ジョブ
     */
    std::function<AsyncOutcome()> makeInitializeJob();
    /**
     * @brief Initializeの非同期認可結果を処理し所有権を確立する
     * @param watcher 認可待ちの監視子
     */
    void handleInitializeAuth(QDBusPendingCallWatcher *watcher);
    /**
     * @brief 呼出元を所有者として採用できるか検査し登録する
     * @param caller 呼出元の一意バス名
     * @return 採用できた場合はtrue
     */
    bool adoptOwner(const QString &caller);

    QTimer m_idleTimer;                                // 5分のアイドル自動終了タイマ
    QDBusServiceWatcher m_ownerWatcher;                // 所有者のバス接続消失を監視
    QString m_owner;                                   // セッション所有者の一意バス名
    bool m_busy = false;                               // 主スレッドのみが変更する処理中フラグ
    bool m_quitWhenIdle = false;                       // ワーカー完了後に終了するフラグ
    quint64 m_revision = 1;                            // 現在の選択改訂番号
    quint64 m_resolvedRevision = 0;                    // 解決成功時の選択改訂番号
    int m_pendingInitAuths = 0;                        // Initialize 認可待ちの件数
    QMap<QDBusPendingCallWatcher *, QPair<QString, QDBusMessage>> m_pendingInitCalls; // 認可待ちの呼出元と呼出
    std::shared_ptr<std::atomic<bool>> m_cancelToken;  // 実行中操作の取消要求
};

} // namespace qZypper

#endif // QZYPPER_BACKEND_PACKAGEMANAGERADAPTOR_H
