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
#include "ServiceInfo.h"
#include "PatternPatchInfo.h"
#include "ResolverInfo.h"
#include "ZyppLocks.h"
#include "ZyppHistory.h"
#include "ZyppCallbackReceiver.h"
#include "ZyppKeyReceiver.h"
#include "ZyppResults.h"
#include "CommitOutcome.h"
#include "OperationState.h"
#include "CommitInteraction.h"
#include "SnapshotInfo.h"
#include "LocalRpm.h"

namespace qZypper {

struct SearchOptions;   // 拡張検索オプション (SearchOptions.h)

/**
 * @class ZyppManager
 * @brief libzyppのラッパークラス (シングルトン)
 *
 * libzyppの全操作をスレッドセーフに提供し、PackageManagerAdaptorから呼び出される
 *
 * 実装は責務ごとにZyppRepos.cpp、ZyppServices.cpp、ZyppQueries.cpp、ZyppSelection.cpp、ZyppResolver.cpp、ZyppCommit.cpp等のZypp*.cppへ分割している
 *
 * 入力値の検証とリポジトリ/サービスのポリシー検査はRepoPolicyが担う
 *
 * m_mutex : libzyppへのアクセスを直列化する非再帰mutex
 *           鍵承認系のメソッドはワーカースレッドのコールバックと競合しないよう取得しない
 */
class ZyppManager {
public:
    using SolverResult = qZypper::SolverResult;                    // ソルバーの結果 (ZyppResults.h)
    using LocalRpmInstallResult = qZypper::LocalRpmInstallResult;  // ローカルRPMインストールの解決結果 (ZyppResults.h)
    using CommitResult = qZypper::CommitResult;                    // コミットの結果 (ZyppResults.h)

    static ZyppManager& instance();                                          // シングルトンの取得

    // 初期化・終了
    bool initialize(const std::string& root = "/", bool cacheOnly = false);  // libzyppの初期化
    void shutdown();                                                         // リソースの解放
    /** @brief 初期化済みかを返す */
    bool isInitialized() const { return m_initialized; }
    bool reloadIfChanged();                                                  // 外部でRPMデータベースが変更された場合に再読込
    OperationState &operationState() { return m_operation; }                 // libzypp非依存の共有制御

    // リポジトリ管理
    std::vector<RepoInfo> getRepos() const;                                                                               // リポジトリ一覧の取得
    bool addRepo(const std::string& url, const std::string& name);                                                        // リポジトリの追加 (簡易)
    bool addRepo(const RepoInfo& info);                                                                                   // リポジトリの追加 (全属性)
    bool removeRepo(const std::string& alias);                                                                            // リポジトリの削除
    bool setRepoEnabled(const std::string& alias, bool enabled);                                                          // リポジトリの有効/無効の切り替え
    bool modifyRepo(const std::string& alias, const RepoChanges& changes);                                                // リポジトリの変更 (渡された項目のみ)
    bool refreshRepos(std::function<void(const std::string&, int)> progressCallback = nullptr);                           // 全リポジトリのリフレッシュ
    bool refreshRepo(const std::string& alias, std::function<void(const std::string&, int)> progressCallback = nullptr);  // 個別リポジトリのリフレッシュ

    // サービス管理
    std::vector<ServiceInfo> getServices() const;                                 // サービス一覧の取得
    bool addService(const std::string& url, const std::string& alias);            // サービスの追加
    bool removeService(const std::string& alias);                                 // サービスの削除
    bool modifyService(const std::string& alias, const ServiceChanges& changes);  // サービスの変更 (渡された項目のみ)
    bool refreshService(const std::string& alias);                                // サービスのリフレッシュ

