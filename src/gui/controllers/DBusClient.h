#ifndef QZYPPER_GUI_CONTROLLERS_DBUSCLIENT_H
#define QZYPPER_GUI_CONTROLLERS_DBUSCLIENT_H

#include <QObject>
#include <QDBusInterface>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusVariant>
#include <QDBusUnixFileDescriptor>
#include <QDBusServiceWatcher>
#include <QVariantMap>
#include <QVariantList>
#include <deque>
#include <functional>


namespace qZypper {

/**
 * @brief D-Busバックエンドとの通信を担当するクライアントクラス。
 *
 * system bus経由でqzypper-backendのメソッドを非同期で呼び出し、シグナルを中継する。
 * 全メソッド呼び出しはQDBusPendingCallWatcherベースで行われ、GUIスレッドを
 * ブロックしない。結果は呼び出しごとの完了コールバックで通知される。
 * デマーシャリング処理を内包する。
 */
class DBusClient : public QObject
{
    Q_OBJECT

public:
    /**
     * @brief 非同期D-Bus呼び出しの完了通知コールバック。
     * @param success D-Bus転送が成功し正常応答が得られた場合 true
     * @param errorName D-Busエラー名 (成功時は空)
     * @param errorMessage エラーメッセージ (成功時は空)
     * @param args 正規化済みの戻り値。成功時のみ設定される。
     *             読み取り系は {QVariantList} / {QVariantMap}、bool系は {bool}
     */
    using ReplyCallback = std::function<void(bool success,
                                             const QString &errorName,
                                             const QString &errorMessage,
                                             const QVariantList &args)>;

    explicit DBusClient(QObject *parent = nullptr);
    ~DBusClient() override;

    bool connectToBackend();                                        // バックエンドに接続
    bool isConnected() const;                                       // 接続状態を返す
    QString lastError() const { return m_lastError; }               // 最新エラーメッセージ

    // 初期化 (バックエンド側)
    void initializeBackend(ReplyCallback callback);

    // リポジトリ操作 (特権)
    void refreshReposAsync(ReplyCallback callback);                  // 全リポ非同期リフレッシュ
    void refreshSingleRepoAsync(const QString &alias,                // 個別リポ非同期リフレッシュ
                                ReplyCallback callback);
    void cancelOperation();                                         // 操作キャンセル
    void addRepo(const QString &url, const QString &name,            // リポ追加
                 ReplyCallback callback);
    void addRepoFull(const QVariantMap &properties,                  // リポ追加 (全属性)
                     ReplyCallback callback);
    void removeRepo(const QString &alias, ReplyCallback callback);    // リポ削除
    void setRepoEnabled(const QString &alias, bool enabled,           // リポ有効 / 無効
                        ReplyCallback callback);
    void modifyRepo(const QString &alias, const QVariantMap &properties,  // リポ変更
                    ReplyCallback callback);

    // サービス操作 (特権)
    void addService(const QString &url, const QString &alias,        // サービス追加
                    ReplyCallback callback);
    void removeService(const QString &alias, ReplyCallback callback);  // サービス削除
    void modifyService(const QString &alias, const QVariantMap &properties,  // サービス変更
                       ReplyCallback callback);
    void refreshServiceAsync(const QString &alias, ReplyCallback callback);  // サービスリフレッシュ

    // パッケージ状態変更
    void setPackageStatus(const QString &name, int status,          // パッケージ状態変更
                          ReplyCallback callback);
    void setPatchStatus(const QString &patchId, int status,         // パッチ状態変更
                        ReplyCallback callback);
    void selectAllNeededPatches(const QVariantMap &options,         // 必要パッチ一括選択
                                ReplyCallback callback);
    void setPackageVersion(const QString &name, const QString &version,  // パッケージ版変更
                           const QString &arch, const QString &repoAlias,
                           ReplyCallback callback);
    void setPatternStatus(const QString &name, int status,          // パターン状態変更
                          ReplyCallback callback);

