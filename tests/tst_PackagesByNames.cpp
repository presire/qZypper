/**
 * @file tst_PackagesByNames.cpp
 * @brief 名前指定の部分取り直し (getPackagesByNames) を一時ルートで検証する
 *
 * 入力 :
 *   ZYPPTMPDIR (一時ディレクトリの親)
 * 出力 :
 *   QtTestの終了コード
 * 依存 :
 *   Qt6::Test、qzypper-zypp、repo2solv (PackagesByNamesFixture経由)
 */

#include <QByteArray>
#include <QHash>
#include <QSet>
#include <QStringList>
#include <QTemporaryDir>
#include <QtTest>
#include <algorithm>
#include <cstddef>
#include <zypp/RepoInfo.h>
#include <zypp/ResPool.h>
#include <zypp/sat/Pool.h>
#include <zypp/ui/Selectable.h>

#include "PackagesByNamesFixture.h"
#include "PackageInfo.h"
#include "ZyppManager.h"

using namespace qZypper;

namespace {
/** @brief 名前の一覧をlibzypp呼び出し用に変換する
 * @param names 変換元の名前
 * @return 入力順のstd::stringのベクタ
 */
std::vector<std::string> toStdNames(const QStringList &names)
{
    std::vector<std::string> result;
    result.reserve(static_cast<std::size_t>(names.size()));
    for (const QString &name : names)
        result.push_back(name.toStdString());
    return result;
}

/** @brief 名前をキーにした行の索引を作る
 * @param rows 索引化するパッケージ情報
 * @return 名前から属性マップへの対応
 */
QHash<QString, QVariantMap> rowIndex(const std::vector<PackageInfo> &rows)
{
    QHash<QString, QVariantMap> index;
    for (const PackageInfo &row : rows)
        index.insert(row.name, row.toVariantMap());
    return index;
}

/**
 * @brief 1フィールドでも内容が違う行の名前を返す
 * @param before 変更前の行
 * @param after 変更後の行
 * @return 差分のあった名前 (昇順)
 */
QStringList changedNames(const std::vector<PackageInfo> &before,
                         const std::vector<PackageInfo> &after)
{
    const QHash<QString, QVariantMap> beforeRows = rowIndex(before);
    const QHash<QString, QVariantMap> afterRows = rowIndex(after);

    QSet<QString> names;
    for (auto it = beforeRows.cbegin(); it != beforeRows.cend(); ++it)
        names.insert(it.key());
    for (auto it = afterRows.cbegin(); it != afterRows.cend(); ++it)
        names.insert(it.key());

    QStringList changed;
    for (const QString &name : names) {
        if (beforeRows.value(name) != afterRows.value(name))
            changed.append(name);
    }
    changed.sort();
    return changed;
}

/** @brief 変更予定一覧に現れる名前の集合を返す */
QSet<QString> pendingNames()
{
    QSet<QString> names;
    for (const PackageInfo &info : ZyppManager::instance().getPendingChanges())
        names.insert(info.name);
    return names;
}

/** @brief 名前が一致する行を検索する
 * @param rows 検索対象の行
 * @param name 検索する名前
 * @return 一致する行への借用ポインタ、存在しない場合はnullptr
 */
const PackageInfo *findByName(const std::vector<PackageInfo> &rows, const QString &name)
{
    const auto found = std::find_if(rows.begin(), rows.end(),
        [&name](const PackageInfo &info) { return info.name == name; });
    return found == rows.end() ? nullptr : &*found;
}

/** @brief ソルバによる自動変更の状態か判定する
 * @param status パッケージ状態
 * @return AutoInstallまたはAutoUpdateならtrue
 */
bool isAutoStatus(int status)
{
    return status == static_cast<int>(PackageStatus::AutoInstall)
        || status == static_cast<int>(PackageStatus::AutoUpdate);
}
}

/**
 * @brief 名前指定のパッケージ取得が部分取り直しに使えることを検証する
 *
 * 一時ルートへqz-availと@Systemとqz-altを読み込み、rootやネットワークなしで
 * 行の一致・重複除去・状態の追随・変更が及ぶ名前の範囲を確かめる
 */
