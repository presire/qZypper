#ifndef QZYPPER_GUI_CONTROLLERS_PACKAGECONTROLLER_H
#define QZYPPER_GUI_CONTROLLERS_PACKAGECONTROLLER_H

#include <QObject>
#include <QQmlEngine>
#include <QPalette>
#include <QVariantList>
#include <QVariantMap>
#include <QUrl>
#include <functional>
#include <vector>

namespace qZypper {

class DBusClient;
class PackageListModel;
class PackageFilterProxyModel;

/**
 * @class PackageController
 * @brief QML_SINGLETONとして公開されるメインコントローラ
 *
 * QMLから全UI操作を仲介し、DBusClient経由でバックエンドと通信する
 * 全D-Bus呼び出しは非同期であり、結果はNOTIFYプロパティまたはシグナルでQMLへ通知される (同期戻り値には依存しない)
 */
class PackageController : public QObject
{
    Q_OBJECT

    Q_PROPERTY(bool connected READ isConnected NOTIFY connectedChanged)
    Q_PROPERTY(bool initialized READ isInitialized NOTIFY initializedChanged)
    Q_PROPERTY(bool busy READ isBusy NOTIFY busyChanged)
    Q_PROPERTY(QString statusMessage READ statusMessage NOTIFY statusMessageChanged)
    Q_PROPERTY(QVariantList packages READ packages NOTIFY packagesChanged)
    Q_PROPERTY(QVariantList repos READ repos NOTIFY reposChanged)
    Q_PROPERTY(QVariantList services READ services NOTIFY servicesChanged)
    Q_PROPERTY(QVariantList patterns READ patterns NOTIFY patternsChanged)
    Q_PROPERTY(QVariantMap currentPackageDetails READ currentPackageDetails NOTIFY currentPackageDetailsChanged)
    Q_PROPERTY(bool hasPendingChanges READ hasPendingChanges NOTIFY pendingChangesChanged)
    Q_PROPERTY(QVariantList pendingChanges READ pendingChanges NOTIFY pendingChangesChanged)
    Q_PROPERTY(QVariantList diskUsage READ diskUsage NOTIFY diskUsageChanged)
    Q_PROPERTY(QVariantMap commitResult READ commitResult NOTIFY commitResultChanged)
    Q_PROPERTY(int listGeneration READ listGeneration NOTIFY packagesChanged)
    Q_PROPERTY(QObject *packageModel READ packageModel CONSTANT)
    Q_PROPERTY(QObject *packageProxy READ packageProxy CONSTANT)
    Q_PROPERTY(QVariantMap operationState READ operationState NOTIFY operationStateChanged)
    Q_PROPERTY(QVariantList pendingPrompts READ pendingPrompts NOTIFY pendingPromptsChanged)
    Q_PROPERTY(QVariantMap currentPrompt READ currentPrompt NOTIFY pendingPromptsChanged)
    Q_PROPERTY(QVariantList patches READ patches NOTIFY patchesChanged)
    Q_PROPERTY(QVariantList locks READ locks NOTIFY locksChanged)
    Q_PROPERTY(QVariantMap transactionSummary READ transactionSummary NOTIFY transactionSummaryChanged)
    Q_PROPERTY(QString searchError READ searchError NOTIFY searchErrorChanged)

public:
    explicit PackageController(QObject *parent = nullptr); /* QML用コントローラと関連モデルを初期化する */
    ~PackageController() override;                         /* 保持する表示キャッシュを破棄する */

