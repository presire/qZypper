#include "ZyppExecutor.h"
#include "ZyppManager.h"
#include <QtTest>

using namespace qZypper;

class ZyppExecutorTest : public QObject {
    Q_OBJECT
private slots:
    void jobsUsePersistentWorkerAndReplyOnMain()
    {
        // Given: root権限もZYppの初期化も必要としない常駐ワーカー。
        auto *mainThread = QThread::currentThread();
        QThread *workerThread = nullptr;
        QList<bool> results;
        ZyppExecutor executor;
        QVERIFY(executor.control().run(OperationStage::Reading));

        // When: 別々のジョブで同じマネージャへの読取を直列実行する。
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

        // Then: 両方とも同じ専用スレッドで実行し、返信は主スレッドに戻る。
        QTRY_COMPARE_WITH_TIMEOUT(results.size(), 2, 5000);
        QCOMPARE(results, QList<bool>({true, true}));
        executor.control().finishing();
        QVERIFY(executor.control().complete());
    }

    void cancelledQueuedJobNeverRuns()
    {
        // Given: 実行開始前に取消しが届いたジョブ。
        bool completed = false;
        WorkerResult outcome;
        ZyppExecutor executor;
        QVERIFY(executor.control().run(OperationStage::Resolving));
        QVERIFY(executor.control().requestCancel());

        // When: 取消し済み操作をキュー投入する。
        executor.submit([] {
            WorkerResult result;
            result.value = true;
            return result;
        }, [&](const WorkerResult &result) { outcome = result; completed = true; }, false);

        // Then: ジョブの値ではなく取消しエラーが主スレッドに届く。
        QTRY_VERIFY_WITH_TIMEOUT(completed, 5000);
        QCOMPARE(outcome.errorName, QStringLiteral("org.presire.qzypper.Error.Cancelled"));
        QVERIFY(!outcome.value.isValid());
        executor.control().finishing();
        QVERIFY(executor.control().complete());
    }
};

QTEST_GUILESS_MAIN(ZyppExecutorTest)
#include "tst_ZyppExecutor.moc"