class PackagesByNamesTest : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void rowsMatchRepositoryView();
    void ignoresUnknownAndEmptyNames();
    void collapsesDuplicatesKeepingOrder();
    void statusRefetchFollowsSelection();
    void dependencyCoverage();
    void removalCoverage();
    void lockCoverage();
    void multiversionCoverage();
    void candidateChangeFollowsSelection();
    void cleanup();
    void cleanupTestCase();

private:
    void verifyCoverage(const QStringList &clicked,
                        const std::vector<PackageInfo> &beforeRows,
                        const QSet<QString> &pendingBefore,
                        const std::vector<PackageInfo> &afterRows);

    QTemporaryDir m_root;              // libzyppの一時ルート
    PackagesByNamesFixture m_fixture;  // 部分取り直し用リポジトリ
};

/** @brief zypp.confで複数版指定を有効にし、qz-avail/@System/qz-altを読み込む */
void PackagesByNamesTest::initTestCase()
{
    qputenv("ZYPP_READONLY_HACK", "1");
    QVERIFY(m_root.isValid());
    if (!m_fixture.build(m_root.path())) {
        if (!m_fixture.skipReason.isEmpty())
            QSKIP(qPrintable(m_fixture.skipReason));
        QVERIFY2(false, qPrintable(m_fixture.error));
    }

    // initializeTargetはZConfigを読み直し、初期化前のaddMultiversionSpecを捨てる
    // そのため複数版指定は初期化前に設定ファイルとして書く
    QVERIFY(PackagesByNamesFixture::writeFile(
        m_root.path() + QStringLiteral("/etc/zypp/zypp.conf"),
        QByteArray("[main]\nmultiversion = provides:multiversion(kernel)\n")));

    auto &manager = ZyppManager::instance();
    QVERIFY2(manager.initialize(m_root.path().toStdString(), true), manager.lastError().c_str());

    zypp::RepoInfo available;
    available.setAlias("qz-avail");
    zypp::sat::Pool::instance().addRepoSolv(
        zypp::Pathname(m_fixture.availSolv.toStdString()), available);
    zypp::sat::Pool::instance().reposErase(zypp::sat::Pool::systemRepoAlias());
    zypp::sat::Pool::instance().addRepoSolv(
        zypp::Pathname(m_fixture.systemSolv.toStdString()), zypp::sat::Pool::systemRepoAlias());
    zypp::RepoInfo alt;
    alt.setAlias("qz-alt");
    zypp::sat::Pool::instance().addRepoSolv(
        zypp::Pathname(m_fixture.altSolv.toStdString()), alt);

    QCOMPARE(manager.getPackagesByRepo("qz-avail").size(), std::size_t(6));
    QCOMPARE(manager.getPackagesByRepo("qz-alt").size(), std::size_t(1));

    // 前提 : qz-mvが複数版同時インストール対象として認識されている
    const auto mv = zypp::ui::Selectable::get(zypp::ResKind::package, "qz-mv");
    QVERIFY2(mv, "qz-mv is not in the pool");
    QVERIFY2(mv->multiversionInstall(),
             "qz-mv is not multiversion-capable (the multiversion spec was not applied)");
    qInfo().noquote() << QStringLiteral("MULTIVERSION qz-mv multiversionInstall=true");

    manager.saveState();
}

/** @brief 名前指定の行がリポジトリ別ビューの同名行と全フィールド一致する */
void PackagesByNamesTest::rowsMatchRepositoryView()
{
    auto &manager = ZyppManager::instance();
    manager.restoreState();
    const QStringList names{QStringLiteral("qz-app"), QStringLiteral("qz-lib"),
                            QStringLiteral("qz-new")};
    const auto rows = manager.getPackagesByNames(toStdNames(names));
    const auto byRepo = manager.getPackagesByRepo("qz-avail");

    QCOMPARE(rows.size(), std::size_t(3));
    for (const PackageInfo &row : rows) {
        const PackageInfo *same = findByName(byRepo, row.name);
        QVERIFY2(same, qPrintable(QStringLiteral("missing in repo view: %1").arg(row.name)));
        QCOMPARE(row.toVariantMap(), same->toVariantMap());
    }
}

