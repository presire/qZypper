#ifndef QZYPPER_GUI_CONTROLLERS_PACKAGECONTROLLER_H
#define QZYPPER_GUI_CONTROLLERS_PACKAGECONTROLLER_H

#include <QObject>
#include <QQmlEngine>
#include <QPalette>
#include <QVariantList>
#include <QVariantMap>


namespace qZypper {

class DBusClient;

/**
 * @brief QML_SINGLETONとして公開されるメインコントローラ。
 *
 * QMLから全UI操作を仲介し、DBusClient 経由でバックエンドと通信する。
 * パッケージ検索・状態変更・リポジトリ管理・ソルバー・コミット等の全機能を提供。
 */
class PackageController : public QObject
{
    Q_OBJECT

    Q_PROPERTY(bool connected READ isConnected NOTIFY connectedChanged)
    Q_PROPERTY(bool busy READ isBusy NOTIFY busyChanged)
    Q_PROPERTY(QString statusMessage READ statusMessage NOTIFY statusMessageChanged)
    Q_PROPERTY(QVariantList packages READ packages NOTIFY packagesChanged)
    Q_PROPERTY(QVariantList repos READ repos NOTIFY reposChanged)
    Q_PROPERTY(QVariantList services READ services NOTIFY servicesChanged)
    Q_PROPERTY(QVariantList patterns READ patterns NOTIFY patternsChanged)
    Q_PROPERTY(QVariantMap currentPackageDetails READ currentPackageDetails NOTIFY currentPackageDetailsChanged)
    Q_PROPERTY(bool hasPendingChanges READ hasPendingChanges NOTIFY packagesChanged)
    Q_PROPERTY(QVariantMap commitResult READ commitResult NOTIFY commitResultChanged)

public:
    explicit PackageController(QObject *parent = nullptr);
    ~PackageController() override;

    bool isConnected() const { return m_connected; }           // バックエンド接続状態
    bool isBusy() const { return m_busy; }                     // 処理中フラグ
    QString statusMessage() const { return m_statusMessage; }  // ステータスメッセージ
    QVariantList packages() const { return m_packages; }       // パッケージ一覧
    QVariantList repos() const { return m_repos; }             // リポジトリ一覧
    QVariantList services() const { return m_services; }       // サービス一覧
    QVariantList patterns() const { return m_patterns; }       // パターン一覧
    QVariantMap currentPackageDetails() const {                // 選択中の詳細
        return m_currentDetails;
    }
    QVariantMap commitResult() const { return m_commitResult; }  // コミット結果
    bool hasPendingChanges();                                  // 変更予定の有無

    // QMLから呼び出し可能なメソッド
    Q_INVOKABLE bool initialize();                                            // パッケージマネージャ初期化
    Q_INVOKABLE void searchPackages(const QString &query, int flags = 0x07);  // パッケージ検索
    Q_INVOKABLE void loadRepos();                                             // リポジトリ一覧読込
    Q_INVOKABLE void loadPackagesByRepo(const QString &repoAlias);            // リポ別パッケージ読込
    Q_INVOKABLE void loadPackageDetails(const QString &name);                 // パッケージ詳細読込
    Q_INVOKABLE void loadPatterns();                                          // パターン一覧読込
    Q_INVOKABLE void loadPackagesByPattern(const QString &patternName);       // パターン別パッケージ読込
    Q_INVOKABLE void loadPatches(int category = -1);                          // パッチ一覧読込

    Q_INVOKABLE bool setPackageStatus(const QString &name, int status);       // パッケージ状態変更
    Q_INVOKABLE bool setPackageVersion(const QString &name,                  // パッケージバージョン変更
                                       const QString &version,
                                       const QString &arch,
                                       const QString &repoAlias);
    Q_INVOKABLE bool togglePatternStatus(const QString &patternName);         // パターン状態トグル
    Q_INVOKABLE QVariantMap updateAllPackages();                               // 全パッケージ更新
    Q_INVOKABLE QVariantMap resolveDependencies();                            // 依存関係解決
    Q_INVOKABLE bool applySolution(int problemIndex, int solutionIndex);      // 解決策適用
    Q_INVOKABLE QVariantMap commit();                                         // 変更をコミット (同期)
    Q_INVOKABLE void commitAsync();                                          // 変更をコミット (非同期)
    Q_INVOKABLE void cancelOperation();                                      // 操作キャンセル

