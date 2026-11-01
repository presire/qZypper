#include <QTemporaryDir>
#include <QtTest>
#include <algorithm>
#include <zypp/RepoInfo.h>
#include <zypp/sat/Pool.h>
#include <zypp/ui/Selectable.h>

#include "SelectionFixture.h"
#include "ZyppManager.h"

using namespace qZypper;

namespace {
/** @brief テスト中だけベンダー切り替え用リポジトリを読み込む */
struct TempRepoGuard
{
    explicit TempRepoGuard(const QString &solvPath)
    {
        zypp::RepoInfo info;
        info.setAlias("qz-vendor");
        zypp::sat::Pool::instance().addRepoSolv(zypp::Pathname(solvPath.toStdString()), info);
    }

    ~TempRepoGuard()
    {
        zypp::sat::Pool::instance().reposErase("qz-vendor");
    }
};

/** @brief 変更予定と実際の選択候補がPackman版qz-lib 1.8であることを確認する */
void verifyVendorCandidate()
{
    const auto changes = ZyppManager::instance().getPendingChanges();
    const auto found = std::find_if(changes.begin(), changes.end(), [](const PackageInfo &info) {
        return info.name == QStringLiteral("qz-lib");
    });
    QVERIFY(found != changes.end());
    QCOMPARE(found->repoAlias, QStringLiteral("qz-vendor"));
    QCOMPARE(found->version, QStringLiteral("1.8-1"));
    const auto selected = zypp::ui::Selectable::get(zypp::ResKind::package, "qz-lib");
    QVERIFY(selected && selected->candidateObj());
    QCOMPARE(selected->candidateObj()->repoInfo().alias(), std::string("qz-vendor"));
    QCOMPARE(selected->candidateObj()->edition().version(), std::string("1.8"));
}
}

/** @brief リポジトリ切り替えのベンダー許可・再解決・取消しを検証する */
class RepoSwitchTest : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void switchSelectsVendorChange();
    void switchSurvivesReResolve();
    void switchSurvivesSolverSettingChange();
    void disableSwitchReverts();
    void restoreStateCancelsSwitch();
    void rejectsUnknownAndSystem();
    void rejectsMalformedAlias();
    void distUpgradeWithSwitchAllowsVendor();
    void removedRepoIsDropped();
    void cleanup();
    void cleanupTestCase();

private:
    QTemporaryDir m_root;
    SelectionFixture m_fixture;
};

/** @brief qz-availと@Systemの一時プールを作り、切り替え前の状態を保存する */
void RepoSwitchTest::initTestCase()
{
    qputenv("ZYPP_READONLY_HACK", "1");
    QVERIFY(m_root.isValid());
    if (!m_fixture.build(m_root.path())) {
        if (!m_fixture.skipReason.isEmpty())
            QSKIP(qPrintable(m_fixture.skipReason));
        QVERIFY2(false, qPrintable(m_fixture.error));
    }
    auto &manager = ZyppManager::instance();
    QVERIFY2(manager.initialize(m_root.path().toStdString(), true), manager.lastError().c_str());
    zypp::RepoInfo available;
    available.setAlias("qz-avail");
    zypp::sat::Pool::instance().addRepoSolv(
        zypp::Pathname(m_fixture.availSolv.toStdString()), available);
    zypp::sat::Pool::instance().reposErase(zypp::sat::Pool::systemRepoAlias());
    zypp::sat::Pool::instance().addRepoSolv(
        zypp::Pathname(m_fixture.systemSolv.toStdString()), zypp::sat::Pool::systemRepoAlias());
    QCOMPARE(manager.getPackagesByRepo("qz-avail").size(), std::size_t(6));
    manager.saveState();
}

/** @brief 明示的な切り替えだけでベンダー変更候補を選択できる */
void RepoSwitchTest::switchSelectsVendorChange()
{
    auto &manager = ZyppManager::instance();
    manager.restoreState();
    const TempRepoGuard repo(m_fixture.vendorSolv);
    QVERIFY(manager.setRepoUpgrade("qz-vendor", true));
    QVERIFY(manager.setRepoUpgrade("qz-vendor", true));
    QVERIFY(manager.getPendingChanges().empty());
    QVERIFY(manager.resolveDependencies().success);
    verifyVendorCandidate();
    QCOMPARE(manager.upgradeRepos(), std::vector<std::string>{"qz-vendor"});
}

/** @brief 既定フラグを再適用した後も切り替え候補が残る */
void RepoSwitchTest::switchSurvivesReResolve()
{
    auto &manager = ZyppManager::instance();
    manager.restoreState();
    const TempRepoGuard repo(m_fixture.vendorSolv);
    QVERIFY(manager.setRepoUpgrade("qz-vendor", true));
    QVERIFY(manager.resolveDependencies().success);
    verifyVendorCandidate();
    QVERIFY(manager.resolveDependencies().success);
    verifyVendorCandidate();
}

/** @brief 通常更新のベンダー変更を禁止しても明示切り替えは維持される */
void RepoSwitchTest::switchSurvivesSolverSettingChange()
{
    auto &manager = ZyppManager::instance();
    manager.restoreState();
    const TempRepoGuard repo(m_fixture.vendorSolv);
    QVERIFY(manager.setRepoUpgrade("qz-vendor", true));
    QVERIFY(manager.resolveDependencies().success);
    verifyVendorCandidate();
    ZyppManager::SolverOverrides settings;
    settings.allowVendorChange = false;
    QVERIFY(manager.setSolverSettings(settings));
    QVERIFY(manager.resolveDependencies().success);
    verifyVendorCandidate();
}