/** @brief 空文字と存在しない名前は無視される */
void PackagesByNamesTest::ignoresUnknownAndEmptyNames()
{
    auto &manager = ZyppManager::instance();
    manager.restoreState();
    const QStringList names{QString(), QStringLiteral("qz-missing"), QStringLiteral("qz-new"),
                            QString()};
    const auto rows = manager.getPackagesByNames(toStdNames(names));

    QCOMPARE(rows.size(), std::size_t(1));
    QCOMPARE(rows.front().name, QStringLiteral("qz-new"));
    QVERIFY(manager.getPackagesByNames({}).empty());
}

/** @brief 重複は1行へ畳み込まれ、入力順が保たれる */
void PackagesByNamesTest::collapsesDuplicatesKeepingOrder()
{
    auto &manager = ZyppManager::instance();
    manager.restoreState();
    const QStringList names{QStringLiteral("qz-new"), QStringLiteral("qz-app"),
                            QStringLiteral("qz-new"), QStringLiteral("qz-lib"),
                            QStringLiteral("qz-app")};
    const auto rows = manager.getPackagesByNames(toStdNames(names));

    QStringList actual;
    for (const PackageInfo &row : rows)
        actual.append(row.name);
    const QStringList expected{QStringLiteral("qz-new"), QStringLiteral("qz-app"),
                               QStringLiteral("qz-lib")};
    QCOMPARE(actual, expected);
}

/** @brief 選択変更と再解決の後に状態がリポジトリ別ビューと同じになる */
void PackagesByNamesTest::statusRefetchFollowsSelection()
{
    auto &manager = ZyppManager::instance();
    manager.restoreState();
    QVERIFY2(manager.setPackageStatus("qz-new", static_cast<int>(PackageStatus::Install)),
             manager.lastError().c_str());
    QVERIFY2(manager.resolveDependencies().success, manager.lastError().c_str());

    const auto rows = manager.getPackagesByNames({std::string("qz-new")});
    const auto byRepo = manager.getPackagesByRepo("qz-avail");
    QCOMPARE(rows.size(), std::size_t(1));
    const PackageInfo *same = findByName(byRepo, QStringLiteral("qz-new"));
    QVERIFY(same);
    QCOMPARE(rows.front().status, same->status);
    QCOMPARE(rows.front().status, static_cast<int>(PackageStatus::Install));
}

/**
 * @brief 依存を引く操作(a)と取り消し(b)で、変化した行が取り直し対象に収まる
 *
 * (a) qz-req-topのインストールでqz-req-depが自動インストールになり、
 * (b) 取り消すと自動インストールから外れる
 */
void PackagesByNamesTest::dependencyCoverage()
{
    auto &manager = ZyppManager::instance();
    manager.restoreState();
    const std::vector<PackageInfo> before = manager.getPackagesByRepo("qz-avail");
    const QSet<QString> pendingBefore = pendingNames();
    QVERIFY(pendingBefore.isEmpty());

    // (a) 依存パッケージを引くインストール
    QVERIFY2(manager.setPackageStatus("qz-req-top", static_cast<int>(PackageStatus::Install)),
             manager.lastError().c_str());
    QVERIFY2(manager.resolveDependencies().success, manager.lastError().c_str());
    const auto depRows = manager.getPackagesByNames({std::string("qz-req-dep")});
    QCOMPARE(depRows.size(), std::size_t(1));
    QVERIFY2(isAutoStatus(depRows.front().status),
             qPrintable(QStringLiteral("qz-req-dep is not auto-installed: %1")
                            .arg(depRows.front().status)));
    const std::vector<PackageInfo> afterInstall = manager.getPackagesByRepo("qz-avail");
    verifyCoverage({QStringLiteral("qz-req-top")}, before, pendingBefore, afterInstall);

    // (b) 直前の操作を取り消す
    const QSet<QString> pendingBeforeCancel = pendingNames();
    QVERIFY2(manager.setPackageStatus("qz-req-top", static_cast<int>(PackageStatus::NoInst)),
             manager.lastError().c_str());
    QVERIFY2(manager.resolveDependencies().success, manager.lastError().c_str());
    const auto depRowsAfter = manager.getPackagesByNames({std::string("qz-req-dep")});
    QCOMPARE(depRowsAfter.size(), std::size_t(1));
    QVERIFY2(!isAutoStatus(depRowsAfter.front().status),
             qPrintable(QStringLiteral("qz-req-dep stayed auto-installed: %1")
                            .arg(depRowsAfter.front().status)));
    QVERIFY(!pendingNames().contains(QStringLiteral("qz-req-dep")));
    verifyCoverage({QStringLiteral("qz-req-top")}, afterInstall, pendingBeforeCancel,
                   manager.getPackagesByRepo("qz-avail"));
}

