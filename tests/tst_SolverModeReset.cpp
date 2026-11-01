#include <QStringList>
#include <QTemporaryDir>
#include <QtTest>
#include <zypp/RepoInfo.h>
#include <zypp/sat/Pool.h>

#include "SelectionFixture.h"
#include "ZyppManager.h"

using namespace qZypper;

namespace {

/**
 * @brief 変更予定のパッケージ名を昇順で返す
 * @param changes 変更予定一覧
 * @return パッケージ名の一覧
 */
QStringList pendingNames(const std::vector<PackageInfo> &changes)
{
    QStringList names;
    for (const PackageInfo &info : changes)
        names.append(info.name);
    names.sort();
    return names;
}

/** @brief libzyppリゾルバのupgradeModeを返す */
bool upgradeMode()
{
    return zypp::getZYpp()->resolver()->upgradeMode();
}

/** @brief libzyppリゾルバのupdateModeを返す */
bool updateMode()
{
    return zypp::getZYpp()->resolver()->updateMode();
}

} // namespace

/**
 * @brief ソルバーのupgradeMode/updateModeが取り消し後に残らないことを検証する
 */
class SolverModeResetTest : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void restoreClearsDistUpgrade();
    void restoreThenPatchesNotDup();
    void restoreClearsUpdateAll();
    void updateAllClearsUpgradeMode();
    void distUpgradeClearsUpdateMode();
    void dupPersistsAcrossResolve();
    void updatePersistsAcrossResolve();
    void cleanupTestCase();

private:
    QTemporaryDir m_root;
    SelectionFixture m_fixture;
};

/** @brief qz-availと@Systemを持つ一時ルートで初期化し、選択状態を保存する */
void SolverModeResetTest::initTestCase()
{
    qputenv("ZYPP_READONLY_HACK", "1");
    QVERIFY(m_root.isValid());

    if (!m_fixture.build(m_root.path())) {
        if (!m_fixture.skipReason.isEmpty())
            QSKIP(qPrintable(m_fixture.skipReason));
        QVERIFY2(false, qPrintable(m_fixture.error));
    }

    auto &manager = ZyppManager::instance();
    QVERIFY2(manager.initialize(m_root.path().toStdString(), true),
             manager.lastError().c_str());

    zypp::RepoInfo available;
    available.setAlias("qz-avail");
    zypp::sat::Pool::instance().addRepoSolv(
        zypp::Pathname(m_fixture.availSolv.toStdString()), available);

    zypp::sat::Pool::instance().reposErase(zypp::sat::Pool::systemRepoAlias());
    zypp::sat::Pool::instance().addRepoSolv(
        zypp::Pathname(m_fixture.systemSolv.toStdString()),
        zypp::sat::Pool::systemRepoAlias());

    QCOMPARE(manager.getPackagesByRepo("qz-avail").size(), std::size_t(6));

    manager.saveState();
}

/** @brief dist-upgradeを取り消すと再解決でdupが復活しないことを検証する */
void SolverModeResetTest::restoreClearsDistUpgrade()
{
    // 準備 : dist-upgradeでupgradeModeを立てる
    auto &manager = ZyppManager::instance();
    manager.restoreState();
    QVERIFY(manager.distUpgrade(false).success);
    QVERIFY(upgradeMode());

    // 実行 : 取消してから依存を解決する
    manager.restoreState();
    QVERIFY(!upgradeMode());
    QVERIFY(manager.resolveDependencies().success);

    // 確認 : 変更予定が空でupgradeModeも立たない
    QVERIFY(pendingNames(manager.getPendingChanges()).isEmpty());
    QVERIFY(!upgradeMode());
}

/** @brief dist-upgradeを取り消した後のパッチ選択がdupを再実行しないことを検証する */
void SolverModeResetTest::restoreThenPatchesNotDup()
{
    // 準備 : dist-upgradeでupgradeModeを立てる
    auto &manager = ZyppManager::instance();
    manager.restoreState();
    QVERIFY(manager.distUpgrade(false).success);
    QVERIFY(upgradeMode());

    // 実行 : 取消して必要なパッチを選択する
    manager.restoreState();
    const auto result = manager.selectAllNeededPatches(false, false, false);

    // 確認 : フィクスチャにパッチは無いため変更予定は空、upgradeModeも立たない
    QVERIFY(result.success);
    QVERIFY(pendingNames(manager.getPendingChanges()).isEmpty());
    QVERIFY(!upgradeMode());
}