    bool isConnected() const { return m_connected; }                         // バックエンド接続状態
    bool isInitialized() const { return m_initialized; }                     // 初期化完了状態
    bool isBusy() const { return m_busy; }                                   // 処理中フラグ
    QString statusMessage() const { return m_statusMessage; }                // ステータスメッセージ
    QVariantList packages() const { return m_packages; }                     // パッケージ一覧
    QVariantList repos() const { return m_repos; }                           // リポジトリ一覧
    QVariantList services() const { return m_services; }                     // サービス一覧
    QVariantList patterns() const { return m_patterns; }                     // パターン一覧
    QVariantList patches() const { return m_patches; }                       // パッチ一覧
    QVariantList locks() const { return m_locks; }                           // パッケージロック一覧
    QVariantMap transactionSummary() const { return m_transactionSummary; }  // トランザクション概要
    QString searchError() const { return m_searchError; }                    // 検索パラメータエラー
    QVariantMap currentPackageDetails() const {                              // 選択中の詳細
        return m_currentDetails;
    }
    QVariantList pendingChanges() const { return m_pendingChanges; }  // 変更予定一覧 (キャッシュ)
    QVariantList diskUsage() const { return m_diskUsage; }            // ディスク使用量 (キャッシュ)
    QVariantMap commitResult() const { return m_commitResult; }       // コミット結果
    int listGeneration() const { return m_listGeneration; }           // パッケージ一覧の世代 (表示集合の作り直し判定用)
    bool hasPendingChanges() const { return m_hasPendingChanges; }    // 変更予定の有無 (キャッシュ、I/Oなし)
    QObject *packageModel() const;                                    // パッケージ一覧モデル (QML公開)
    QObject *packageProxy() const;                                    // フィルタ/ソートプロキシ (QML公開)
    QVariantMap operationState() const { return m_operationState; }   // 現在の操作状態 (Wave1)
    QVariantList pendingPrompts() const { return m_prompts; }         // 未応答プロンプトのキュー
    QVariantMap currentPrompt() const;                                // 先頭の未応答プロンプト

    // QMLから呼び出し可能なメソッド (全て非同期、結果はNOTIFYプロパティ/シグナルで通知)
    Q_INVOKABLE void initialize();                                                     // パッケージマネージャ初期化
    Q_INVOKABLE void searchPackages(const QString &query, int flags = 0x07);           // パッケージ検索
    Q_INVOKABLE void loadRepos();                                                      // リポジトリ一覧読込
    Q_INVOKABLE void loadPackagesByRepo(const QString &repoAlias);                     // リポ別パッケージ読込
    Q_INVOKABLE void loadPackageDetails(const QString &name);                          // パッケージ詳細読込
    Q_INVOKABLE void clearPackageDetails();                                            // パッケージ詳細クリア
    Q_INVOKABLE void loadPatterns();                                                   // パターン一覧読込
    Q_INVOKABLE void loadPackagesByPattern(const QString &patternName);                // パターン別パッケージ読込
    Q_INVOKABLE void loadPatches(int category = -1);                                   // パッチ一覧読込
    Q_INVOKABLE void distUpgrade(bool allowVendorChange);                              // ディストリビューション更新
    Q_INVOKABLE void verifySystem();                                                   // システム整合性検証
    Q_INVOKABLE void setPatchStatus(const QString &patchId, int status);               // パッチ状態変更
    Q_INVOKABLE void selectAllNeededPatches(bool securityOnly, bool includeOptional);  // 必要パッチ一括選択
    Q_INVOKABLE void searchPackagesEx(const QString &query, int flags,
                                      const QString &match, bool installedOnly);  // 高度な検索
    Q_INVOKABLE void loadClassification(const QString &classification);           // 分類別パッケージ読込
    Q_INVOKABLE void loadLocks();                                                 // ロック一覧読込
    Q_INVOKABLE void addLock(const QVariantMap &lock);                            // ロック追加
    Q_INVOKABLE void removeLock(const QVariantMap &lock);                         // ロック削除
    Q_INVOKABLE void loadHistory(const QVariantMap &filter);                      // 履歴読込
    Q_INVOKABLE void requestTransactionSummary();                                 // トランザクション概要取得
    Q_INVOKABLE void saveSolverTestcase();                                        // ソルバーテストケース保存
    Q_INVOKABLE void installLocalRpms(const QVariantList &fileUrls);              // ローカルRPMを選択 (localRpmsFinished発行)
    Q_INVOKABLE void previewRepoFile(const QUrl &fileUrl);                        // .repoを読込・解析 (repoFilePreviewReady発行)
    Q_INVOKABLE void importRepoFile(const QString &content);                      // .repoを取込 (repoFileImported発行)