    // パッケージ検索・一覧
    std::vector<PackageInfo> searchPackages(const std::string& query, int flags);    // パッケージの検索
    std::vector<PackageInfo> searchPackagesEx(const SearchOptions& options);         // 拡張検索 (PoolQuery)
    PackageDetails getPackageDetails(const std::string& name) const;                 // パッケージ詳細の取得
    std::vector<PackageInfo> getPackagesByRepo(const std::string& repoAlias) const;  // リポジトリ別のパッケージ
    std::vector<PackageInfo> getPendingChanges() const;                              // 変更予定一覧の取得 (全プール横断)

    // パッケージ分類 (recommended/suggested/orphaned/unneeded/multiversion/retracted/retractedInstalled/ptf/locked)
    std::vector<PackageInfo> getPackagesByClassification(const std::string& cls) const;

    // パターン・パッチ
    std::vector<PatternInfo> getPatterns() const;                                         // パターン一覧の取得
    std::vector<PackageInfo> getPackagesByPattern(const std::string& patternName) const;  // パターン別のパッケージ
    std::vector<PatchInfo> getPatches(int category = -1) const;                           // パッチ一覧の取得
    bool setPatchStatus(const std::string& id, int status);                               // パッチの選択状態の変更

    // 永続ロック (zypper al/rl互換、/etc/zypp/locks)
    std::vector<LockInfo> getLocks() const;                      // ロック一覧の取得
    bool addLock(const LockInfo& info);                          // ロックの追加 (永続化)
    bool removeLock(const LockInfo& info);                       // ロックの削除 (永続化)

    /**
     * @brief zyppヒストリ (/var/log/zypp/history) を新しい順に取得する
     * @param options 取得範囲と絞り込み条件
     * @param entries 取得したエントリの格納先
     * @param error 失敗時の理由
     * @return ファイルが無い場合は空で成功、上限超過・取消し・読込失敗はfalse
     */
    bool getHistory(const HistoryOptions& options, std::vector<HistoryEntry>& entries,
                    std::string& error);

    std::string createSolverTestcase();                          // ソルバーテストケースを/var/log配下に生成し、作成したディレクトリパスを返す

    // 選択状態の管理
    bool setPackageStatus(const std::string& name, int status);  // パッケージ状態の変更
    bool setPackageVersion(const std::string& name,              // パッケージバージョンの変更
                           const std::string& version,
                           const std::string& arch,
                           const std::string& repoAlias);
    bool setPatternStatus(const std::string& name, int status);  // パターン状態の変更
    void saveState();                                            // 選択状態の保存
    void restoreState();                                         // 選択状態の復元
    static bool isValidPackageStatus(int status);                // PackageStatusの範囲内か

    // 依存関係の解決
    SolverResult resolveDependencies();                          // ソルバーの実行
    SolverResult updateAllPackages();                            // 全パッケージの更新 (doUpdate +依存関係の解決)
    SolverResult verifySystem();                                 // システム整合性の検証 (zypper verify相当)
    bool applySolution(int problemIndex, int solutionIndex);     // 解決策の適用
    SolverResult distUpgrade(bool allowVendorChange);            // ディストリビューションの更新 (doUpgrade、zypper dup)
    SolverResult selectAllNeededPatches(bool securityOnly, bool includeOptional,
                                        bool skipInteractive);   // 必要なパッチの一括選択 (zypper patch)
    QVariantMap getTransactionSummary();                         // 現在のプール状態のリスクサマリの取得

    // コミット
    CommitResult commit(ProgressCallbackFn progressCallback = nullptr,   // コミットの実行
                        StateEventCallbackFn stateCallback = nullptr,
                        PromptAskFn askCallback = nullptr,
                        ScriptOutputFn scriptOutputCallback = nullptr);

    std::vector<DiskUsageInfo> getDiskUsage() const;             // ディスク使用量の取得

