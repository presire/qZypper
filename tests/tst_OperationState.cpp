#include "OperationState.h"
#include <QtTest>

using namespace qZypper;
using namespace std::chrono_literals;

class OperationStateTest : public QObject {
    Q_OBJECT
private slots:
    void authorizesWhenIdle()
    {
        // Given
        OperationState state;
        // When
        QVERIFY(state.authorize());
        // Then
        QCOMPARE(state.snapshot().value("state").toString(), "authorizing");
        QVERIFY(state.snapshot().value("busy").toBool());
    }
    void preservesCancelWhenAuthorizationCompletes()
    {
        // Given
        OperationState state; state.authorize(); state.requestCancel();
        // When
        QVERIFY(state.run(OperationStage::Refreshing));
        // Then
        QVERIFY(state.cancelFlag().load());
    }
    void rejectsConcurrentJobWhenRunning()
    {
        // Given
        OperationState state; state.run(OperationStage::Resolving);
        // When
        const bool accepted = state.run(OperationStage::Reading);
        // Then
        QVERIFY(!accepted);
        QCOMPARE(state.snapshot().value("stage").toString(), "resolving");
    }
    void remainsBusyWhenWorkerStillRunning()
    {
        // Given
        OperationState state; state.run(OperationStage::Downloading);
        // When
        const bool completed = state.complete();
        // Then
        QVERIFY(!completed); QVERIFY(state.snapshot().value("busy").toBool());
    }
    void completesWhenFinishing()
    {
        // Given
        OperationState state; state.run(OperationStage::Reading); state.finishing();
        // When
        QVERIFY(state.complete());
        // Then
        QCOMPARE(state.snapshot().value("state").toString(), "idle");
        QVERIFY(!state.snapshot().value("busy").toBool());
    }
    void refusesCancelWhenInstalling()
    {
        // Given
        OperationState state; state.run(OperationStage::Downloading); state.stage(OperationStage::Installing);
        // When
        const bool accepted = state.requestCancel();
        // Then
        QVERIFY(!accepted); QVERIFY(!state.cancelFlag().load());
    }
    void keepsRpmProtectedWhenLateDownloadCallbackArrives()
    {
        // Given
        OperationState state; state.run(OperationStage::Installing);
        // When
        state.stage(OperationStage::Downloading);
        // Then
        QCOMPARE(state.snapshot().value("stage").toString(), "installing");
        QVERIFY(!state.snapshot().value("cancellable").toBool());
    }
    void cancelsWhenWatchdogExpiresInDownload()
    {
        // Given
        OperationState state; state.run(OperationStage::Downloading);
        // When
        const auto action = state.watchdog(0ms);
        // Then
        QCOMPARE(action, WatchdogAction::Cancel); QVERIFY(state.cancelFlag().load());
        QVERIFY(state.snapshot().value("busy").toBool());
    }
    void warnsWithoutCancelWhenWatchdogExpiresInRpm()
    {
        // Given
        OperationState state; state.run(OperationStage::Installing);
        // When
        const auto action = state.watchdog(0ms);
        // Then
        QCOMPARE(action, WatchdogAction::Warn); QVERIFY(!state.cancelFlag().load());
    }
    void staysRunningWhenHeartbeatIsFresh()
    {
        // Given
        OperationState state; state.run(OperationStage::Refreshing); state.heartbeat();
        // When
        const auto action = state.watchdog(10min);
        // Then
        QCOMPARE(action, WatchdogAction::None);
    }
    void clearsPreviousCancelWhenNewOperationStarts()
    {
        // Given
        OperationState state; state.run(OperationStage::Reading); state.requestCancel(); state.finishing(); state.complete();
        // When
        QVERIFY(state.run(OperationStage::Initializing));
        // Then
        QVERIFY(!state.cancelFlag().load());
    }
};

QTEST_GUILESS_MAIN(OperationStateTest)
#include "tst_OperationState.moc"
