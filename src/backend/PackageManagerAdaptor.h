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
 * @class PackageManagerAdaptor
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
    /** @brief D-Busアダプタを親オブジェクトへ関連付けて生成する */
    explicit PackageManagerAdaptor(QObject *parent = nullptr);

public slots:
    // 初期化
    /** @brief 呼出元の認可後にlibzyppセッションを初期化する */
    bool Initialize();                                             // libzypp初期化

    // リポジトリ操作
    /** @brief 有効な全リポジトリのメタデータを更新する */
    bool RefreshRepos();                                           // 全リポリフレッシュ
    /** @brief 自動更新が有効なリポジトリのうち期限切れのものだけを更新する */
    bool RefreshReposIfNeeded();                                   // 期限切れリポジトリのリフレッシュ
    /** @brief 指定したリポジトリだけのメタデータを更新する */
    bool RefreshSingleRepo(const QString &alias);                  // 個別リポリフレッシュ
    /** @brief 次回の鍵確認で使う署名鍵指紋を事前承認する */
    bool TrustKey(const QString &fingerprint);                     // リポ署名鍵を明示承認
    /** @brief 現在登録されているリポジトリを返す */
    QVariantList GetRepos();                                       // リポジトリ一覧取得
    /** @brief URLと表示名だけを指定してリポジトリを追加する */
    QVariantMap AddRepo(const QString &url, const QString &name);  // リポジトリ追加
    /** @brief プロパティマップからリポジトリを追加する */
    QVariantMap AddRepoFull(const QVariantMap &properties);        // リポジトリ追加 (全属性)
    /** @brief エイリアスで指定したリポジトリを削除する */
    bool RemoveRepo(const QString &alias);                         // リポジトリ削除
    /** @brief リポジトリの有効状態を変更する */
    bool SetRepoEnabled(const QString &alias, bool enabled);       // リポジトリ有効/無効
    /** @brief 許可されたプロパティだけを使いリポジトリを更新する */
    bool ModifyRepo(const QString &alias, const QVariantMap &properties);  // リポジトリ変更
    /** @brief .repo内容を検査して登録可能なリポジトリとして取り込む */
    QVariantMap ImportRepoFile(const QString &content);            // .repo内容を取込
    /** @brief .repo内容を変更せずに解析結果だけを返す */
    QVariantList ParseRepoFile(const QString &content);            // .repo内容を構文解析 (読取専用)

    // サービス操作
    /** @brief 登録済みサービスの一覧を返す */
    QVariantList GetServices();                                               // サービス一覧取得
    /** @brief URLとエイリアスからサービスを追加する */
    bool AddService(const QString &url, const QString &alias);                // サービス追加
    /** @brief 指定したサービスを削除する */
    bool RemoveService(const QString &alias);                                 // サービス削除
    /** @brief 指定したサービスのプロパティを更新する */
    bool ModifyService(const QString &alias, const QVariantMap &properties);  // サービス変更
    /** @brief サービスを更新して関連リポジトリを同期する */
    bool RefreshService(const QString &alias);                                // サービスリフレッシュ

    // パッケージ検索・一覧
    /** @brief 指定フィールドを対象にパッケージを部分一致検索する */
    QVariantList SearchPackages(const QString &query, int searchFlags);  // パッケージ検索
    /** @brief 検索オプションに従いパッケージを検索する */
    QVariantList SearchPackagesEx(const QVariantMap &options);           // 拡張パッケージ検索 (PoolQuery)
    /** @brief 指定したパッケージの詳細情報を返す */
    QVariantMap GetPackageDetails(const QString &name);                  // パッケージ詳細取得
    /** @brief リポジトリが提供するパッケージを返す */
    QVariantList GetPackagesByRepo(const QString &repoAlias);            // リポ別パッケージ
    /** @brief 利用可能なパターンを返す */
    QVariantList GetPatterns();                                          // パターン一覧取得
    /** @brief パターンに含まれるパッケージを返す */
    QVariantList GetPackagesByPattern(const QString &patternName);       // パターン別パッケージ
    /** @brief カテゴリに該当するパッチを返す */
    QVariantList GetPatches(int category);                               // パッチ一覧取得
    /** @brief パッチの選択状態を変更する */
    bool SetPatchStatus(const QString &id, int status);                  // パッチ選択状態変更
    /** @brief 必要なパッチを条件に従ってまとめて選択する */
    QVariantMap SelectAllNeededPatches(const QVariantMap &options);      // 必要パッチ一括選択

    // 変更予定パッケージ一覧
    /** @brief コミット前の選択変更一覧を返す */
    QVariantList GetPendingChanges();                                    // 変更予定一覧取得

    // RPMグループ・言語
    /** @brief RPMグループのツリーを件数付きで返す */
    QVariantList GetRpmGroups();                                         // RPMグループツリー取得
    /** @brief 指定RPMグループ配下のパッケージ一覧を返す */
    QVariantList GetPackagesByRpmGroup(const QString &path);             // グループ別パッケージ取得
    /** @brief 利用可能・要求済みロケールの一覧を返す */
    QVariantList GetLocales();                                           // ロケール一覧取得
    /** @brief 指定ロケールに対応するパッケージ一覧を返す */
    QVariantList GetPackagesByLocale(const QString &code);               // ロケール別パッケージ取得
    /** @brief 保存済み状態との差分として未反映のロケール変更を返す */
    QVariantList GetPendingLocaleChanges();                              // 未反映ロケール変更取得
    /** @brief 指定ロケールの要求状態を変更する */
    bool SetLocaleRequested(const QString &code, bool requested);        // ロケール要求状態変更

    // ローカルRPM導入
    /** @brief UIから渡されたRPM記述子を安全に導入候補へ追加する */
    QVariantMap InstallLocalRpms(const QList<QDBusUnixFileDescriptor> &files,
                                 const QStringList &fileNames);          // ローカルRPM群を導入 (UIが開いた記述子)

    // 状態変更
    /** @brief パッケージの要求状態を変更する */
    bool SetPackageStatus(const QString &name, int status);              // パッケージ状態変更
    /** @brief リポジトリ内の指定版をパッケージの候補として選択する */
    bool SetPackageVersion(const QString &name,                          // パッケージバージョン変更
                           const QString &version,
                           const QString &arch,
                           const QString &repoAlias);
    /** @brief パターンの要求状態を変更する */
    bool SetPatternStatus(const QString &name, int status);   // パターン状態変更

    // 一括操作
    /** @brief 複数パッケージへ同じ操作を一括適用する */
    QVariantMap SetPackagesAction(const QStringList &names,   // 一括状態変更
                                  const QString &action);
    /** @brief インストール済みパッケージのサブパッケージを選択する */
    QVariantMap SelectSubpackages(const QString &suffix);     // サブパッケージ選択
    /** @brief 推奨パッケージをすべて選択して依存を解決する */
    QVariantMap SelectAllRecommended();                       // 推奨パッケージ選択

    // 全パッケージ更新
    /** @brief 更新可能なパッケージをすべて更新対象として選択する */
    QVariantMap UpdateAllPackages();                          // 全パッケージ更新 (doUpdate)

    // 依存関係解決
    /** @brief 現在の選択に対して依存関係を解決する */
    QVariantMap ResolveDependencies();                        // ソルバー実行
    /** @brief 指定した競合の解決策を適用する */
    bool ApplySolution(int problemIndex, int solutionIndex);  // 解決策適用

    // ソルバー設定
    /** @brief 有効なソルバー設定とzypp.conf既定値を返す */
    QVariantMap GetSolverSettings();                          // ソルバー設定取得
    /** @brief 指定したソルバー設定だけを上書きする */
    bool SetSolverSettings(const QVariantMap &settings);      // ソルバー設定上書き

    // ディストリビューションアップグレード
    /** @brief 配布版アップグレードの選択変更を作成する */
    QVariantMap DistUpgrade(const QVariantMap &options);      // dup (ディストリビューション更新)
    /** @brief 現在の変更が持つリスク要約を返す */
    QVariantMap GetTransactionSummary();                      // 現在のリスクサマリ取得

    // コミット
    /** @brief 現在の選択改訂番号を返す */
    qulonglong GetSelectionRevision();                        // 選択改訂番号取得 (処理中も可)
    /** @brief 確認済みの選択改訂と一致する場合だけ変更を適用する */
    QVariantMap Commit(qulonglong expectedRevision);          // 確認した選択の変更をコミット

    // 状態保存・復元
    /** @brief 現在の選択状態を一時保存する */
    void SaveState();                                         // 選択状態を保存
    /** @brief 保存済みの選択状態を復元する */
    void RestoreState();                                      // 選択状態を復元

    // ディスク使用量
    /** @brief 選択変更後を含むディスク使用量を返す */
    QVariantList GetDiskUsage();                              // ディスク使用量取得

    // 永続ロック (zypper al/rl互換)
    /** @brief 永続ロックの一覧を返す */
    QVariantList GetLocks();                                  // ロック一覧取得
    /** @brief Polkit認可後に永続ロックを追加する */
    bool AddLock(const QVariantMap &lock);                    // ロック追加 (Polkit: manage-locks)
    /** @brief Polkit認可後に永続ロックを削除する */
    bool RemoveLock(const QVariantMap &lock);                 // ロック削除 (Polkit: manage-locks)

    // パッケージ分類
    /** @brief 分類に一致するパッケージを返す */
    QVariantList GetPackagesByClassification(const QString &cls);  // 分類別パッケージ一覧

    // zyppヒストリ
    /** @brief 検証済みの範囲で操作履歴を新しい順に返す */
    QVariantList GetHistory(const QVariantMap &options); // 操作履歴取得 (新しい順)

    // システム検証 / ソルバーテストケース
    /** @brief システム整合性を検証して問題を返す */
    QVariantMap VerifySystem();                         // システム整合性検証 (zypper verify相当)
    /** @brief ソルバー障害解析用テストケースの出力先を返す */
    QString CreateSolverTestcase();                     // ソルバーテストケース出力パスを返す

    // キャンセル
    /** @brief 実行中処理へ協調的な取消要求を送る */
    void CancelOperation();                             // 操作キャンセル
    /** @brief 公開可能な現在の操作状態を返す */
    QVariantMap GetOperationState();
    /** @brief 保留中の問合せへ検証済みの回答を渡す */
    bool AnswerPrompt(uint id, const QVariantMap &answer);
    /** @brief 所有者セッションを明示的に解放する */
    bool ReleaseSession();

    // NOTE: 以下のシグナルはD-Busイントロスペクション用の宣言、
    //       実際の送信は、emitToOwner()による所有者宛て (unicast) で行い、Qtのemitは使用しない
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
    void OperationStateChanged(const QVariantMap &state);                                    // 操作状態の更新
    void PromptRequested(uint id, const QString &type, const QVariantMap &data);             // 問合せの表示要求
    void PromptClosed(uint id);                                                              // 問合せ終了 (回答/期限切れ/中断)
    void ScriptOutput(const QVariantMap &output);                                            // スクリプト/RPM出力行
    void PoolReloaded();                                                                     // パッケージプール再読込