    // キャンセル
    /**
     * @brief 操作のキャンセルを要求する
     * @note 要求フラグは操作の開始時 (アダプタのstartWorker) にだけクリアする
     *       各操作の内部でクリアすると、事前検査中に届いた要求が失われるため
     */
    void cancelOperation() { m_cancelRequested = true; }
    /** @brief 操作開始前にキャンセル要求と取消し結果を消去する */
    void clearCancelRequest() { m_cancelRequested = false; m_lastOpCancelled = false; }
    /** @brief キャンセルが要求されているかを返す */
    bool isCancelRequested() const { return m_cancelRequested.load(); }
    /** @brief 直前の操作が取消し要求で中断されたかを返す */
    bool lastOperationCancelled() const { return m_lastOpCancelled.load(); }
    /** @brief 操作の開始を記録する */
    void beginOperation() { m_lastOpCancelled = false; }

    /**
     * @brief 最新のエラーメッセージを取得する
     * @return エラーメッセージ
     */
    std::string lastError() const {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_lastError;
    }

    // GPG鍵の信頼
    /**
     * @brief GPG鍵フィンガープリントを承認する (ワンショット)
     * @param fingerprint フィンガープリント (正規化前の生文字列も可)
     * @return 正規化成功時はtrue、不正な場合はfalseを返しm_lastErrorを設定する
     * @note m_mutexは取得しない (receiverが独自mutexを持つ)
     */
    bool approveKeyFingerprint(const std::string &fingerprint);

    /**
     * @brief 未信頼鍵の通知コールバックを設定する
     * @param cb 通知コールバック
     * @note m_mutexは取得しない。initialize()前の呼び出しも可能
     */
    void setUntrustedKeyCallback(UntrustedKeyCallbackFn cb);

    /**
     * @brief 未知鍵のin-flight信頼プロンプトのコールバックを設定する
     * @param cb 回答コールバック (ワーカースレッドで実行)
     */
    void setKeyTrustPromptCallback(KeyTrustPromptFn cb);

    /**
     * @brief 検証済みのローカルRPMを一時plaindirリポジトリ経由で選択・解決する
     *
     * 各ファイルはLocalRpm::validatePathで検証済みであることが前提であり、root所有0700の一時ディレクトリへTOCTOU-safeにコピーする
     *
     * plaindirリポジトリは/etc/zypp/repos.dへは永続化せず、プールへロードしてS_Installを選択後にソルバーを実行する
     *
     * 未署名のRPMは許可するがunsignedPackagesに列挙する
     *
     * @param files インストール対象のローカルRPM
     * @return 解決結果、失敗時はsuccess=falseかつm_lastErrorを設定する
     */
    LocalRpmInstallResult installLocalRpms(std::vector<LocalRpmFile> files);

    void cleanupLocalRpms();                                     // 一時ローカルRPMリポジトリとコピー済みファイルの破棄

private:
    ZyppManager() = default;                                     // コンストラクタ (private)
    ~ZyppManager() = default;                                    // デストラクタ (private)
    ZyppManager(const ZyppManager&) = delete;                    // コピー禁止
    ZyppManager& operator=(const ZyppManager&) = delete;         // 代入禁止

    // セッション
    void recordDatabaseStamp();                                  // RPMデータベースの変更検知用スタンプの記録

    // パッケージ情報・ステータス変換
    PackageInfo makePackageInfo(const zypp::ui::Selectable::Ptr& sel) const;                // PackageInfoの生成
    static int fromZyppStatus(zypp::ui::Status status);                                     // zypp --> qZypperのステータス変換
    static bool toZyppStatus(int status, zypp::ui::Status &out);                            // qZypper --> zyppのステータス変換 (範囲外はfalse)
    static std::string makePatchId(const std::string& name, const std::string& version,
                                   const std::string& arch, const std::string& repoAlias);  // パッチの安定識別子の生成

    // リスクサマリ
    QVariantMap computeTransactionSummary() const;               // リスクサマリの生成 (内部でロック)
    QVariantMap computeTransactionSummaryLocked() const;         // リスクサマリの生成 (m_mutex保持中に呼ぶ)

