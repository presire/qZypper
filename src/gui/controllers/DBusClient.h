#ifndef QZYPPER_GUI_CONTROLLERS_DBUSCLIENT_H
#define QZYPPER_GUI_CONTROLLERS_DBUSCLIENT_H

#include <QObject>
#include <QDBusInterface>
#include <QDBusConnection>
#include <QDBusReply>
#include <QDBusVariant>
#include <QDBusPendingCall>
#include <QDBusPendingCallWatcher>
#include <QDBusServiceWatcher>
#include <QVariantMap>
#include <QVariantList>


namespace qZypper {

/**
 * @brief D-Busバックエンドとの通信を担当するクライアントクラス。
 *
 * system bus経由でqzypper-backendのメソッドを呼び出し、シグナルを中継する。
 * デマーシャリング処理を内包する。
 */
class DBusClient : public QObject
{
    Q_OBJECT

public:
    explicit DBusClient(QObject *parent = nullptr);
    ~DBusClient() override;

    bool connectToBackend();                                        // バックエンドに接続
    bool isConnected() const;                                       // 接続状態を返す
    QString lastError() const { return m_lastError; }               // 最新エラーメッセージ

    // 初期化 (バックエンド側)
    bool initializeBackend();

    // リポジトリ操作 (特権)
    bool refreshRepos();                                            // 全リポ同期リフレッシュ
    void refreshReposAsync();                                       // 全リポ非同期リフレッシュ
    bool refreshSingleRepo(const QString &alias);                   // 個別リポリフレッシュ
    void cancelOperation();                                         // 操作キャンセル
    QVariantMap addRepo(const QString &url, const QString &name);   // リポ追加
    QVariantMap addRepoFull(const QVariantMap &properties);         // リポ追加 (全属性)
    bool removeRepo(const QString &alias);                          // リポ削除
    bool setRepoEnabled(const QString &alias, bool enabled);        // リポ有効 / 無効
    bool modifyRepo(const QString &alias,                           // リポ変更
                    const QVariantMap &properties);

    // サービス操作 (特権)
    bool addService(const QString &url, const QString &alias);      // サービス追加
    bool removeService(const QString &alias);                       // サービス削除
    bool modifyService(const QString &alias,                        // サービス変更
                       const QVariantMap &properties);
    bool refreshService(const QString &alias);                      // サービスリフレッシュ

    // パッケージ状態変更 (バックエンドのプール操作)
    bool setPackageStatus(const QString &name, int status);         // パッケージ状態変更
    bool setPackageVersion(const QString &name,                    // パッケージバージョン変更
                           const QString &version,
                           const QString &arch,
                           const QString &repoAlias);
    bool setPatternStatus(const QString &name, int status);         // パターン状態変更

    // 全パッケージ更新
    QVariantMap updateAllPackages();                                // 全パッケージ更新

    // 依存関係解決
    QVariantMap resolveDependencies();                              // ソルバー実行
    bool applySolution(int problemIndex, int solutionIndex);        // 解決策適用

    // コミット (特権)
    QVariantMap commit();                                           // 変更をコミット (同期)
    void commitAsync();                                             // 変更をコミット (非同期)

    // 読み取り操作 (D-Bus経由)
    QVariantList getRepos();                                        // リポジトリ一覧取得
    QVariantList searchPackages(const QString &query, int flags);   // パッケージ検索
    QVariantMap getPackageDetails(const QString &name);             // パッケージ詳細取得
    QVariantList getPackagesByRepo(const QString &repoAlias);       // リポ別パッケージ
    QVariantList getPatterns();                                     // パターン一覧取得
    QVariantList getPackagesByPattern(const QString &patternName);  // パターン別パッケージ
    QVariantList getPatches(int category);                          // パッチ一覧取得
    QVariantList getServices();                                     // サービス一覧取得
    QVariantList getDiskUsage();                                    // ディスク使用量取得

    // 変更予定パッケージ一覧
    QVariantList getPendingChanges();                   // 変更予定一覧取得

    // 状態保存・復元
    void saveState();                                   // 選択状態を保存
    void restoreState();                                // 選択状態を復元

signals:
    void backendConnected();                            // バックエンド接続完了
    void backendDisconnected();                         // バックエンド接続断
    void backendReconnected();                          // バックエンド再接続 (再起動後)
    void refreshReposFinished(bool success);            // 非同期リフレッシュ完了
    void repoRefreshProgress(const QString &repoAlias,  // リフレッシュ進捗
                             int percentage);
    void progressChanged(const QString &packageName,    // 操作進捗
                         int percentage,
                         const QString &stage);
    void commitProgressChanged(const QString &packageName,  // コミット進捗 (詳細版)
                               int percentage,
                               const QString &stage,
                               int totalSteps,
                               int completedSteps,
                               int overallPercentage);
    void commitFinished(const QVariantMap &result);     // 非同期コミット完了
    void transactionFinished(bool success,              // トランザクション完了
                             const QString &summary);
    void errorOccurred(const QString &errorMessage);    // エラー通知
    void packageStateChanged(const QString &packageName,  // パッケージ状態遷移
                             const QString &event);

private:
    static constexpr const char* SERVICE_NAME = "org.presire.qzypper";
    static constexpr const char* OBJECT_PATH  = "/org/presire/qzypper";
    static constexpr const char* INTERFACE     = "org.presire.qzypper.PackageManager";

    QDBusInterface *m_iface = nullptr;                  // D-Busインターフェース
    QDBusServiceWatcher *m_watcher = nullptr;           // サービスウォッチャー
    bool m_connected = false;                           // 接続状態
    QString m_lastError;                                // 最新エラーメッセージ

    void onServiceRegistered(const QString &serviceName);    // サービス登録ハンドラ
    void onServiceUnregistered(const QString &serviceName);  // サービス解除ハンドラ
};

} // namespace qZypper

#endif // QZYPPER_GUI_CONTROLLERS_DBUSCLIENT_H
