#ifndef QZYPPER_BACKEND_COMMITOUTCOME_H
#define QZYPPER_BACKEND_COMMITOUTCOME_H

#include <string>
#include <vector>

namespace qZypper {

/** @brief libzyppに依存しないアクションの実行段階。 */
enum class CommitStepStage { Done, Error, Todo };
/** @brief トランザクション全体の実行結果。 */
enum class CommitOutcome { Completed, Partial, Cancelled, Failed };
/** @brief 実行結果を判定するためのパッケージ情報。 */
struct CommitStep {
    std::string name;
    CommitStepStage stage;
};
/** @brief 実行段階の集計。空の正常トランザクションは完了とする。 */
struct CommitClassification {
    CommitOutcome outcome = CommitOutcome::Failed;
    int doneCount = 0;
    int errorCount = 0;
    int skippedCount = 0;
    std::vector<std::string> failedPackages;
    std::vector<std::string> skippedPackages;
};

CommitClassification classifyCommit(const std::vector<CommitStep> &steps,
                                    bool cancelRequested, bool exception);
const char *commitOutcomeName(CommitOutcome outcome);

} // namespace qZypper
#endif // QZYPPER_BACKEND_COMMITOUTCOME_H
