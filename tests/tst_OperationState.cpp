#include <QtTest>
#include <thread>
#include "OperationState.h"

using namespace qZypper;
using namespace std::chrono_literals;

/**
 * @class OperationStateTest
 * @brief 操作の状態遷移・排他・取消し・ウォッチドッグを検証する
 *
 * アイドルからの認証、認証完了後の取消し保持、同時ジョブの拒否、
 * 実行中のビジー維持、段階ごとの取消し可否、遅延コールバックの防御、
 * ウォッチドッグの期限切れ動作、新規操作開始時の取消し初期化を対象にする
 */
class OperationStateTest : public QObject {
    Q_OBJECT
private slots:
    /** @brief アイドル状態で認証を開始できることを検証する */
    void authorizesWhenIdle()
    {
        // 準備 : 新しい操作状態を用意する
        OperationState state;
        // 実行 : 認証を要求する
        QVERIFY(state.authorize());
        // 確認 : 認証中の状態になり、ビジーになることを確認する
        QCOMPARE(state.snapshot().value("state").toString(), "authorizing");
        QVERIFY(state.snapshot().value("busy").toBool());
    }
    /** @brief 認証完了後も取消し要求が保持されることを検証する */
    void preservesCancelWhenAuthorizationCompletes()
    {
        // 準備 : 認証中に取消しを要求する
        OperationState state; state.authorize(); state.requestCancel();
        // 実行 : リフレッシュ段階へ進める
        QVERIFY(state.run(OperationStage::Refreshing));
        // 確認 : 取消しフラグが立ったままであることを確認する
        QVERIFY(state.cancelFlag().load());
    }
    /** @brief 実行中に別のジョブを拒否することを検証する */
    void rejectsConcurrentJobWhenRunning()
    {
        // 準備 : 既に解決段階を実行中にする
        OperationState state; state.run(OperationStage::Resolving);
        // 実行 : 読み込み段階の実行を要求する
        const bool accepted = state.run(OperationStage::Reading);
        // 確認 : 拒否され段階が解決段階のままであることを確認する
        QVERIFY(!accepted);
        QCOMPARE(state.snapshot().value("stage").toString(), "resolving");
    }
    /** @brief ワーカー実行中は完了できないことを検証する */
    void remainsBusyWhenWorkerStillRunning()
    {
        // 準備 : ダウンロード段階として実行する
        OperationState state; state.run(OperationStage::Downloading);
        // 実行 : 完了を要求する
        const bool completed = state.complete();
        // 確認 : 完了せずビジーが続くことを確認する
        QVERIFY(!completed); QVERIFY(state.snapshot().value("busy").toBool());
    }
    /** @brief 終了処理中の完了要求でアイドルへ戻ることを検証する */
    void completesWhenFinishing()
    {
        // 準備 : 読み込み段階から終了処理中へ移す
        OperationState state; state.run(OperationStage::Reading); state.finishing();
        // 実行 : 完了を要求する
        QVERIFY(state.complete());
        // 確認 : アイドルになりビジーでないことを確認する
        QCOMPARE(state.snapshot().value("state").toString(), "idle");
        QVERIFY(!state.snapshot().value("busy").toBool());
    }
    /** @brief インストール中は取消しを拒否することを検証する */
    void refusesCancelWhenInstalling()
    {
        // 準備 : ダウンロードからインストール段階へ移す
        OperationState state; state.run(OperationStage::Downloading); state.stage(OperationStage::Installing);
        // 実行 : 取消しを要求する
        const bool accepted = state.requestCancel();
        // 確認 : 拒否され取消しフラグが立たないことを確認する
        QVERIFY(!accepted); QVERIFY(!state.cancelFlag().load());
    }
    /** @brief インストール後に届いた遅延ダウンロードコールバックで段階を戻さないことを検証する */
    void keepsRpmProtectedWhenLateDownloadCallbackArrives()
    {
        // 準備 : インストール段階から実行する
        OperationState state; state.run(OperationStage::Installing);
        // 実行 : ダウンロード段階へ戻そうとする
        state.stage(OperationStage::Downloading);
        // 確認 : 段階がインストールのままで取消し不可であることを確認する
        QCOMPARE(state.snapshot().value("stage").toString(), "installing");
        QVERIFY(!state.snapshot().value("cancellable").toBool());
    }
    /** @brief ダウンロード中のウォッチドッグ期限切れで取消しになることを検証する */
    void cancelsWhenWatchdogExpiresInDownload()
    {
        // 準備 : ダウンロード段階として実行する
        OperationState state; state.run(OperationStage::Downloading);
        // 実行 : ウォッチドッグを期限切れにする
        const auto action = state.watchdog(0ms);
        // 確認 : 取消し動作になりフラグが立つことを確認する
        QCOMPARE(action, WatchdogAction::Cancel); QVERIFY(state.cancelFlag().load());
        QVERIFY(state.snapshot().value("busy").toBool());
    }
    /** @brief rpm処理中のウォッチドッグ期限切れで取消さず警告することを検証する */
    void warnsWithoutCancelWhenWatchdogExpiresInRpm()
    {
        // 準備 : インストール段階として実行する
        OperationState state; state.run(OperationStage::Installing);
        // 実行 : ウォッチドッグを期限切れにする
        const auto action = state.watchdog(0ms);
        // 確認 : 警告のみで取消しフラグが立たないことを確認する
        QCOMPARE(action, WatchdogAction::Warn); QVERIFY(!state.cancelFlag().load());
    }
    /** @brief 直近のハートビートがある間は実行を続けることを検証する */
    void staysRunningWhenHeartbeatIsFresh()
    {
        // 準備 : リフレッシュ段階でハートビートを記録する
        OperationState state; state.run(OperationStage::Refreshing); state.heartbeat();
        // 実行 : 長めの経過時間でウォッチドッグを確認する
        const auto action = state.watchdog(10min);
        // 確認 : 何もしないことを確認する
        QCOMPARE(action, WatchdogAction::None);
    }
    /** @brief 新しい操作の開始で以前の取消しを消すことを検証する */
    void clearsPreviousCancelWhenNewOperationStarts()
    {
        // 準備 : 取消しから完了まで進めた操作状態を用意する
        OperationState state; state.run(OperationStage::Reading); state.requestCancel(); state.finishing(); state.complete();
        // 実行 : 初期化段階で新たに実行する
        QVERIFY(state.run(OperationStage::Initializing));
        // 確認 : 取消しフラグが消えていることを確認する
        QVERIFY(!state.cancelFlag().load());
    }

