#include <QtTest>
#include <QElapsedTimer>
#include <QSignalSpy>
#include <unistd.h>
#include <vector>
#include "PackageInfo.h"
#include "PackageRow.h"
#include "RepoInfo.h"
#include "ZyppManager.h"
#include "PackageListModel.h"
#include "PackageFilterProxyModel.h"

using namespace qZypper;

namespace {

// (a) の検索対象: 名前・キーワード・概要 (説明文を含めない)
constexpr int kFlagsWithoutDescription =
    static_cast<int>(SearchFlag::Name)
    | static_cast<int>(SearchFlag::Keywords)
    | static_cast<int>(SearchFlag::Summary);

// (b) の検索対象: (a) に説明文を加える
constexpr int kFlagsWithDescription =
    kFlagsWithoutDescription | static_cast<int>(SearchFlag::Description);

/**
 * @brief 計測結果を1行で出力する
 * @param name ケース名
 * @param ms 計測時間 (ミリ秒)
 * @param rows 結果行数
 */
void printPerf(const char *name, qint64 ms, int rows)
{
    qInfo().noquote() << QStringLiteral("PERF case=%1 ms=%2 rows=%3")
                             .arg(QLatin1String(name))
                             .arg(ms)
                             .arg(rows);
}

} // namespace

/**
 * @class PerfBaselineTest
 * @brief リポジトリ切替の性能ベースラインを読取専用で計測する
 *
 * QZYPPER_PERF=1かつ非rootの時のみ実行する。しきい値では判定せず、
 * 各ケースの所要時間と行数をPERF行として出力するだけにとどめる
 */