    Q_INVOKABLE void setPackageStatus(const QString &name, int status);       // パッケージ状態変更
    Q_INVOKABLE void setPackageVersion(const QString &name,                   // パッケージバージョン変更
                                       const QString &version,
                                       const QString &arch,
                                       const QString &repoAlias);
    Q_INVOKABLE void togglePatternStatus(const QString &patternName);         // パターン状態トグル
    Q_INVOKABLE void updateAllPackages();                                     // 全パッケージ更新 (updateAllFinished発行)
    Q_INVOKABLE void resolveDependencies();                                   // 依存関係解決 (resolveFinished発行)
    Q_INVOKABLE void applySolution(int problemIndex, int solutionIndex);      // 解決策適用
    Q_INVOKABLE void prepareCommit();                                         // コミット用リビジョン取得 (commitPrepared発行)
    Q_INVOKABLE void commitAsync();                                           // 変更を非同期コミット
    Q_INVOKABLE void trustKey(const QString &fingerprint);                    // 署名鍵を信頼 (keyTrustFinished発行)
    Q_INVOKABLE void cancelOperation();                                       // 操作キャンセル

    Q_INVOKABLE void addRepo(const QString &url, const QString &name);        // リポ追加 (addRepoFinished発行)
    Q_INVOKABLE void addRepoFull(const QVariantMap &properties);              // リポ追加 (全属性)
    Q_INVOKABLE void removeRepo(const QString &alias);                        // リポ削除
    Q_INVOKABLE void setRepoEnabled(const QString &alias, bool enabled);      // リポ有効/無効
    Q_INVOKABLE void modifyRepo(const QString &alias,                         // リポ変更
                                const QVariantMap &properties);
    Q_INVOKABLE void refreshReposAsync();                                     // 全リポ非同期リフレッシュ
    Q_INVOKABLE void cancelRefresh();                                         // リフレッシュキャンセル
    Q_INVOKABLE void refreshSingleRepo(const QString &alias);                 // 個別リポ非同期リフレッシュ

    // サービス管理
    Q_INVOKABLE void loadServices();                                        // サービス一覧読込
    Q_INVOKABLE void addService(const QString &url, const QString &alias);  // サービス追加 (addServiceFinished発行)
    Q_INVOKABLE void removeService(const QString &alias);                   // サービス削除
    Q_INVOKABLE void modifyService(const QString &alias,                    // サービス変更
                                   const QVariantMap &properties);
    Q_INVOKABLE void refreshService(const QString &alias);                  // サービス非同期リフレッシュ
    Q_INVOKABLE void requestDiskUsage();                                    // ディスク使用量取得 (diskUsageChanged発行)
    Q_INVOKABLE void applyDarkMode(bool dark);                              // ダークモード適用
    Q_INVOKABLE void saveState();                                           // 選択状態を保存
    Q_INVOKABLE void restoreState();                                        // 選択状態を復元

    // 操作状態 / プロンプト / セッション (Wave1)
    Q_INVOKABLE void answerPrompt(uint promptId, const QString &action);    // プロンプトへ応答
    Q_INVOKABLE void openExternalUrl(const QString &url);                   // http(s) URLをポータル経由で開く
    Q_INVOKABLE void releaseSession();                                      // 編集セッションを解放
    Q_INVOKABLE void retryOperation();                                      // ロック等で失敗した操作を再試行

signals:
    void connectedChanged();                                // 接続状態変更
    void initializedChanged();                              // 初期化完了状態変更
    void busyChanged();                                     // ビジー状態変更
    void statusMessageChanged();                            // ステータスメッセージ変更
    void packagesChanged();                                 // パッケージ一覧変更
    void reposChanged();                                    // リポジトリ一覧変更
    void servicesChanged();                                 // サービス一覧変更
    void patternsChanged();                                 // パターン一覧変更
    void currentPackageDetailsChanged();                    // パッケージ詳細変更
    void pendingChangesChanged();                           // 変更予定一覧 / 有無変更
    void diskUsageChanged();                                // ディスク使用量変更