    /** @brief プロンプト応答待ちの間はウォッチドッグが発火しないことを検証する */
    void watchdogSuspendedWhileWaitingForPrompt()
    {
        // 準備 : ダウンロード中にプロンプト応答待ちを開始する
        OperationState state;
        QVERIFY(state.run(OperationStage::Downloading));
        state.beginPromptWait(5000ms);
        std::this_thread::sleep_for(20ms);

        // 確認 : 通常なら期限切れの短いタイムアウトでも発火しない
        QCOMPARE(state.watchdog(1ms), WatchdogAction::None);

        // 実行 : 応答待ちを終了する
        state.endPromptWait();
        std::this_thread::sleep_for(5ms);

        // 確認 : 通常の監視へ戻り、取消し可能な段階では取消し要求になる
        QCOMPARE(state.watchdog(1ms), WatchdogAction::Cancel);
    }

    /** @brief 応答待ちの上限を超えたら通常のウォッチドッグ判定へ戻ることを検証する */
    void watchdogResumesAfterPromptWaitBound()
    {
        // 準備 : 短い上限でプロンプト応答待ちを開始する
        OperationState state;
        QVERIFY(state.run(OperationStage::Downloading));
        state.beginPromptWait(10ms);
        std::this_thread::sleep_for(30ms);

        // 確認 : 上限超過後は無応答として取消し要求になる
        QCOMPARE(state.watchdog(1ms), WatchdogAction::Cancel);
    }
};

QTEST_GUILESS_MAIN(OperationStateTest)
#include "tst_OperationState.moc"