class PerfBaselineTest : public QObject {
    Q_OBJECT
private slots:
    /** @brief (a)-(h)の性能ケースを順に計測する */
    void measuresBaseline()
    {
        // 準備 : 計測を許可した環境か確認する
        if (qEnvironmentVariable("QZYPPER_PERF") != "1")
            QSKIP("Set QZYPPER_PERF=1 to run the performance baseline");
        if (::geteuid() == 0)
            QSKIP("Performance baseline must not run as root");
        qputenv("ZYPP_READONLY_HACK", "1");

        ZyppManager &manager = ZyppManager::instance();
        QVERIFY2(manager.initialize("/", true),
                 qPrintable(QString::fromStdString(manager.lastError())));

        QElapsedTimer timer;

        // (a) 説明文を含めない部分一致検索
        timer.start();
        const std::vector<PackageInfo> searchLib =
            manager.searchPackages("lib", kFlagsWithoutDescription);
        printPerf("search-lib", timer.elapsed(), static_cast<int>(searchLib.size()));

        // (b) 説明文を含む部分一致検索
        timer.start();
        const std::vector<PackageInfo> searchLibWithDescription =
            manager.searchPackages("lib", kFlagsWithDescription);
        printPerf("search-lib-desc", timer.elapsed(),
                  static_cast<int>(searchLibWithDescription.size()));

        // (c) パッケージ数が最多の有効リポジトリを探し、その取得時間を測る
        std::string largestAlias;
        std::size_t largestCount = 0;
        for (const RepoInfo &repo : manager.getRepos()) {
            if (!repo.enabled)
                continue;
            const std::size_t count =
                manager.getPackagesByRepo(repo.alias.toStdString()).size();
            if (count > largestCount) {
                largestCount = count;
                largestAlias = repo.alias.toStdString();
            }
        }
        QVERIFY2(!largestAlias.empty(), "No enabled repository is available");
        timer.start();
        const std::vector<PackageInfo> largestRepoPackages =
            manager.getPackagesByRepo(largestAlias);
        printPerf("repo-max", timer.elapsed(),
                  static_cast<int>(largestRepoPackages.size()));

        // (d) (a)の結果をGUIモデルの行形式へ変換する時間
        timer.start();
        const PackageRows packageRows = toPackageRows(searchLib);
        printPerf("row-convert", timer.elapsed(), packageRows.size());

        // (e) 差分更新モデルへ初めて反映する時間
        PackageListModel model;
        timer.start();
        model.applyPackages(packageRows);
        printPerf("apply-initial", timer.elapsed(), model.rowCount());

        // (f) 1行のステータスだけを変えて再適用する時間とdataChangedの回数
        PackageRows oneRowChanged = packageRows;
        QVERIFY(!oneRowChanged.isEmpty());
        const int originalStatus = oneRowChanged.at(0).status;
        oneRowChanged[0].status =
            originalStatus == static_cast<int>(PackageStatus::Install)
                ? static_cast<int>(PackageStatus::NoInst)
                : static_cast<int>(PackageStatus::Install);
        QSignalSpy dataChanged(&model, &QAbstractItemModel::dataChanged);
        timer.start();
        model.applyPackages(oneRowChanged);
        qInfo().noquote() << QStringLiteral(
                                 "PERF case=apply-one-row ms=%1 rows=%2 dataChanged=%3")
                                 .arg(timer.elapsed())
                                 .arg(model.rowCount())
                                 .arg(dataChanged.count());

        // (g) プロキシへ接続して名前順にソートし直す時間
        PackageFilterProxyModel proxy;
        timer.start();
        proxy.setSourceModel(&model);
        proxy.setSortColumn(PackageFilterProxyModel::NameColumn);
        printPerf("proxy-sort", timer.elapsed(), proxy.rowCount());

        // (i) 安定キーから行番号を引く時間
        timer.start();
        int resolvedKeys = 0;
        for (int row = 0; row < model.rowCount(); ++row) {
            if (model.rowForKey(model.keyAt(row)) == row)
                ++resolvedKeys;
        }
        printPerf("key-lookup", timer.elapsed(), resolvedKeys);

        // (j) 全行選択後の表示中選択件数を求める時間
        QList<int> allRows;
        allRows.reserve(model.rowCount());
        for (int row = 0; row < model.rowCount(); ++row)
            allRows.append(row);
        model.setRowsSelected(allRows, true);
        timer.start();
        const int selectedVisible = proxy.selectedVisibleCount();
        printPerf("selection-count", timer.elapsed(), selectedVisible);

        // (h) 未インストールの1パッケージをInstallにし、依存解決と変更予定取得を測る
        const PackageInfo *candidate = nullptr;
        for (const PackageInfo &package : searchLib) {
            if (package.status == static_cast<int>(PackageStatus::NoInst)
                && package.installedVersion.isEmpty()) {
                candidate = &package;
                break;
            }
        }
        if (!candidate) {
            for (const PackageInfo &package : largestRepoPackages) {
                if (package.status == static_cast<int>(PackageStatus::NoInst)
                    && package.installedVersion.isEmpty()) {
                    candidate = &package;
                    break;
                }
            }
        }
        QVERIFY2(candidate, "No not-installed package is available for selection");

        timer.start();
        manager.saveState();
        const qint64 saveMs = timer.elapsed();

        timer.start();
        const bool statusSet = manager.setPackageStatus(
            candidate->name.toStdString(), static_cast<int>(PackageStatus::Install));
        const qint64 setMs = timer.elapsed();

        timer.start();
        (void)manager.resolveDependencies();
        const qint64 resolveMs = timer.elapsed();

        timer.start();
        const std::vector<PackageInfo> pendingChanges = manager.getPendingChanges();
        const int pendingRows = static_cast<int>(pendingChanges.size());
        const qint64 pendingMs = timer.elapsed();

        // (k) 部分取り直し: 選択した名前と変更予定の名前だけを取得する時間
        std::vector<std::string> refetchNames;
        refetchNames.push_back(candidate->name.toStdString());
        for (const PackageInfo &change : pendingChanges)
            refetchNames.push_back(change.name.toStdString());
        timer.start();
        const std::vector<PackageInfo> refetched = manager.getPackagesByNames(refetchNames);
        const qint64 refetchMs = timer.elapsed();

        timer.start();
        manager.restoreState();
        const qint64 restoreMs = timer.elapsed();
        QVERIFY2(statusSet, qPrintable(QString::fromStdString(manager.lastError())));
        qInfo().noquote() << QStringLiteral(
                                 "PERF case=selection-solve ms=%1 rows=%2 saveMs=%3 setMs=%4 resolveMs=%5 pendingMs=%6 restoreMs=%7")
                                 .arg(saveMs + setMs + resolveMs + pendingMs + restoreMs)
                                 .arg(pendingRows)
                                 .arg(saveMs)
                                 .arg(setMs)
                                 .arg(resolveMs)
                                 .arg(pendingMs)
                                 .arg(restoreMs);

        printPerf("partial-refetch", refetchMs, static_cast<int>(refetched.size()));

        manager.shutdown();
    }
};

QTEST_GUILESS_MAIN(PerfBaselineTest)
#include "tst_PerfBaseline.moc"
