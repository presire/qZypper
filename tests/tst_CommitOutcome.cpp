#include <QtTest>
#include "CommitOutcome.h"

using namespace qZypper;

/**
 * @class CommitOutcomeTest
 * @brief コミット結果の分類ロジックを検証する
 *
 * 実行ステップの集合と取消し・例外の有無から総合結果と各件数を求めるclassifyCommitと、
 * 例外発生時の分類を行うclassifyCommitExceptionを対象にする
 */
class CommitOutcomeTest : public QObject {
    Q_OBJECT
private slots:
    /** @brief classifiesStepsの入力データを提供する */
    void classifiesSteps_data()
    {
        QTest::addColumn<QString>("stages");
        QTest::addColumn<bool>("cancelled");
        QTest::addColumn<bool>("exception");
        QTest::addColumn<int>("outcome");
        QTest::addColumn<int>("done");
        QTest::addColumn<int>("errors");
        QTest::addColumn<int>("skipped");
        QTest::newRow("all-done") << "DDD" << false << false << int(CommitOutcome::Completed) << 3 << 0 << 0;
        QTest::newRow("empty") << "" << false << false << int(CommitOutcome::Completed) << 0 << 0 << 0;
        QTest::newRow("partial-error") << "DE" << false << false << int(CommitOutcome::Partial) << 1 << 1 << 0;
        QTest::newRow("partial-skipped") << "DT" << false << false << int(CommitOutcome::Partial) << 1 << 0 << 1;
        QTest::newRow("all-skipped") << "TT" << false << false << int(CommitOutcome::Failed) << 0 << 0 << 2;
        QTest::newRow("all-errors") << "EE" << false << false << int(CommitOutcome::Failed) << 0 << 2 << 0;
        QTest::newRow("cancel-midway") << "DET" << true << false << int(CommitOutcome::Cancelled) << 1 << 1 << 1;
        QTest::newRow("cancel-before-apply") << "T" << true << false << int(CommitOutcome::Cancelled) << 0 << 0 << 1;
        QTest::newRow("late-cancel") << "D" << true << false << int(CommitOutcome::Completed) << 1 << 0 << 0;
        QTest::newRow("empty-late-cancel") << "" << true << false << int(CommitOutcome::Completed) << 0 << 0 << 0;
        QTest::newRow("exception-after-apply") << "DT" << false << true << int(CommitOutcome::Failed) << 1 << 0 << 1;
        QTest::newRow("exception-over-cancel") << "T" << true << true << int(CommitOutcome::Failed) << 0 << 0 << 1;
        QTest::newRow("empty-exception") << "" << false << true << int(CommitOutcome::Failed) << 0 << 0 << 0;
    }
    /** @brief ステップの実行結果と取消し・例外から総合結果と各件数を分類することを検証する */
    void classifiesSteps()
    {
        // 準備 : ステップ列と取消し・例外のフラグを入力する
        QFETCH(QString, stages); QFETCH(bool, cancelled); QFETCH(bool, exception);
        QFETCH(int, outcome); QFETCH(int, done); QFETCH(int, errors); QFETCH(int, skipped);
        std::vector<CommitStep> steps;
        std::vector<std::string> failedNames, skippedNames;
        for (qsizetype i = 0; i < stages.size(); ++i) {
            const auto name = std::string("pkg-") + std::to_string(i);
            switch (stages[i].toLatin1()) {
            case 'D': steps.push_back({name, CommitStepStage::Done}); break;
            case 'E': steps.push_back({name, CommitStepStage::Error}); failedNames.push_back(name); break;
            case 'T': steps.push_back({name, CommitStepStage::Todo}); skippedNames.push_back(name); break;
            default: QFAIL("Invalid fixture");
            }
        }
        // 実行 : 総合結果を分類する
        const auto result = classifyCommit(steps, cancelled, exception);
        // 確認 : 総合結果と実行・エラー・スキップの件数を確認する
        QCOMPARE(int(result.outcome), outcome);
        QCOMPARE(result.doneCount, done); QCOMPARE(result.errorCount, errors); QCOMPARE(result.skippedCount, skipped);
        QVERIFY(result.failedPackages == failedNames); QVERIFY(result.skippedPackages == skippedNames);
    }
    /** @brief classifiesExceptionの入力データを提供する */
    void classifiesException_data()
    {
        QTest::addColumn<bool>("cancelled");
        QTest::addColumn<bool>("userRequest");
        QTest::addColumn<int>("outcome");
        QTest::newRow("cancel-abort") << true << true << int(CommitOutcome::Cancelled);
        QTest::newRow("unrelated-after-cancel") << true << false << int(CommitOutcome::Failed);
        QTest::newRow("abort-without-cancel") << false << true << int(CommitOutcome::Failed);
        QTest::newRow("plain-failure") << false << false << int(CommitOutcome::Failed);
    }
    /** @brief 取消しと利用者要求の組合せから、例外時の分類結果を決めることを検証する */
    void classifiesException()
    {
        // 準備 : 取消しと利用者要求の組合せを入力する
        QFETCH(bool, cancelled); QFETCH(bool, userRequest); QFETCH(int, outcome);
        // 確認 : 例外時の分類結果を確認する
        QCOMPARE(int(classifyCommitException(cancelled, userRequest)), outcome);
    }
};

QTEST_GUILESS_MAIN(CommitOutcomeTest)
#include "tst_CommitOutcome.moc"