    void progressChanged(const QString &packageName,        // 操作進捗
                         int percentage,
                         const QString &stage);
    void commitProgressChanged(const QString &packageName,  // コミット進捗 (詳細版)
                               int percentage,
                               const QString &stage,
                               int totalSteps,
                               int completedSteps,
                               int overallPercentage);
    void commitResultChanged();                             // コミット結果変更
    void transactionFinished(bool success,                  // トランザクション完了
                             const QString &summary);
    void errorOccurred(const QString &errorMessage);        // エラー通知
    void packageStateChanged(const QString &packageName,    // パッケージ状態遷移
                             const QString &event);
    void refreshFinished(bool success);                     // リフレッシュ完了
    void conflictsDetected(const QVariantList &problems);   // コンフリクト検出
    void untrustedKeyDetected(const QVariantMap &keyInfo);  // 未信頼鍵検出

    void updateAllFinished(const QVariantMap &result);      // 全更新完了 (success/problems/backendCrashed)
    void resolveFinished(const QVariantMap &result);        // 依存解決完了 (success/problems/backendCrashed)
    void commitPrepared(bool ok);                           // コミット準備完了 (リビジョン取得成功)
    void addRepoFinished(bool success);                     // リポジトリ追加完了
    void addServiceFinished(bool success);                  // サービス追加完了
    void keyTrustFinished(bool success);                    // 署名鍵承認完了

    void operationStateChanged();                                  // 操作状態変更
    void pendingPromptsChanged();                                  // プロンプトキュー変更
    void promptRequested(const QVariantMap &prompt);               // 対話プロンプト要求
    void sessionReleased(bool released);                           // 編集セッション解放完了
    void packageLocked(const QVariantMap &lockInfo);               // Zyppロック検出
    void poolReloaded();                                           // プール再読込通知
    void patchesChanged();                                         // パッチ一覧更新
    void locksChanged();                                           // ロック一覧更新
    void transactionSummaryChanged();                              // トランザクション概要更新
    void searchErrorChanged();                                     // 検索パラメータエラー更新
    void distUpgradeFinished(const QVariantMap &result);           // ディストリビューション更新結果
    void verifyFinished(const QVariantMap &result);                // システム検証結果
    void selectPatchesFinished(const QVariantMap &result);         // パッチ一括選択結果
    void historyResultReady(const QVariantList &entries);          // 履歴取得結果
    void lockFinished(bool success);                               // ロック追加/削除結果
    void solverTestcaseCreated(const QString &path);               // テストケース保存パス
    void scriptOutputReceived(const QVariantMap &line);            // RPMスクリプト出力 (1行)
    void localRpmsFinished(const QVariantMap &result);             // ローカルRPM選択結果 (success/problems/summary)
    void repoFilePreviewReady(bool ok, const QVariantList &repos,  // .repo解析結果
                              const QString &content, const QString &error);
    void repoFileImported(const QVariantMap &result);              // .repo取込結果 (success/added/skipped)

private:
    enum class PackageSource { None, Search, Repo, Pattern, Patches, Classification }; /**< パッケージ一覧の取得元 */

    void setBusy(bool busy);                                  /* ビジー状態を更新して通知する */
    void setStatusMessage(const QString &msg);                /* ステータスメッセージを更新して通知する */
    void setSearchError(const QString &err);                  /* 検索パラメータエラーを更新して通知する */
    void cacheTransactionSummary(const QVariantMap &result);  /* ソルバー結果内の概要をキャッシュする */

    /**
     * @brief 現在のソースに応じてパッケージ一覧を非同期で再取得する
     *
     * リクエストシリアルにより古い応答を破棄し、要求の連打を抑止する
     * @param newGeneration 取得後に世代カウンタを進める場合はtrue
     * @param onDone 完了コールバック (成功可否と取得済みリスト)
     */
    void requestPackageList(bool newGeneration,
                            std::function<void(bool, const QVariantList &)> onDone);
    void refreshPackageStatuses(bool newGeneration = false);            /* シリアル管理下でパッケージ一覧を再取得する */
    void refreshPendingChanges();                                       /* 合体と直列化を行って変更予定を再取得する */
    void refreshPendingChangesThen(std::function<void(bool)> onDone);   /* 選択状態反映後に完了通知する */
    void onRepoFileRead(quint64 request, bool readOk,
                        const QByteArray &contentBytes,
                        const QString &readError);                      /* GUIスレッドで.repo読み込み結果を処理する */
    void fetchCommitRevision(int attemptsLeft);                         /* revisionと変更予定の整合を取得する */
    void clearPrompts();                                                /* 未応答プロンプトを全て破棄する */

