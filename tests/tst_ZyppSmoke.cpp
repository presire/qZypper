#include <QtTest>
#include <unistd.h>
#include "ZyppExecutor.h"
#include "ZyppManager.h"

using namespace qZypper;

/**
 * @class ZyppSmokeTest
 * @brief 明示的に有効化した時の読取専用システムキャッシュ検索を検証する
 *
 * QZYPPER_ZYPP_SMOKE=1かつ非rootの時のみ実行し、読取専用で初期化した上でrpm検索が1件以上を返すことを確認する
 * (既定では環境条件によりスキップする)
 */
class ZyppSmokeTest : public QObject {
    Q_OBJECT
private slots:
    /** @brief 環境変数で有効化した時、読取専用検索が結果を返すことを検証する */
    void searchesReadonlySystemCacheWhenExplicitlyEnabled()
    {
        // 準備 : 読取専用スモークを実行できる環境条件を確認する
        if (qEnvironmentVariable("QZYPPER_ZYPP_SMOKE") != "1") QSKIP("Set QZYPPER_ZYPP_SMOKE=1 for read-only cache smoke test");
        if (::geteuid() == 0) QSKIP("Read-only smoke test must not run as root");
        qputenv("ZYPP_READONLY_HACK", "1");
        ZyppExecutor executor;
        executor.control().run(OperationStage::Initializing);
        bool completed = false;
        WorkerResult outcome;
        // 実行 : 読取専用で初期化し、rpmを検索する
        executor.submit([] {
            auto &manager = ZyppManager::instance();
            WorkerResult result;
            if (!manager.initialize("/", true)) {
                result.errorText = QString::fromStdString(manager.lastError());
            } else {
                result.value = int(manager.searchPackages("rpm", 1).size());
            }
            return result;
        }, [&](const WorkerResult &result) { outcome = result; completed = true; }, false);
        // 確認 : エラーなく1件以上が返ることを確認する
        QTRY_VERIFY_WITH_TIMEOUT(completed, 30000);
        QVERIFY2(outcome.errorText.isEmpty(), qPrintable(outcome.errorText));
        QVERIFY(outcome.value.toInt() > 0);
    }
};

QTEST_GUILESS_MAIN(ZyppSmokeTest)
#include "tst_ZyppSmoke.moc"
