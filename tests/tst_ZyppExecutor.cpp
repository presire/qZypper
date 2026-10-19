#include <QtTest>
#include "ZyppExecutor.h"
#include "ZyppManager.h"

using namespace qZypper;

/**
 * @class ZyppExecutorTest
 * @brief 常駐ワーカースレッド上のジョブ実行と取消しを検証する
 *
 * root権限を要さない読取ジョブを専用ワーカーへ直列実行し、返信が主スレッドへ戻ること、実行前取消しがジョブを走らせず、
 * 取消しエラーとして届くことを対象にする
 */
class ZyppExecutorTest : public QObject {
    Q_OBJECT
private slots:
    /** @brief 常駐ワーカーで直列実行し、返信が主スレッドに戻ることを検証する */
    void jobsUsePersistentWorkerAndReplyOnMain()
    {
        // 準備 : root権限もZYppの初期化も必要としない常駐ワーカー
        auto *mainThread = QThread::currentThread();
        QThread *workerThread = nullptr;
        QList<bool> results;
        ZyppExecutor executor;
        QVERIFY(executor.control().run(OperationStage::Reading));

        // 実行 : 別々のジョブで同じマネージャへの読取を直列実行する
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

        // 確認 : 両方とも同じ専用スレッドで実行し、返信は主スレッドに戻る
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

        // 実行 : 取消し済み操作をキュー投入する
        executor.submit([] {
            WorkerResult result;
            result.value = true;
            return result;
        }, [&](const WorkerResult &result) { outcome = result; completed = true; }, false);

        // 確認 : ジョブの値ではなく取消しエラーが主スレッドに届く
        QTRY_VERIFY_WITH_TIMEOUT(completed, 5000);
        QCOMPARE(outcome.errorName, QStringLiteral("org.presire.qzypper.Error.Cancelled"));
        QVERIFY(!outcome.value.isValid());
        executor.control().finishing();
        QVERIFY(executor.control().complete());
    }
};

QTEST_GUILESS_MAIN(ZyppExecutorTest)
#include "tst_ZyppExecutor.moc"