/** @brief 削除状態の行を取得でき、変化した行が取り直し対象に収まることを検証する */
void PackagesByNamesTest::removalCoverage()
{
    auto &manager = ZyppManager::instance();
    manager.restoreState();
    const std::vector<PackageInfo> before = manager.getPackagesByRepo("qz-avail");
    const QSet<QString> pendingBefore = pendingNames();

    QVERIFY2(manager.setPackageStatus("qz-inst", static_cast<int>(PackageStatus::Del)),
             manager.lastError().c_str());
    QVERIFY2(manager.resolveDependencies().success, manager.lastError().c_str());

    const auto rows = manager.getPackagesByNames({std::string("qz-inst")});
    QCOMPARE(rows.size(), std::size_t(1));
    QCOMPARE(rows.front().status, static_cast<int>(PackageStatus::Del));
    verifyCoverage({QStringLiteral("qz-inst")}, before, pendingBefore,
                   manager.getPackagesByRepo("qz-avail"));
}

/** @brief Taboo/Protectedの状態と変化した行が取り直し対象に収まることを検証する */
void PackagesByNamesTest::lockCoverage()
{
    auto &manager = ZyppManager::instance();

    // Taboo (インストール禁止) は未導入パッケージに適用する
    manager.restoreState();
    const std::vector<PackageInfo> beforeTaboo = manager.getPackagesByRepo("qz-avail");
    const QSet<QString> pendingBeforeTaboo = pendingNames();
    QVERIFY2(manager.setPackageStatus("qz-new", static_cast<int>(PackageStatus::Taboo)),
             manager.lastError().c_str());
    QVERIFY2(manager.resolveDependencies().success, manager.lastError().c_str());
    const auto tabooRows = manager.getPackagesByNames({std::string("qz-new")});
    QCOMPARE(tabooRows.size(), std::size_t(1));
    QCOMPARE(tabooRows.front().status, static_cast<int>(PackageStatus::Taboo));
    verifyCoverage({QStringLiteral("qz-new")}, beforeTaboo, pendingBeforeTaboo,
                   manager.getPackagesByRepo("qz-avail"));

    // Protected (削除禁止) は導入済みパッケージに適用する
    manager.restoreState();
    const std::vector<PackageInfo> beforeProtected = manager.getPackagesByRepo("qz-avail");
    const QSet<QString> pendingBeforeProtected = pendingNames();
    QVERIFY2(manager.setPackageStatus("qz-lock", static_cast<int>(PackageStatus::Protected)),
             manager.lastError().c_str());
    QVERIFY2(manager.resolveDependencies().success, manager.lastError().c_str());
    const auto protectedRows = manager.getPackagesByNames({std::string("qz-lock")});
    QCOMPARE(protectedRows.size(), std::size_t(1));
    QCOMPARE(protectedRows.front().status, static_cast<int>(PackageStatus::Protected));
    verifyCoverage({QStringLiteral("qz-lock")}, beforeProtected, pendingBeforeProtected,
                   manager.getPackagesByRepo("qz-avail"));
}

/**
 * @brief 複数版同時インストールの追加で、変化した行が取り直し対象に収まる
 *
 * qz-mvは1.0が導入済みで1.1を追加インストールできる (multiversion(kernel)を提供)
 */
