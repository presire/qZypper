#ifndef QZYPPER_BACKEND_COMMITOUTCOME_H
#define QZYPPER_BACKEND_COMMITOUTCOME_H

#include <string>
#include <vector>

namespace qZypper {

/** @brief libzyppに依存しないアクションの実行段階 */
enum class CommitStepStage {
    Done,       // 正常に完了
    Error,      // 実行に失敗
    Todo        // 未実行のまま終了
};

/** @brief トランザクション全体の実行結果 */
enum class CommitOutcome {
    Completed,  // 全ステップが完了
    Partial,    // 一部だけ完了
    Cancelled,  // 取消しにより未完了
    Failed      // 例外または実行失敗
};

/** @brief 実行結果を判定するためのパッケージ情報 */
struct CommitStep {
    std::string name;       // 対象パッケージ名
    CommitStepStage stage;  // 最終実行段階
};

/** @brief 実行段階の集計、空の正常トランザクションは完了とする */
struct CommitClassification {
    CommitOutcome outcome = CommitOutcome::Failed;  // 全体の分類結果
    int doneCount = 0;                              // 完了ステップ数
    int errorCount = 0;                             // エラーステップ数
    int skippedCount = 0;                           // 未実行ステップ数
    std::vector<std::string> failedPackages;        // 失敗したパッケージ名
    std::vector<std::string> skippedPackages;       // 未実行のパッケージ名
};

/**
 * @brief 全ステップの実行段階からコミット結果を分類する
 * @param steps 各パッケージの最終実行段階
 * @param cancelRequested 取消しが要求されたか
 * @param exception 例外により処理を終了したか
 * @return 完了数と未実行項目を含む分類結果
 */
CommitClassification classifyCommit(const std::vector<CommitStep> &steps,
                                    bool cancelRequested, bool exception);

/**
 * @brief 例外終了を取消しまたは失敗に分類する
 * @param cancelRequested 取消しが要求されたか
 * @param userRequestException 例外が利用者要求に起因するか
 * @return 例外時のコミット結果
 */
CommitOutcome classifyCommitException(bool cancelRequested, bool userRequestException);

/**
 * @brief コミット結果のD-Bus用安定識別子を返す
 * @param outcome 変換するコミット結果
 * @return 英小文字ハイフン区切りの識別子
 */
const char *commitOutcomeName(CommitOutcome outcome);

} // namespace qZypper
#endif // QZYPPER_BACKEND_COMMITOUTCOME_H