    // 取消し
    /** @brief 取消し要求による中断を記録する (m_mutex保持中に呼ぶ) */
    void markCancelled() { m_lastError = "Operation cancelled"; m_lastOpCancelled = true; }

    /**
     * @brief サービス由来のリポジトリを浄化する
     *
     * 指定サービスの全リポジトリを検査し、ポリシー違反や署名検査の弱体化があるものを削除する
     * 残留.repoファイルは共有ファイルの削除を避けるため、ガード付きでのみ削除する
     *
     * @param serviceAlias 対象サービスのエイリアス
     * @param report 削除内容の報告 (エイリアスと理由のみ、URLなし)
     * @param preexistingRepoFiles サービスの追加/更新前に存在した.repoファイル
     * @return 違反がなく全て健全な場合はtrue
     * @note 呼び出し側のm_mutex配下で呼ぶこと (内部でロックしない)
     */
    bool sanitizeServiceRepos(const std::string &serviceAlias, std::string &report,
                              const std::set<std::string> &preexistingRepoFiles);

    /**
     * @brief サービスの.serviceファイルの汚染を検証し、必要であれば隔離する
     *
     * メモリ上のサービスにRepoPolicy::checkServicePolicyを適用し、更にディスク上の.serviceファイルをServiceFileReaderで解析して一致を要求する
     * 不一致の場合はサービスを削除し、残留ファイルを除去する
     *
     * 残留ファイルの除去は、操作前にファイルがこのサービス専有であったことが前提となる
     * (addServiceはlibzyppが新規ファイルを生成し、refreshServiceは事前にserviceFileMatchesで共有ファイルを拒否している)
     *
     * @param serviceAlias 対象サービスのエイリアス
     * @return 健全な場合はtrue、隔離時はm_lastErrorを設定してfalseを返す
     *         (削除できずに残存した場合はその旨をm_lastErrorに設定する)
     * @note 呼び出し側のm_mutex配下で呼ぶこと (内部でロックしない)
     */
    bool quarantineServiceIfTainted(const std::string &serviceAlias);

    // ローカルRPM
    void cleanupLocalRpmsLocked();                                    // 一時リポジトリの破棄 (m_mutex保持中に呼ぶ)

    // libzyppの状態
    zypp::ZYpp::Ptr m_zypp;                                           // ZYppシングルトン
    std::unique_ptr<zypp::RepoManager> m_repoManager;                 // リポジトリマネージャ
    bool m_initialized = false;                                       // 初期化済みフラグ
    std::string m_rpmStamp;                                           // RPMデータベースの変更検知用
    zypp::ResolverProblemList m_problems;                             // ソルバーの問題リスト

    // ディストリビューション更新
    bool m_distUpgradeMode = false;                                   // DistUpgradeの選択を保持中 (再解決もdupで行う)
    bool m_dupAllowVendorChange = false;                              // DistUpgradeのベンダー変更許可

    // 排他制御・エラー・取消し
    mutable std::string m_lastError;                                  // 最新のエラーメッセージ
    mutable std::mutex m_mutex;                                       // スレッド排他ロック
    OperationState m_operation;                                       // 操作状態と取消し要求
    std::atomic<bool> &m_cancelRequested = m_operation.cancelFlag();  // キャンセル要求フラグ
    std::atomic<bool> m_lastOpCancelled{false};                       // 直前の操作が取消しで中断された

    // コールバック受信器・コミット時の対話
    KeyRingReceiver m_keyRingReceiver;                                // GPG鍵の信頼コールバック
    DigestReceiver m_digestReceiver;                                  // ダイジェスト検証コールバック
    std::set<std::string> m_acceptedLicenses;                         // 承認済みライセンス (pkg|version)

    // ローカルRPM
    std::string m_localRpmAlias;                                      // 一時ローカルRPMリポジトリのエイリアス
    std::string m_localRpmDir;                                        // 一時コピー先ディレクトリ
};

} // namespace qZypper

#endif // QZYPPER_BACKEND_ZYPPMANAGER_H
