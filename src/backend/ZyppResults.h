#ifndef QZYPPER_BACKEND_ZYPPRESULTS_H
#define QZYPPER_BACKEND_ZYPPRESULTS_H

#include <QStringList>
#include <QVariantMap>
#include <string>
#include <vector>
#include "ResolverInfo.h"
#include "CommitOutcome.h"
#include "SnapshotInfo.h"

namespace qZypper {

/**
 * @brief ソルバーの結果
 *
 * ZyppManager::SolverResultとして参照される
 */
struct SolverResult {
    bool success;                                   // 解決成功フラグ
    std::vector<ConflictInfo> problems;             // 衝突問題のリスト
    QVariantMap summary;                            // リスクサマリ(TransactionSummary)
};

/**
 * @brief ローカルRPMインストールの解決結果
 *
 * ZyppManager::LocalRpmInstallResultとして参照される
 */
struct LocalRpmInstallResult {
    bool success;                                   // 解決成功フラグ
    std::vector<ConflictInfo> problems;             // 衝突問題のリスト
    QVariantMap summary;                            // リスクサマリ(TransactionSummary)
    std::vector<std::string> packages;              // 追加対象のパッケージ名
    std::vector<std::string> unsignedPackages;      // 未署名のパッケージ名(UIの警告用)
};

/**
 * @brief コミットの結果
 *
 * ZyppManager::CommitResultとして参照される
 */
struct CommitResult {
    CommitClassification classification;            // 未実行を含む実行結果
    bool success = false;                           // コミット成功フラグ
    int installed = 0;                              // インストール件数
    int updated = 0;                                // 更新件数
    int removed = 0;                                // 削除件数
    std::vector<std::string> failedPackages;        // 失敗したパッケージ名
    std::vector<std::string> ignoredPackages;       // ユーザが[Ignore]を選択したパッケージ名
    std::vector<std::string> installedPackages;     // インストール済みのパッケージ名
    std::vector<std::string> updatedPackages;       // 更新済みのパッケージ名
    std::vector<std::string> removedPackages;       // 削除済みのパッケージ名
    quint64 totalInstalledSize = 0;                 // インストールサイズの合計(bytes)
    quint64 totalDownloadSize = 0;                  // ダウンロードサイズの合計(bytes)
    double elapsedSeconds = 0.0;                    // 経過時間(秒)
    std::string errorMessage;                       // エラーメッセージ
    QStringList scriptOutput;                       // スクリプト出力(最新500行)
    QStringList rpmMessages;                        // rpmメッセージ(最新500行)
    bool scriptOutputTruncated = false;             // 出力が上限で切り捨てられたか
    bool rebootNeeded = false;                      // 再起動が必要か
    bool restartSuggested = false;                  // 再起動が推奨されるか(パッチ)
    QStringList servicesToRestart;                  // 再起動が必要なsystemdサービスユニット
    std::vector<SnapshotEntry> snapshots;           // コミットで作成されたスナップショット
};

} // namespace qZypper

#endif // QZYPPER_BACKEND_ZYPPRESULTS_H