private:
    /** @brief 現在のD-Bus呼出引数を境界条件に従って検査する */
    bool checkArguments();
    /**
     * @brief ワーカーから主スレッドへ引き渡す遅延返信の結果
     */
    using AsyncOutcome = WorkerResult;

    /** @brief 別のlibzypp操作が実行中ではないことを確認する */
    bool checkNotBusy();                                                // libzypp呼出の並行実行を拒否
    /** @brief 読取呼出の所有者と初期化状態を検査する */
    bool checkRead();                                                   // 読取呼出の検査
    /** @brief 読取ジョブを例外から保護して実行する */
    QVariant runRead(const std::function<QVariant()> &job);             // 読取処理を例外から保護して実行
    void emitToOwner(const QString &member, const QVariantList &args);  // 所有者宛てのシグナル送信
    bool checkOwner();                                                  // 生存する所有者の呼出か検査
    bool preparePrivileged();                                           // 処理中・所有者・初期化を順に検査
    void ownerVanished(const QString &owner);                           // 所有者消失時に安全な終了を予約
    void resetIdleTimer();                                              // 所有者の呼出だけでタイマをリセット
    void idleTimeout();                                                 // アイドル期限で終了または終了待ちに移行
    void bumpRevision();                                                // 選択改訂を進め解決済み状態を無効化
    QVariant runOwnerSync(const std::function<QVariant()> &job,
                          bool bump = true, bool solver = false);       // 同期操作と例外・改訂の管理
    bool runPrivilegedBool(const QString &actionId,
                           const std::function<bool()> &job);           // 真偽値を返す特権操作
    void startWorker(const QDBusMessage &msg,
                     const std::function<AsyncOutcome()> &job);         // 単一ワーカーで実行 (認可なし)
    void startPrivileged(const QString &actionId,
                         const std::function<AsyncOutcome()> &job);     // 認可後に単一ワーカーで実行
    void finishPrivileged(const QDBusMessage &msg,
                          const AsyncOutcome &outcome);                 // 主スレッドから返信して処理を完了
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
    /** @brief 変更された操作状態だけを所有者へ通知する */
    void publishState();
    /** @brief 所有者消失後にワーカー上でセッションを安全に解放する */
    void releaseAbandonedSession();
    /** @brief 保留中のPolkit認可要求を取消す */
    void cancelAuthorization(const QString &id);

    QTimer m_idleTimer;                                // 所有者不在時の5分アイドル自動終了タイマ
    QDBusServiceWatcher m_ownerWatcher;                // 所有者のバス接続消失を監視
    QString m_owner;                                   // セッション所有者の一意バス名
    bool m_busy = false;                               // 主スレッドのみが変更する処理中フラグ
    bool m_quitWhenIdle = false;                       // ワーカー完了後に終了するフラグ
    bool m_idleExpired = false;                        // アイドル期限後、期限時点の認可待ちの終了を待っている
    quint64 m_revision = 1;                            // 現在の選択改訂番号
    quint64 m_resolvedRevision = 0;                    // 解決成功時の選択改訂番号
    ZyppExecutor m_executor;                           // libzypp専用ワーカーと問合せ管理
    PolkitAuthorization m_authorization;               // 非同期Polkit認可器
    bool m_initialized = false;                        // libzyppセッション初期化済みか
    QTimer m_watchdog;                                 // 無進捗を監視する定期タイマ
    QVariantMap m_publishedState;                      // 最後に所有者へ通知した操作状態
    QString m_authId;                                  // 現在保留中の操作認可ID

    /**
     * @brief Initializeの認可待ち1件分
     */
    struct PendingInit {
        QString caller;                                // 呼出元の一意バス名
        uint uid = 0;                                  // 呼出元のUID (GetConnectionUnixUser)
        QDBusMessage msg;                              // 遅延返信する呼出
        QString authId;                                // 保留中のPolkit認可ID
    };

    static constexpr int kMaxPendingInitAuths = 8;        // 認可待ちの全体上限
    static constexpr int kMaxPendingInitAuthsPerUid = 2;  // 認可待ちのUIDごとの上限

    QMap<QString, PendingInit> m_pendingInitCalls;        // 呼出元ごとの初期化認可待ち要求
};

} // namespace qZypper

#endif // QZYPPER_BACKEND_PACKAGEMANAGERADAPTOR_H
