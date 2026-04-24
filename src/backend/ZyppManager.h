#ifndef QZYPPER_BACKEND_ZYPPMANAGER_H
#define QZYPPER_BACKEND_ZYPPMANAGER_H

#include <string>
#include <vector>
#include <functional>
#include <memory>
#include <mutex>
#include <atomic>
#include <zypp/ZYpp.h>
#include <zypp/ZYppFactory.h>
#include <zypp/RepoManager.h>
#include <zypp/ResPool.h>
#include <zypp/ResPoolProxy.h>
#include <zypp/ui/Selectable.h>
#include <zypp/Package.h>
#include <zypp/Pattern.h>
#include <zypp/Patch.h>
#include <zypp/Resolver.h>
#include <zypp/ProblemTypes.h>
#include <zypp/ServiceInfo.h>
#include "PackageInfo.h"
#include "RepoInfo.h"
#include "ZyppCallbackReceiver.h"


namespace qZypper {

/**
 * @brief libzypp のラッパークラス (シングルトン)。
 *
 * libzypp の全操作をスレッドセーフに提供する。
 * PackageManagerAdaptor から呼び出される。
 */
class ZyppManager {
public:
    static ZyppManager& instance();                       // シングルトン取得

    // 初期化・終了
    bool initialize(const std::string& root = "/");       // libzypp初期化
    void shutdown();                                      // リソース解放
    bool isInitialized() const { return m_initialized; }  // 初期化済みか

    // リポジトリ管理
    std::vector<RepoInfo> getRepos() const;                              // リポジトリ一覧取得
    bool addRepo(const std::string& url, const std::string& name);       // リポ追加 (簡易)
    bool addRepo(const RepoInfo& info);                                  // リポ追加 (全属性)
    bool removeRepo(const std::string& alias);                           // リポ削除
    bool setRepoEnabled(const std::string& alias, bool enabled);         // リポ有効 / 無効
    bool modifyRepo(const std::string& alias, const RepoInfo& newInfo);  // リポ変更
    bool refreshRepos(std::function<void(const std::string&, int)> progressCallback = nullptr);                           // 全リポリフレッシュ
    bool refreshRepo(const std::string& alias, std::function<void(const std::string&, int)> progressCallback = nullptr);  // 個別リポリフレッシュ
    std::string probeRepoType(const std::string& url);                   // リポタイプ検出

    // サービス管理
    std::vector<ServiceInfo> getServices() const;                              // サービス一覧取得
    bool addService(const std::string& url, const std::string& alias);         // サービス追加
    bool removeService(const std::string& alias);                              // サービス削除
    bool modifyService(const std::string& alias, const ServiceInfo& newInfo);  // サービス変更
    bool refreshService(const std::string& alias);                             // サービスリフレッシュ
    std::string probeServiceType(const std::string& url);                      // サービスタイプ検出

    // パッケージ検索・一覧
    std::vector<PackageInfo> searchPackages(const std::string& query, int flags) const;  // パッケージ検索
    PackageDetails getPackageDetails(const std::string& name) const;                     // パッケージ詳細取得
    std::vector<PackageInfo> getPackagesByRepo(const std::string& repoAlias) const;      // リポ別パッケージ

    // パターン・パッチ
    std::vector<PatternInfo> getPatterns() const;                                         // パターン一覧取得
    std::vector<PackageInfo> getPackagesByPattern(const std::string& patternName) const;  // パターン別パッケージ
    std::vector<PatchInfo> getPatches(int category = -1) const;                           // パッチ一覧取得

    // 変更予定パッケージ一覧 (全プール横断)
    std::vector<PackageInfo> getPendingChanges() const;          // 変更予定一覧取得

    // 状態管理
    bool setPackageStatus(const std::string& name, int status);  // パッケージ状態変更
    bool setPackageVersion(const std::string& name,             // パッケージバージョン変更
                           const std::string& version,
                           const std::string& arch,
                           const std::string& repoAlias);
    bool setPatternStatus(const std::string& name, int status);  // パターン状態変更
    void saveState();                                            // 選択状態を保存
    void restoreState();                                         // 選択状態を復元

    // 依存関係解決
    /** @brief ソルバー結果 */
    struct SolverResult {
        bool success;                                            // 解決成功フラグ
        std::vector<ConflictInfo> problems;                      // 衝突問題リスト
    };

    SolverResult resolveDependencies();                          // ソルバー実行
    SolverResult updateAllPackages();                            // 全パッケージ更新 (doUpdate + 依存解決)
    bool applySolution(int problemIndex, int solutionIndex);     // 解決策適用

    // コミット
    /** @brief コミット結果 */
    struct CommitResult {
        bool success = false;                           // コミット成功フラグ
        int installed = 0;                              // インストール件数
        int updated = 0;                                // 更新件数
        int removed = 0;                                // 削除件数
        std::vector<std::string> failedPackages;        // 失敗パッケージ名
        std::vector<std::string> installedPackages;     // インストール済みパッケージ名
        std::vector<std::string> updatedPackages;       // 更新済みパッケージ名
        std::vector<std::string> removedPackages;       // 削除済みパッケージ名
        quint64 totalInstalledSize = 0;                 // インストールサイズ合計 (bytes)
        quint64 totalDownloadSize = 0;                  // ダウンロードサイズ合計 (bytes)
        double elapsedSeconds = 0.0;                    // 経過時間 (秒)
        std::string errorMessage;                       // エラーメッセージ
    };

    CommitResult commit(ProgressCallbackFn progressCallback = nullptr,   // コミット実行
                        StateEventCallbackFn stateCallback = nullptr);

    // ディスク使用量
    std::vector<DiskUsageInfo> getDiskUsage() const;       // ディスク使用量取得

    // キャンセル
    void cancelOperation() { m_cancelRequested = true; }   // 操作キャンセル要求
    bool isCancelRequested() const { return m_cancelRequested.load(); }  // キャンセル状態取得

    // エラー情報
    std::string lastError() const { return m_lastError; }  // 最新エラーメッセージ

private:
    ZyppManager() = default;                                                  // コンストラクタ (private)
    ~ZyppManager() = default;                                                 // デストラクタ (private)
    ZyppManager(const ZyppManager&) = delete;                                 // コピー禁止
    ZyppManager& operator=(const ZyppManager&) = delete;                      // 代入禁止
    PackageInfo makePackageInfo(const zypp::ui::Selectable::Ptr& sel) const;  // PackageInfo生成
    static int fromZyppStatus(zypp::ui::Status status);                       // zypp --> qZypperステータス変換
    static zypp::ui::Status toZyppStatus(int status);                         // qZypper --> zyppステータス変換
    zypp::ZYpp::Ptr m_zypp;                                                   // ZYppシングルトン
    std::unique_ptr<zypp::RepoManager> m_repoManager;                         // リポジトリマネージャ
    bool m_initialized = false;                                               // 初期化済みフラグ
    mutable std::string m_lastError;                                          // 最新エラーメッセージ
    mutable std::mutex m_mutex;                                               // スレッド排他ロック
    std::atomic<bool> m_cancelRequested{false};                               // キャンセル要求フラグ
    zypp::ResolverProblemList m_problems;                                     // ソルバー問題リスト
    KeyRingReceiver m_keyRingReceiver;                                        // GPG鍵信頼コールバック
    DigestReceiver m_digestReceiver;                                          // ダイジェスト検証コールバック
};

} // namespace qZypper

#endif // QZYPPER_BACKEND_ZYPPMANAGER_H