/** @brief 全体更新を取り消すと再解決でupdateが復活しないことを検証する */
void SolverModeResetTest::restoreClearsUpdateAll()
{
    // 準備 : 全体更新でupdateModeを立てる
    auto &manager = ZyppManager::instance();
    manager.restoreState();
    QVERIFY(manager.updateAllPackages().success);
    QVERIFY(updateMode());

    // 実行 : 取消してから依存を解決する
    manager.restoreState();
    QVERIFY(!updateMode());
    QVERIFY(manager.resolveDependencies().success);

    // 確認 : 変更予定が空でupdateModeも立たない
    QVERIFY(pendingNames(manager.getPendingChanges()).isEmpty());
    QVERIFY(!updateMode());
}

/** @brief 全体更新がdist-upgradeのupgradeModeを解除することを検証する */
void SolverModeResetTest::updateAllClearsUpgradeMode()
{
    // 準備 : dist-upgradeでupgradeModeを立てる
    auto &manager = ZyppManager::instance();
    manager.restoreState();
    QVERIFY(manager.distUpgrade(false).success);
    QVERIFY(upgradeMode());

    // 実行 : 全体更新へ切り替える
    QVERIFY(manager.updateAllPackages().success);

    // 確認 : upgradeModeは解除され、updateModeが立つ
    QVERIFY(!upgradeMode());
    QVERIFY(updateMode());
}

/** @brief dist-upgradeが全体更新のupdateModeを解除することを検証する */
void SolverModeResetTest::distUpgradeClearsUpdateMode()
{
    // 準備 : 全体更新でupdateModeを立てる
    auto &manager = ZyppManager::instance();
    manager.restoreState();
    QVERIFY(manager.updateAllPackages().success);
    QVERIFY(updateMode());

    // 実行 : dist-upgradeへ切り替える
    QVERIFY(manager.distUpgrade(false).success);

    // 確認 : updateModeは解除され、upgradeModeが立つ
    QVERIFY(!updateMode());
    QVERIFY(upgradeMode());
}

/** @brief 取り消す前のdist-upgradeが再解決でも維持されることを検証する */
void SolverModeResetTest::dupPersistsAcrossResolve()
{
    // 準備 : dist-upgradeを実行する
    auto &manager = ZyppManager::instance();
    manager.restoreState();
    QVERIFY(manager.distUpgrade(false).success);

    // 実行 : もう一度依存を解決する
    QVERIFY(manager.resolveDependencies().success);

    // 確認 : upgradeModeとDUPの変更予定が維持される
    QVERIFY(upgradeMode());
    const QStringList names = pendingNames(manager.getPendingChanges());
    QVERIFY(names.contains(QStringLiteral("qz-app")));
    QVERIFY(names.contains(QStringLiteral("qz-extra")));
    QVERIFY(names.contains(QStringLiteral("qz-lib")));
}

/** @brief 取り消す前の全体更新が再解決でも維持されることを検証する */
void SolverModeResetTest::updatePersistsAcrossResolve()
{
    // 準備 : 全体更新を実行する
    auto &manager = ZyppManager::instance();
    manager.restoreState();
    QVERIFY(manager.updateAllPackages().success);

    // 実行 : もう一度依存を解決する
    QVERIFY(manager.resolveDependencies().success);

    // 確認 : updateModeと更新対象が維持される
    QVERIFY(updateMode());
    QVERIFY(pendingNames(manager.getPendingChanges()).contains(QStringLiteral("qz-app")));
}

/** @brief zyppの状態を解放する */
void SolverModeResetTest::cleanupTestCase()
{
    ZyppManager::instance().shutdown();
}

QTEST_GUILESS_MAIN(SolverModeResetTest)
#include "tst_SolverModeReset.moc"