void PackagesByNamesTest::multiversionCoverage()
{
    auto &manager = ZyppManager::instance();
    manager.restoreState();
    const std::vector<PackageInfo> before = manager.getPackagesByRepo("qz-avail");
    const QSet<QString> pendingBefore = pendingNames();

    QVERIFY2(manager.setPackageVersionStatus(
                 "qz-mv", "1.1-1", "noarch", "qz-avail",
                 static_cast<int>(PackageStatus::Install)),
             manager.lastError().c_str());
    QVERIFY2(manager.resolveDependencies().success, manager.lastError().c_str());

    bool found = false;
    for (const PackageInfo &info : manager.getPendingChanges()) {
        if (info.name == QStringLiteral("qz-mv") && info.version == QStringLiteral("1.1-1"))
            found = true;
    }
    QVERIFY2(found, "qz-mv 1.1-1 is not in the pending changes");

    const auto rows = manager.getPackagesByNames({std::string("qz-mv")});
    QCOMPARE(rows.size(), std::size_t(1));
    QCOMPARE(rows.front().version, QStringLiteral("1.1-1"));
    verifyCoverage({QStringLiteral("qz-mv")}, before, pendingBefore,
                   manager.getPackagesByRepo("qz-avail"));
}

/** @brief 候補変更の後、名前指定の行が変更先リポジトリの行と一致する */
void PackagesByNamesTest::candidateChangeFollowsSelection()
{
    auto &manager = ZyppManager::instance();
    manager.restoreState();
    QVERIFY2(manager.setPackageVersion("qz-lib", "1.2-1", "noarch", "qz-alt"),
             manager.lastError().c_str());

    const auto rows = manager.getPackagesByNames({std::string("qz-lib")});
    QCOMPARE(rows.size(), std::size_t(1));
    QCOMPARE(rows.front().version, QStringLiteral("1.2-1"));
    QCOMPARE(rows.front().repoAlias, QStringLiteral("qz-alt"));

    const std::vector<PackageInfo> altRows = manager.getPackagesByRepo("qz-alt");
    const PackageInfo *same = findByName(altRows, QStringLiteral("qz-lib"));
    QVERIFY(same);
    QCOMPARE(rows.front().toVariantMap(), same->toVariantMap());
}

/** @brief 選択状態を戻し、明示指定した候補を解除する */
void PackagesByNamesTest::cleanup()
{
    auto &manager = ZyppManager::instance();
    manager.restoreState();

    const zypp::ResPoolProxy proxy = zypp::ResPool::instance().proxy();
    for (auto it = proxy.byKindBegin<zypp::Package>(); it != proxy.byKindEnd<zypp::Package>(); ++it)
        (*it)->setCandidate(zypp::PoolItem());
}

/** @brief 一時プールを解放し、未初期化時は空を返すことを確かめる */
void PackagesByNamesTest::cleanupTestCase()
{
    auto &manager = ZyppManager::instance();
    manager.shutdown();
    QVERIFY(manager.getPackagesByNames({std::string("qz-new")}).empty());
}

/**
 * @brief 変化した行の名前が クリックした名前 ∪ 変更前の変更予定 ∪ 変更後の変更予定 に収まるか検査する
 * @param clicked 操作でクリックしたパッケージ名
 * @param beforeRows 操作前の getPackagesByRepo の行
 * @param pendingBefore 操作前の変更予定の名前
 * @param afterRows 操作後の getPackagesByRepo の行
 */
void PackagesByNamesTest::verifyCoverage(const QStringList &clicked,
                                         const std::vector<PackageInfo> &beforeRows,
                                         const QSet<QString> &pendingBefore,
                                         const std::vector<PackageInfo> &afterRows)
{
    QSet<QString> allowed = pendingBefore;
    allowed.unite(pendingNames());
    for (const QString &name : clicked)
        allowed.insert(name);

    const QStringList changed = changedNames(beforeRows, afterRows);
    for (const QString &name : changed) {
        QVERIFY2(allowed.contains(name),
                 qPrintable(QStringLiteral("row changed outside the refetch set: %1").arg(name)));
    }
}

QTEST_GUILESS_MAIN(PackagesByNamesTest)
#include "tst_PackagesByNames.moc"
