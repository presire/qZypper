#ifndef QZYPPER_BACKEND_ZYPPMANAGER_H
#define QZYPPER_BACKEND_ZYPPMANAGER_H

#include <string>
#include <vector>
#include <set>
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

    /**
     * @brief GPG鍵フィンガープリントを承認する (ワンショット)。
     * @param fingerprint フィンガープリント (正規化前の生文字列も可)
     * @return 正規化成功時 true。不正な場合は false を返し m_lastError を設定する。
     * @note m_mutex は取得しない (receiver が独自 mutex を持つ)。
     */
    bool approveKeyFingerprint(const std::string &fingerprint);

    /**
     * @brief 未信頼鍵通知コールバックを設定する。
     * @param cb 通知コールバック
     * @note m_mutex は取得しない。initialize() 前の呼び出しも可能。
     */
    void setUntrustedKeyCallback(UntrustedKeyCallbackFn cb);

private:
    ZyppManager() = default;                                                  // コンストラクタ (private)
    ~ZyppManager() = default;                                                 // デストラクタ (private)
    ZyppManager(const ZyppManager&) = delete;                                 // コピー禁止
    ZyppManager& operator=(const ZyppManager&) = delete;                      // 代入禁止
    PackageInfo makePackageInfo(const zypp::ui::Selectable::Ptr& sel) const;  // PackageInfo生成
    static int fromZyppStatus(zypp::ui::Status status);                       // zypp --> qZypperステータス変換
    static zypp::ui::Status toZyppStatus(int status);                         // qZypper --> zyppステータス変換
    /**
     * @brief リポジトリ/サービスURLを検証する。
     * @param url 検証対象URL
     * @param err 失敗時のエラー詳細 (呼び出し側が m_lastError 用に利用)
     * @param allowIsoNesting iso スキームの url クエリパラメータ再帰検証を許可するか
     * @return 有効時 true
     */
    static bool validateUrl(const std::string &url, std::string &err,
                            bool allowIsoNesting = true);
    /**
     * @brief リポジトリ/サービスエイリアスを検証する。
     * @param alias 検証対象エイリアス
     * @param err 失敗時のエラー詳細
     * @return 有効時 true
     */
    static bool validateAlias(const std::string &alias, std::string &err);
    /**
     * @brief リポジトリポリシーを検査する。
     *
     * サービスやミラーリスト由来のリポジトリデータが URL/INI/GPG 検証を
     * 迂回するのを防ぐ。制御文字・不正 URL・不審なミラーリスト/GPG 鍵 URL を拒否する。
     * エラー文にはエイリアス・理由・スキームのみを含め、完全な URL は含めない。
     * @param repo 検査対象リポジトリ
     * @param err 失敗時のエラー詳細
     * @return ポリシー準拠時 true
     */
    static bool checkRepoPolicy(const zypp::RepoInfo &repo, std::string &err);
    /**
     * @brief リポジトリの生 GPG 設定が弱体化されているか検査する。
     *
     * getRawGpgChecks の三値 (g, r, p) のいずれかが確定的に false の場合
     * (例: gpgcheck=1 repo_gpgcheck=0 pkg_gpgcheck=0) true を返す。
     * 実効ブール値では AllowUnsigned を検出できないため生値を見る。
     * @param repo 検査対象リポジトリ
     * @return 弱体化あり時 true
     */
    static bool rawGpgChecksWeakened(const zypp::RepoInfo &repo);
    /**
     * @brief ミラーリスト由来の全エンドポイント URL を検証する。
     *
     * repoOrigins() (MirroredOriginSet) を走査し validateUrl で検査する。
     * ネットワーク I/O を伴う場合があるためオフライン必須箇所では呼ばない。
     * @param repo 検査対象リポジトリ
     * @param err 失敗時のエラー詳細 (エイリアスとスキームのみ)
     * @return 全端点が有効時 true
     */
    static bool checkRepoOrigins(const zypp::RepoInfo &repo, std::string &err);
    /**
     * @brief サービス情報を検査する。
     *
     * リモート repoindex 由来のエイリアス/名前に含まれる制御文字、
     * 不正 URL、PLUGIN 型、repoStates キーの INI メタ文字を拒否する。
     * @param svc 検査対象サービス
     * @param err 失敗時のエラー詳細
     * @return ポリシー準拠時 true
     */
    static bool checkServicePolicy(const zypp::ServiceInfo &svc, std::string &err);
    /**
     * @brief on-disk の .service ファイルがメモリ上のサービスと一致するか検査する。
     *
     * 単一セクション・セクション名=alias・全 url/type 値の一致を要求する
     * (改行注入による type=plugin / url= 行の差し込みを検出)。
     * @param path .service ファイルパス
     * @param svc メモリ上のサービス
     * @return 一致時 true
     */
    static bool serviceFileMatches(const zypp::Pathname &path, const zypp::ServiceInfo &svc);
    /**
     * @brief サービス由来のリポジトリを浄化する。
     *
     * 指定サービスの全リポジトリを検査し、ポリシー違反や署名検査弱体化が
     * あるものを削除する。残留 .repo ファイルは共有ファイル削除を避けるため
     * ガード付きでのみ削除する。
     * @param serviceAlias 対象サービスエイリアス
     * @param report 削除内容の報告 (エイリアスと理由のみ、URL なし)
     * @param preexistingRepoFiles サービス追加/更新前に存在した .repo ファイル
     * @return 違反がなく全て健全時 true
     * @note 呼び出し側の m_mutex 配下で呼ぶこと (内部でロックしない)。
     */
    bool sanitizeServiceRepos(const std::string &serviceAlias, std::string &report,
                              const std::set<std::string> &preexistingRepoFiles);
    /**
     * @brief 既知リポジトリパス直下の通常ファイルを列挙する。
     * @return フルパス文字列の集合
     */
    static std::set<std::string> snapshotRepoFiles();
    /**
     * @brief サービスの .service ファイル汚染を検証し必要なら隔離する。
     *
     * メモリ上のサービスに checkServicePolicy を適用し、さらに on-disk の
     * .service ファイルを ServiceFileReader で解析して一致を要求する。
     * 不一致時はサービスを削除し残留ファイルを除去する。
     * 残留ファイル除去は操作前にファイルがこのサービス専有であったことが前提
     * (addService は libzypp が新規ファイルを生成、refreshService は事前に
     * serviceFileMatches で共有ファイルを拒否している)。
     * @param serviceAlias 対象サービスエイリアス
     * @return 健全時 true。隔離時は m_lastError を設定し false を返す
     *         (削除できず残存した場合はその旨を m_lastError に設定する)。
     * @note 呼び出し側の m_mutex 配下で呼ぶこと (内部でロックしない)。
     */
    bool quarantineServiceIfTainted(const std::string &serviceAlias);
    /**
     * @brief リポジトリ/サービス表示名を検証する。
     * @param name 検証対象名 (API が空を許す箇所では空も有効)
     * @param err 失敗時のエラー詳細
     * @return 有効時 true
     */
    static bool validateName(const std::string &name, std::string &err);
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
