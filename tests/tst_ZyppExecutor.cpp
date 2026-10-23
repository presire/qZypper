#include <QtTest>
#include "ZyppExecutor.h"
#include "ZyppManager.h"

using namespace qZypper;

/**
 * @class ZyppExecutorTest
 * @brief 常駐ワーカースレッド上のジョブ実行と取消しを検証する
 *
 * root権限を要さない読み取りジョブを専用ワーカーで直列実行する
 *
 * 返信がメインスレッドへ戻ること、
 * 実行前の取消しがジョブを実行せず取消しエラーとして届くことを対象にする
 */
class ZyppExecutorTest : public QObject {
    Q_OBJECT
private slots:
    /** @brief 常駐ワーカーで直列実行し、返信がメインスレッドに戻ることを検証する */
    void jobsUsePersistentWorkerAndReplyOnMain()
    {
        // 準備 : root権限もlibzyppの初期化も必要としない常駐ワーカー
        auto *mainThread = QThread::currentThread();
        QThread *workerThread = nullptr;
        QList<bool> results;
        ZyppExecutor executor;
        QVERIFY(executor.control().run(OperationStage::Reading));

        // 実行 : 別々のジョブで同じマネージャへの読み取りを直列実行する
        executor.submit([&] {
            workerThread = QThread::currentThread();
            WorkerResult result;
            result.value = workerThread != mainThread && ZyppManager::instance().getRepos().empty();
            return result;
        }, [&](const WorkerResult &result) {
            results.append(result.errorName.isEmpty() && result.value.toBool()
                           && QThread::currentThread() == mainThread);
        }, false);
        executor.submit([&] {
            WorkerResult result;
            result.value = QThread::currentThread() == workerThread
                && !ZyppManager::instance().isInitialized();
            return result;
        }, [&](const WorkerResult &result) {
            results.append(result.errorName.isEmpty() && result.value.toBool()
                           && QThread::currentThread() == mainThread);
        }, false);

        // 確認 : 両方とも同じ専用スレッドで実行し、返信はメインスレッドに戻る
        QTRY_COMPARE_WITH_TIMEOUT(results.size(), 2, 5000);
        QCOMPARE(results, QList<bool>({true, true}));
        executor.control().finishing();
        QVERIFY(executor.control().complete());
    }

    /** @brief 取消し済みジョブが実行されず、取消しエラーを返すことを検証する */
    void cancelledQueuedJobNeverRuns()
    {
        // 準備 : 実行開始前に取消しが届いたジョブ
        bool completed = false;
        WorkerResult outcome;
        ZyppExecutor executor;
        QVERIFY(executor.control().run(OperationStage::Resolving));
        QVERIFY(executor.control().requestCancel());

        // 実行 : 取消し済み操作をキューへ投入する
        executor.submit([] {
            WorkerResult result;
            result.value = true;
            return result;
        }, [&](const WorkerResult &result) { outcome = result; completed = true; }, false);

        // 確認 : ジョブの値ではなく取消しエラーがメインスレッドに届く
        QTRY_VERIFY_WITH_TIMEOUT(completed, 5000);
        QCOMPARE(outcome.errorName, QStringLiteral("org.presire.qzypper.Error.Cancelled"));
        QVERIFY(!outcome.value.isValid());
        executor.control().finishing();
        QVERIFY(executor.control().complete());
    }

    /** @brief ask()の応答待ち中はウォッチドッグが発火しないことを検証する */
    void askSuspendsWatchdogWhilePromptPending()
    {
        // 準備 : ワーカーでask()を呼ぶジョブを投入する
        ZyppExecutor executor;
        QVERIFY(executor.control().run(OperationStage::Downloading));
        executor.prompts().open();
        QSignalSpy promptSpy(&executor, &ZyppExecutor::promptRequested);
        QSignalSpy closedSpy(&executor, &ZyppExecutor::promptClosed);
        bool completed = false;

        executor.submit([&] {
            PromptRequest request;
            request.type = QStringLiteral("install-error");
            request.timeout = std::chrono::milliseconds(500);
            request.actions = QStringList{QStringLiteral("abort"), QStringLiteral("retry")};
            request.safeDefault = QVariantMap{{QStringLiteral("action"), QStringLiteral("abort")}};
            WorkerResult result;
            result.value = executor.ask(request);
            return result;
        }, [&](const WorkerResult &) { completed = true; }, false);

        // 実行 : プロンプトが届くまで待ち、その間は短いタイムアウトでも監視しない
        QTRY_COMPARE_WITH_TIMEOUT(promptSpy.size(), 1, 5000);
        QCOMPARE(executor.control().watchdog(std::chrono::milliseconds(1)),
                 WatchdogAction::None);

        // 実行 : [Abort]で回答する
        const quint32 id = promptSpy.first().at(0).toUInt();
        QVERIFY(executor.prompts().answer(id, QVariantMap{{QStringLiteral("action"),
                                                           QStringLiteral("abort")}}));

        // 確認 : ジョブが完了し、ダイアログを閉じる通知も届く
        QTRY_VERIFY_WITH_TIMEOUT(completed, 5000);
        QCOMPARE(closedSpy.size(), 1);
    }
};

QTEST_GUILESS_MAIN(ZyppExecutorTest)
#include "tst_ZyppExecutor.moc"