    Q_INVOKABLE bool addRepo(const QString &url, const QString &name);        // リポ追加
    Q_INVOKABLE bool addRepoFull(const QVariantMap &properties);              // リポ追加 (全属性)
    Q_INVOKABLE bool removeRepo(const QString &alias);                        // リポ削除
    Q_INVOKABLE bool setRepoEnabled(const QString &alias, bool enabled);      // リポ有効/無効
    Q_INVOKABLE bool modifyRepo(const QString &alias,                         // リポ変更
                                const QVariantMap &properties);
    Q_INVOKABLE bool refreshRepos();                                          // 全リポ同期リフレッシュ
    Q_INVOKABLE void refreshReposAsync();                                     // 全リポ非同期リフレッシュ
    Q_INVOKABLE void cancelRefresh();                                         // リフレッシュキャンセル
    Q_INVOKABLE bool refreshSingleRepo(const QString &alias);                 // 個別リポリフレッシュ
    Q_INVOKABLE QString probeRepoType(const QString &url);                    // リポタイプ検出 (未実装)

    // サービス管理
    Q_INVOKABLE void loadServices();                                        // サービス一覧読込
    Q_INVOKABLE bool addService(const QString &url, const QString &alias);  // サービス追加
    Q_INVOKABLE bool removeService(const QString &alias);                   // サービス削除
    Q_INVOKABLE bool modifyService(const QString &alias,                    // サービス変更
                                   const QVariantMap &properties);
    Q_INVOKABLE bool refreshService(const QString &alias);                  // サービスリフレッシュ
    Q_INVOKABLE QVariantList getPendingChanges();                           // 変更予定一覧取得
    Q_INVOKABLE QVariantList getDiskUsage();                                // ディスク使用量取得
    Q_INVOKABLE bool checkAuth(const QString &actionId);                    // Polkit認証
    Q_INVOKABLE void applyDarkMode(bool dark);                               // ダークモード適用
    Q_INVOKABLE void saveState();                                           // 選択状態を保存
    Q_INVOKABLE void restoreState();                                        // 選択状態を復元

signals:
    void connectedChanged();                                // 接続状態変更
    void busyChanged();                                     // ビジー状態変更
    void statusMessageChanged();                            // ステータスメッセージ変更
    void packagesChanged();                                 // パッケージ一覧変更
    void reposChanged();                                    // リポジトリ一覧変更
    void servicesChanged();                                 // サービス一覧変更
    void patternsChanged();                                 // パターン一覧変更
    void currentPackageDetailsChanged();                    // パッケージ詳細変更

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
    void packageStateChanged(const QString &packageName,   // パッケージ状態遷移
                             const QString &event);
    void refreshFinished(bool success);                     // リフレッシュ完了
    void conflictsDetected(const QVariantList &problems);   // コンフリクト検出

private:
    enum class PackageSource { None, Search, Repo, Pattern, Patches };

    void setBusy(bool busy);                            // ビジー状態設定
    void setStatusMessage(const QString &msg);          // ステータスメッセージ設定
    void refreshPackageStatuses();                      // パッケージ一覧再取得

    DBusClient *m_dbus = nullptr;                       // D-Busクライアント
    bool m_connected = false;                           // バックエンド接続状態
    bool m_busy = false;                                // 処理中フラグ
    QString m_statusMessage;                            // ステータスメッセージ
    QVariantList m_packages;                            // パッケージ一覧キャッシュ
    QVariantList m_repos;                               // リポジトリ一覧キャッシュ
    QVariantList m_services;                            // サービス一覧キャッシュ
    QVariantList m_patterns;                            // パターン一覧キャッシュ
    QVariantMap m_currentDetails;                       // 選択中パッケージ詳細
    QVariantMap m_commitResult;                         // コミット結果
    PackageSource m_lastSource = PackageSource::None;   // 最後のパッケージソース
    QString m_lastSourceParam;                          // 最後のソースパラメータ
    int m_lastSearchFlags = 0x07;                       // 最後の検索フラグ
};

} // namespace qZypper

#endif // QZYPPER_GUI_CONTROLLERS_PACKAGECONTROLLER_H
