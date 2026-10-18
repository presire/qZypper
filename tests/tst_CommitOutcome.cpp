#include "CommitOutcome.h"
#include <QtTest>

using namespace qZypper;

class CommitOutcomeTest : public QObject {
    Q_OBJECT
private slots:
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
    void classifiesSteps()
    {
        // Given
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
        // When
        const auto result = classifyCommit(steps, cancelled, exception);
        // Then
        QCOMPARE(int(result.outcome), outcome);
        QCOMPARE(result.doneCount, done); QCOMPARE(result.errorCount, errors); QCOMPARE(result.skippedCount, skipped);
        QVERIFY(result.failedPackages == failedNames); QVERIFY(result.skippedPackages == skippedNames);
    }
};

QTEST_GUILESS_MAIN(CommitOutcomeTest)
#include "tst_CommitOutcome.moc"