    // 依存関係解決
    void updateAllPackages(ReplyCallback callback);                  // 全パッケージ更新
    void distUpgrade(const QVariantMap &options,                     // ディストリビューション更新
                     ReplyCallback callback);
    void verifySystem(ReplyCallback callback);                       // システム整合性検証
    void resolveDependencies(ReplyCallback callback);                // 依存関係解決
    void applySolution(int problemIndex, int solutionIndex,          // 解決策適用
                       ReplyCallback callback);

    // コミット
    void getSelectionRevision(ReplyCallback callback);               // 選択リビジョン取得
    void commitAsync(quint64 expectedRevision, ReplyCallback callback);  // 非同期コミット

    // 署名鍵
    void trustKey(const QString &fingerprint, ReplyCallback callback);  // 署名鍵承認

    // 読み取り操作
    void getRepos(ReplyCallback callback);                           // リポジトリ一覧
    void searchPackages(const QString &query, int flags,             // パッケージ検索
                        ReplyCallback callback);
    void searchPackagesEx(const QVariantMap &options,                // 高度な検索 (マッチ方式等)
                          ReplyCallback callback);
    void getPackagesByClassification(const QString &classification,  // 分類別パッケージ一覧
                                      ReplyCallback callback);
    void getPackageDetails(const QString &name, ReplyCallback callback);  // パッケージ詳細
    void getPackagesByRepo(const QString &repoAlias,                 // リポジトリ別一覧
                           ReplyCallback callback);
    void getPatterns(ReplyCallback callback);                        // パターン一覧
    void getPackagesByPattern(const QString &patternName,            // パターン別一覧
                              ReplyCallback callback);
    void getPatches(int category, ReplyCallback callback);           // パッチ一覧
    void getServices(ReplyCallback callback);                        // サービス一覧
    void getDiskUsage(ReplyCallback callback);                       // ディスク使用量
    void getPendingChanges(ReplyCallback callback);                  // 変更予定一覧
    void getTransactionSummary(ReplyCallback callback);              // トランザクション概要 (リスク情報込み)
    void getLocks(ReplyCallback callback);                           // パッケージロック一覧
    void addLock(const QVariantMap &lock, ReplyCallback callback);   // パッケージロック追加
    void removeLock(const QVariantMap &lock, ReplyCallback callback);  // パッケージロック削除
    void getHistory(const QVariantMap &filter, ReplyCallback callback);  // zypp履歴一覧
    void createSolverTestcase(ReplyCallback callback);               // ソルバーテストケース作成

    // 状態保存・復元
    void saveState();                                               // 選択状態の保存 (fire-and-forget)
    void restoreState(ReplyCallback callback);                       // 選択状態の復元

    // 操作状態 (Wave1)
    void getOperationState(ReplyCallback callback);                  // 現在の操作状態取得 (a{sv})

    // 対話プロンプト (Wave1)
    void answerPrompt(uint promptId, const QVariantMap &answer,      // プロンプトへの応答
                      ReplyCallback callback);
    void releaseSession(ReplyCallback callback);                     // 編集セッション解放

