#include "ZyppExecutor.h"
#include "ZyppManager.h"
#include <QtTest>
#include <unistd.h>

using namespace qZypper;

class ZyppSmokeTest : public QObject {
    Q_OBJECT
private slots:
    void searchesReadonlySystemCacheWhenExplicitlyEnabled()
    {
        // Given
        if (qEnvironmentVariable("QZYPPER_ZYPP_SMOKE") != "1") QSKIP("Set QZYPPER_ZYPP_SMOKE=1 for read-only cache smoke test");
        if (::geteuid() == 0) QSKIP("Read-only smoke test must not run as root");
        qputenv("ZYPP_READONLY_HACK", "1");
        ZyppExecutor executor;
        executor.control().run(OperationStage::Initializing);
        bool completed = false;
        WorkerResult outcome;
        // When
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
        // Then
        QTRY_VERIFY_WITH_TIMEOUT(completed, 30000);
        QVERIFY2(outcome.errorText.isEmpty(), qPrintable(outcome.errorText));
        QVERIFY(outcome.value.toInt() > 0);
    }
};

QTEST_GUILESS_MAIN(ZyppSmokeTest)
#include "tst_ZyppSmoke.moc"