/** @brief 切り替えを解除するとソルバー由来の変更予定も取り消される */
void RepoSwitchTest::disableSwitchReverts()
{
    auto &manager = ZyppManager::instance();
    manager.restoreState();
    const TempRepoGuard repo(m_fixture.vendorSolv);
    QVERIFY(manager.setRepoUpgrade("qz-vendor", true));
    QVERIFY(manager.resolveDependencies().success);
    verifyVendorCandidate();
    QVERIFY(manager.setRepoUpgrade("qz-vendor", false));
    QVERIFY(manager.resolveDependencies().success);
    const auto changes = manager.getPendingChanges();
    QVERIFY(std::none_of(changes.begin(), changes.end(), [](const PackageInfo &info) {
        return info.name == QStringLiteral("qz-lib");
    }));
    QVERIFY(manager.upgradeRepos().empty());
}

/** @brief 保存状態の復元で切り替え要求と変更予定を取り消せる */
void RepoSwitchTest::restoreStateCancelsSwitch()
{
    auto &manager = ZyppManager::instance();
    manager.restoreState();
    const TempRepoGuard repo(m_fixture.vendorSolv);
    QVERIFY(manager.setRepoUpgrade("qz-vendor", true));
    QVERIFY(manager.resolveDependencies().success);
    verifyVendorCandidate();
    manager.restoreState();
    QVERIFY(manager.upgradeRepos().empty());
    QVERIFY(manager.resolveDependencies().success);
    QVERIFY(manager.getPendingChanges().empty());
}

/** @brief 空・不明・システムのエイリアスは正確な理由で拒否される */
void RepoSwitchTest::rejectsUnknownAndSystem()
{
    auto &manager = ZyppManager::instance();
    manager.restoreState();
    const TempRepoGuard repo(m_fixture.vendorSolv);
    QVERIFY(!manager.setRepoUpgrade("", true));
    QCOMPARE(manager.lastError(), std::string("Repository alias is empty"));
    QVERIFY(!manager.setRepoUpgrade("unknown", true));
    QCOMPARE(manager.lastError(), std::string("Repository not found: unknown"));
    QVERIFY(!manager.setRepoUpgrade(zypp::sat::Pool::systemRepoAlias(), true));
    QCOMPARE(manager.lastError(), std::string("System repository cannot be switched"));
    QVERIFY(manager.upgradeRepos().empty());
}

/** @brief 不正な形式のエイリアスは検証で拒否され、切り替え対象リストは変わらない */
void RepoSwitchTest::rejectsMalformedAlias()
{
    auto &manager = ZyppManager::instance();
    manager.restoreState();
    const TempRepoGuard repo(m_fixture.vendorSolv);
    QVERIFY(manager.upgradeRepos().empty());
    const std::vector<std::string> malformed = {
        "../qz-vendor", "qz/vendor", "qz\\vendor", "[qz]", ".qz", "qz\nvendor"
    };
    for (const auto &alias : malformed) {
        const auto before = manager.upgradeRepos();
        QVERIFY2(!manager.setRepoUpgrade(alias, true), alias.c_str());
        QVERIFY2(!manager.lastError().empty(), alias.c_str());
        QVERIFY2(manager.lastError().rfind("Invalid repository alias: ", 0) == 0,
                 manager.lastError().c_str());
        QCOMPARE(manager.upgradeRepos(), before);
        QVERIFY(manager.upgradeRepos().empty());
    }
}

/** @brief DUPの通常指定より明示的な切り替えのベンダー許可を優先する */
void RepoSwitchTest::distUpgradeWithSwitchAllowsVendor()
{
    auto &manager = ZyppManager::instance();
    manager.restoreState();
    const TempRepoGuard repo(m_fixture.vendorSolv);
    QVERIFY(manager.setRepoUpgrade("qz-vendor", true));
    QVERIFY(manager.distUpgrade(false).success);
    verifyVendorCandidate();
    QVERIFY(manager.resolveDependencies().success);
    verifyVendorCandidate();
}

/** @brief 消滅したリポジトリは照会から除外され、再解決時に要求も破棄される */
void RepoSwitchTest::removedRepoIsDropped()
{
    auto &manager = ZyppManager::instance();
    manager.restoreState();
    {
        const TempRepoGuard repo(m_fixture.vendorSolv);
        QVERIFY(manager.setRepoUpgrade("qz-vendor", true));
    }
    QVERIFY(manager.upgradeRepos().empty());
    QVERIFY(manager.resolveDependencies().success);
    QVERIFY(manager.upgradeRepos().empty());
    QVERIFY(!zypp::getZYpp()->resolver()->upgradingRepos());
    const TempRepoGuard reloaded(m_fixture.vendorSolv);
    QVERIFY(manager.upgradeRepos().empty());
}

/** @brief スロットで上書きしたソルバー設定を既定値へ戻す */
void RepoSwitchTest::cleanup()
{
    ZyppManager::instance().resetSolverOverrides();
}

/** @brief 一時プールを解放する */
void RepoSwitchTest::cleanupTestCase()
{
    ZyppManager::instance().shutdown();
    QVERIFY(!ZyppManager::instance().setRepoUpgrade("qz-vendor", true));
}

QTEST_GUILESS_MAIN(RepoSwitchTest)
#include "tst_RepoSwitch.moc"