    /**
     * @brief バックエンド切断時に再接続と再初期化を非同期で試みる
     * @param onDone 完了コールバック (再接続・初期化成功時はtrue)
     */
    void reconnectBackend(std::function<void(bool)> onDone);

    void setOperationState(const QVariantMap &state);                // 操作状態を更新して通知
    bool removePrompt(uint promptId);                                // キューから除去 (存在した場合はtrue)
    void enqueuePrompt(uint promptId, const QString &type,           // プロンプトをキューへ追加
                       const QVariantMap &data);
    void handlePackageLocked(const QString &lockerName,              // Zyppロック検出をQMLへ通知
                             int lockerPid,
                             const QString &message);

    DBusClient *m_dbus = nullptr;                                    // D-Busクライアント
    PackageListModel *m_packageModel = nullptr;                      // パッケージ一覧モデル
    PackageFilterProxyModel *m_packageProxy = nullptr;               // フィルタ/ソートプロキシ
    QVariantMap m_operationState;                                    // 現在の操作状態
    QVariantList m_prompts;                                          // 未応答プロンプトのキュー
    std::function<void()> m_retryAction;                             // 直近操作の再試行アクション
    bool m_connected = false;                                        // バックエンド接続状態
    bool m_initialized = false;                                      // 初期化完了状態
    bool m_busy = false;                                             // 処理中フラグ
    QString m_statusMessage;                                         // ステータスメッセージ
    QVariantList m_packages;                                         // パッケージ一覧キャッシュ
    QVariantList m_repos;                                            // リポジトリ一覧キャッシュ
    QVariantList m_services;                                         // サービス一覧キャッシュ
    QVariantList m_patterns;                                         // パターン一覧キャッシュ
    QVariantList m_patches;                                          // パッチ一覧キャッシュ
    int m_patchCategory = -1;                                        // 表示中のパッチカテゴリ (-1 = 全て)
    QVariantList m_locks;                                            // ロック一覧キャッシュ
    QVariantMap m_transactionSummary;                                // トランザクション概要キャッシュ
    QString m_searchError;                                           // 直近の検索パラメータエラー
    QVariantMap m_currentDetails;                                    // 選択中パッケージ詳細
    QVariantList m_pendingChanges;                                   // 変更予定一覧キャッシュ
    QVariantList m_diskUsage;                                        // ディスク使用量キャッシュ
    QVariantMap m_commitResult;                                      // コミット結果
    bool m_hasPendingChanges = false;                                // 変更予定有無キャッシュ
    quint64 m_commitRevision = 0;                                    // コミット用選択リビジョン (0 = 未確定)
    PackageSource m_lastSource = PackageSource::None;                // 最後のパッケージソース
    QString m_lastSourceParam;                                       // 最後のソースパラメータ
    int m_lastSearchFlags = 0x07;                                    // 最後の検索フラグ
    QString m_lastSearchMatch = QStringLiteral("substring");         // 最後の検索マッチ方式
    bool m_lastSearchInstalledOnly = false;                          // 最後の検索のインストール済み限定
    bool m_useExSearch = false;                                      // 高度な検索 (SearchPackagesEx) を使用
    int m_listGeneration = 0;                                        // パッケージ一覧の世代カウンタ
    quint64 m_listRequestSerial = 0;                                 // パッケージ一覧要求のシリアル (古い応答破棄用)
    quint64 m_detailsRequestSerial = 0;                              // 詳細要求のシリアル (古い応答破棄用)
    bool m_pendingRefreshInFlight = false;                           // 変更予定取得が実行中
    bool m_pendingRefreshQueued = false;                             // 変更予定取得の再要求が待機中
    std::vector<std::function<void(bool)>> m_pendingRefreshWaiters;  // 変更予定取得完了の待機者
    quint64 m_repoPreviewRequest = 0;                                // .repoプレビュー読み込みの最新要求番号
};

} // namespace qZypper

#endif // QZYPPER_GUI_CONTROLLERS_PACKAGECONTROLLER_H
