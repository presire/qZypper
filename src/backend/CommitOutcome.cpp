#include "CommitOutcome.h"

namespace qZypper {

/** @brief 未実行と失敗を区別してコミットを分類する。 */
CommitClassification classifyCommit(const std::vector<CommitStep> &steps,
                                    bool cancelRequested, bool exception)
{
    CommitClassification result;
    for (const auto &step : steps) {
        switch (step.stage) {
        case CommitStepStage::Done:
            ++result.doneCount;
            break;
        case CommitStepStage::Error:
            ++result.errorCount;
            result.failedPackages.push_back(step.name);
            break;
        case CommitStepStage::Todo:
            ++result.skippedCount;
            result.skippedPackages.push_back(step.name);
            break;
        }
    }
    // 例外は取消しより優先する。取消しだけで全完了を失敗に変えない。
    if (exception)
        result.outcome = CommitOutcome::Failed;
    else if (result.errorCount == 0 && result.skippedCount == 0)
        result.outcome = CommitOutcome::Completed;
    else if (cancelRequested && result.skippedCount > 0)
        result.outcome = CommitOutcome::Cancelled;
    else if (result.doneCount > 0)
        result.outcome = CommitOutcome::Partial;
    return result;
}

/** @brief D-Busで使用する安定した結果識別子を返す。 */
const char *commitOutcomeName(CommitOutcome outcome)
{
    switch (outcome) {
    case CommitOutcome::Completed: return "completed";
    case CommitOutcome::Partial: return "partial";
    case CommitOutcome::Cancelled: return "cancelled";
    case CommitOutcome::Failed: return "failed";
    }
    return "failed";
}

} // namespace qZypper