    // ローカルRPM / .repo 取り込み
    void installLocalRpms(const QList<QDBusUnixFileDescriptor> &files,  // ローカルRPMのインストール選択
                          const QStringList &fileNames, ReplyCallback callback);
    void parseRepoFile(const QString &content,                       // .repo ファイルの解析 (読み取り専用)
                       ReplyCallback callback);
    void importRepoFile(const QString &content,                      // .repo ファイルの取り込み
                        ReplyCallback callback);

signals:
    void lastErrorChanged();                                    // 最新エラー変更
    void connectedChanged();                                    // 接続状態変更
    void backendConnected();                                    // バックエンド接続
    void backendDisconnected();                                 // バックエンド切断
    void backendReconnected();                                  // バックエンド再接続
    void repoRefreshProgress(const QString &alias,              // リポジトリリフレッシュ進捗
                             int percentage);
    void progressChanged(const QString &packageName,            // 操作進捗
                         int percentage,
                         const QString &stage);
    void commitProgressChanged(const QString &packageName,      // コミット進捗 (詳細版)
                               int percentage,
                               const QString &stage,
                               int totalSteps,
                               int completedSteps,
                               int overallPercentage);
    void transactionFinished(bool success,                      // トランザクション完了
                             const QString &summary);
    void errorOccurred(const QString &errorMessage);            // エラー通知
    void packageStateChanged(const QString &packageName,        // パッケージ状態遷移
                             const QString &event);
    void untrustedKeyDetected(const QVariantMap &keyInfo);      // 未信頼鍵検出
    void operationStateChanged(const QVariantMap &state);       // 操作状態変更 (Wave1)
    void promptRequested(uint promptId,                         // 対話プロンプト要求 (Wave1)
                         const QString &promptType,
                         const QVariantMap &data);
    void promptClosed(uint promptId);                           // プロンプト終了 (応答/タイムアウト/取消)
    void poolReloaded();                                        // パッケージプール再読込 (Wave1)
    void packageLocked(const QString &lockerName,               // Zyppロック検出 (Wave1)
                       int lockerPid,
                       const QString &message);
    void scriptOutput(const QVariantMap &line);                 // RPMスクリプト出力 (1行)

private slots:
    // 入れ子の QDBusArgument を正規化してから中継する
    void onUntrustedKeyDetected(const QVariantMap &keyInfo);
    void onOperationStateChanged(const QVariantMap &state);
    void onPromptRequested(uint promptId, const QString &promptType, const QVariantMap &data);
    void onScriptOutput(const QVariantMap &line);

private:
    static constexpr const char* SERVICE_NAME = "org.presire.qzypper";
    static constexpr const char* OBJECT_PATH  = "/org/presire/qzypper";
    static constexpr const char* INTERFACE     = "org.presire.qzypper.PackageManager";

    QDBusInterface *m_iface = nullptr;                  // D-Busインターフェース
    QDBusServiceWatcher *m_watcher = nullptr;           // サービスウォッチャー
    bool m_connected = false;                           // 接続状態
    bool m_initializing = false;                        // 初期化呼び出し中 (再入防止)
    QString m_lastError;                                // 最新エラーメッセージ

    /**
     * @brief D-Busメソッドを非同期で呼び出し、完了時にコールバックへ結果を渡す。
     *
     * GUIスレッドをブロックしないようQDBusPendingCallWatcherを使用する。
     * エラー時はm_lastErrorを更新し、コールバックへエラー名/メッセージを返す。
     * @param msg 送信するメソッド呼び出しメッセージ
     * @param timeout タイムアウト (ミリ秒)
     * @param callback 完了時に呼ばれるコールバック
     */
    void callAsync(const QDBusMessage &msg, int timeout, ReplyCallback callback);
    void dispatchCall(const QDBusMessage &msg, int timeout, ReplyCallback callback, bool serialized);
    void dispatchNextQueuedCall();

    /** @brief バックエンドは同時に1操作しか受け付けないため、直列化待ちの呼び出し */
    struct QueuedCall {
        QDBusMessage msg;
        int timeout;
        ReplyCallback callback;
    };
    std::deque<QueuedCall> m_callQueue;   // 直列化対象の待ち行列
    bool m_serialCallInFlight = false;    // 直列化対象の呼び出しが応答待ちか

    /**
     * @brief 世代番号。バックエンド再接続/切断のたびに増加する。
     *
     * 送信時点の世代と不一致になった応答はバックエンド消滅後の古い応答とみなし破棄する。
     */
    quint64 m_generation = 0;

    /** @brief バックエンド消滅時に待機中の呼び出しを破棄し、応答を無効化する。 */
    void resetPendingCalls();

    void onServiceRegistered(const QString &serviceName);    // サービス登録ハンドラ
    void onServiceUnregistered(const QString &serviceName);  // サービス解除ハンドラ
};

} // namespace qZypper

#endif // QZYPPER_GUI_CONTROLLERS_DBUSCLIENT_H
