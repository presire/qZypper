#ifndef QZYPPER_BACKEND_PACKAGEMANAGERADAPTOR_H
#define QZYPPER_BACKEND_PACKAGEMANAGERADAPTOR_H

#include <QObject>
#include <QDBusConnection>
#include <QDBusContext>
#include <QDBusMessage>
#include <QDBusServiceWatcher>
#include <QDBusPendingCallWatcher>
#include <QDBusUnixFileDescriptor>
#include <QMap>
#include <QVariantMap>
#include <QVariantList>
#include <QTimer>
#include <functional>
#include "ZyppExecutor.h"
#include "PolkitAuthorization.h"

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
    QVariantMap ImportRepoFile(const QString &content);            // .repo 内容を取込
    QVariantList ParseRepoFile(const QString &content);            // .repo 内容を構文解析 (読取専用)

    // サービス操作
    QVariantList GetServices();                                               // サービス一覧取得
    bool AddService(const QString &url, const QString &alias);                // サービス追加
    bool RemoveService(const QString &alias);                                 // サービス削除
    bool ModifyService(const QString &alias, const QVariantMap &properties);  // サービス変更
    bool RefreshService(const QString &alias);                                // サービスリフレッシュ

    // パッケージ検索・一覧
    QVariantList SearchPackages(const QString &query, int searchFlags);  // パッケージ検索
    QVariantList SearchPackagesEx(const QVariantMap &options);           // 拡張パッケージ検索 (PoolQuery)
    QVariantMap GetPackageDetails(const QString &name);                  // パッケージ詳細取得
    QVariantList GetPackagesByRepo(const QString &repoAlias);            // リポ別パッケージ
    QVariantList GetPatterns();                                          // パターン一覧取得
    QVariantList GetPackagesByPattern(const QString &patternName);       // パターン別パッケージ
    QVariantList GetPatches(int category);                               // パッチ一覧取得
    bool SetPatchStatus(const QString &id, int status);                  // パッチ選択状態変更
    QVariantMap SelectAllNeededPatches(const QVariantMap &options);      // 必要パッチ一括選択

    // 変更予定パッケージ一覧
    QVariantList GetPendingChanges();                         // 変更予定一覧取得

    // ローカル RPM 導入
    QVariantMap InstallLocalRpms(const QList<QDBusUnixFileDescriptor> &files,
                                 const QStringList &fileNames);  // ローカル RPM 群を導入 (GUI が開いた記述子)

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

    // ディストリビューションアップグレード
    QVariantMap DistUpgrade(const QVariantMap &options);      // dup (ディストリビューション更新)
    QVariantMap GetTransactionSummary();                      // 現在のリスクサマリ取得

    // コミット
    qulonglong GetSelectionRevision();                  // 選択改訂番号取得 (処理中も可)
    QVariantMap Commit(qulonglong expectedRevision);    // 確認した選択の変更をコミット

    // 状態保存・復元
    void SaveState();                                   // 選択状態を保存
    void RestoreState();                                // 選択状態を復元

    // ディスク使用量
    QVariantList GetDiskUsage();                        // ディスク使用量取得

    // 永続ロック (zypper al/rl 互換)
    QVariantList GetLocks();                            // ロック一覧取得
    bool AddLock(const QVariantMap &lock);              // ロック追加 (Polkit: manage-locks)
    bool RemoveLock(const QVariantMap &lock);           // ロック削除 (Polkit: manage-locks)

    // パッケージ分類
    QVariantList GetPackagesByClassification(const QString &cls);  // 分類別パッケージ一覧

    // zypp ヒストリ
    QVariantList GetHistory(const QVariantMap &options); // 操作履歴取得 (新しい順)

    // システム検証 / ソルバーテストケース
    QVariantMap VerifySystem();                         // システム整合性検証 (zypper verify 相当)
    QString CreateSolverTestcase();                     // ソルバーテストケース出力パスを返す

    // キャンセル
    void CancelOperation();                             // 操作キャンセル
    QVariantMap GetOperationState();
    bool AnswerPrompt(uint id, const QVariantMap &answer);
    bool ReleaseSession();

    // NOTE: 以下のシグナルは D-Bus イントロスペクション用の宣言。実際の送信は
    //       emitToOwner() による所有者宛て (unicast) で行い、Qt の emit は使わない。
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
    void OperationStateChanged(const QVariantMap &state);
    void PromptRequested(uint id, const QString &type, const QVariantMap &data);
    void PromptClosed(uint id);                                        // 問合せ終了 (回答/期限切れ/中断)
    void ScriptOutput(const QVariantMap &output);                                          // スクリプト/rpm出力行
    void PoolReloaded();

private:
    bool checkArguments();
    /**
     * @brief ワーカーから主スレッドへ引き渡す遅延返信の結果。
     */
    using AsyncOutcome = WorkerResult;

    bool checkNotBusy();                               // libzypp呼出の並行実行を拒否
    bool checkRead();                                  // 読取呼出の検査
    QVariant runRead(const std::function<QVariant()> &job);          // 読取処理を例外から保護して実行
    void emitToOwner(const QString &member, const QVariantList &args); // 所有者宛てのシグナル送信
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
    /**
     * @brief libzypp初期化のワーカー処理を生成する (失敗時は通知してfalseを返す)
     * @return 初期化ジョブ
     */
    std::function<AsyncOutcome()> makeInitializeJob();
    /**
     * @brief Initializeの非同期認可結果を処理し所有権を確立する
     * @param caller 認可要求した接続
     * @param authorized 認可されたか
     */
    void handleInitializeAuth(const QString &caller, bool authorized);
    /**
     * @brief 呼出元を所有者として採用できるか検査し登録する
     * @param caller 呼出元の一意バス名
     * @return 採用できた場合はtrue
     */
    bool adoptOwner(const QString &caller);
    void publishState();
    void releaseAbandonedSession();
    void cancelAuthorization(const QString &id);

    QTimer m_idleTimer;                                // 5分のアイドル自動終了タイマ
    QDBusServiceWatcher m_ownerWatcher;                // 所有者のバス接続消失を監視
    QString m_owner;                                   // セッション所有者の一意バス名
    bool m_busy = false;                               // 主スレッドのみが変更する処理中フラグ
    bool m_quitWhenIdle = false;                       // ワーカー完了後に終了するフラグ
    quint64 m_revision = 1;                            // 現在の選択改訂番号
    quint64 m_resolvedRevision = 0;                    // 解決成功時の選択改訂番号
    ZyppExecutor m_executor;
    PolkitAuthorization m_authorization;
    bool m_initialized = false;
    QTimer m_watchdog;
    QVariantMap m_publishedState;
    QString m_authId;
    /**
     * @brief Initialize の認可待ち 1 件分。
     */
    struct PendingInit {
        QString caller;                                // 呼出元の一意バス名
        uint uid = 0;                                  // 呼出元の UID (GetConnectionUnixUser)
        QDBusMessage msg;                              // 遅延返信する呼出
        QString authId;
    };
    static constexpr int kMaxPendingInitAuths = 8;        // 認可待ちの全体上限
    static constexpr int kMaxPendingInitAuthsPerUid = 2;  // 認可待ちの UID ごとの上限

    QMap<QString, PendingInit> m_pendingInitCalls;
};

} // namespace qZypper

#endif // QZYPPER_BACKEND_PACKAGEMANAGERADAPTOR_H
