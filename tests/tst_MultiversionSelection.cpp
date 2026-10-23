/**
 * @file tst_MultiversionSelection.cpp
 * @brief 複数版パッケージの版単位選択APIを一時ルートで検証する
 *
 * 入力 :
 *   ZYPPTMPDIR (一時ディレクトリの親)
 * 出力 :
 *   QtTestの終了コード
 * 依存 :
 *   Qt6::Test、qzypper-zypp、repo2solv (SelectionFixture経由)
 */

#include <QStringList>
#include <QTemporaryDir>
#include <QtTest>
#include <zypp/RepoInfo.h>
#include <zypp/ResPool.h>
#include <zypp/ZConfig.h>
#include <zypp/sat/Pool.h>
#include <zypp/ui/Selectable.h>

#include "PackageInfo.h"
#include "SelectionFixture.h"
#include "ZyppManager.h"

using namespace qZypper;

namespace {
/**
 * @brief 変更予定一覧に指定バージョンの項目があるかを返す
 * @param changes 変更予定の一覧
 * @param version 対象バージョン
 * @return 見つかった場合はtrue
 * @note 一覧の内容は変更しない
 */
bool hasVersion(const std::vector<PackageInfo> &changes, const QString &version)
{
    for (const PackageInfo &entry : changes) {
        if (entry.version == version) {
            return true;
        }
    }

    return false;
}
} // namespace

/**
 * @class MultiversionSelectionTest
 * @brief パッケージ版単位の選択APIを一時ルートで検証する
 * @note 複数版ビットをモデル化できない環境では該当テストをQSKIPする
 */
class MultiversionSelectionTest : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void init();
    void unknownPackageRejected();
    void unknownVersionRejected();
    void invalidStatusRejected();
    void nonMultiversionRowRejected();
    void perVersionSelectionAndPendingChanges();

private:
    QTemporaryDir m_root;                          // 一時ルートのディレクトリ
    SelectionFixture m_fixture;                    // リポジトリを構築するフィクスチャ
    bool m_multiversionModeled = false;            // 複数版ビットをモデル化できたか
};

/**
 * @brief 一時ルートへリポジトリを構築してZyppManagerを初期化する
 * @note リポジトリを追加できない場合はQSKIPまたはQVERIFY2で失敗する
 */
void MultiversionSelectionTest::initTestCase()
{
    // 準備 : libzyppを読み取り専用にし、一時ルートへフィクスチャを構築する
    qputenv("ZYPP_READONLY_HACK", "1");
    QVERIFY(m_root.isValid());

    if (!m_fixture.build(m_root.path())) {
        if (!m_fixture.skipReason.isEmpty()) {
            QSKIP(qPrintable(m_fixture.skipReason));
        }

        QVERIFY2(false, qPrintable(m_fixture.error));
    }

    // 準備 : 複数版可能パッケージの指定をプール読み込み前に登録する
    zypp::ZConfig::instance().addMultiversionSpec("qz-lib");

    auto &manager = ZyppManager::instance();
    QVERIFY2(manager.initialize(m_root.path().toStdString(), true),
             manager.lastError().c_str());

    // 準備 : メインリポジトリ・代替リポジトリ・システムを登録する
    zypp::RepoInfo available;
    available.setAlias("qz-avail");
    zypp::sat::Pool::instance().addRepoSolv(
        zypp::Pathname(m_fixture.availSolv.toStdString()), available);

    zypp::RepoInfo alternative;
    alternative.setAlias("qz-alt");
    zypp::sat::Pool::instance().addRepoSolv(
        zypp::Pathname(m_fixture.altSolv.toStdString()), alternative);

    zypp::sat::Pool::instance().reposErase(zypp::sat::Pool::systemRepoAlias());
    zypp::sat::Pool::instance().addRepoSolv(
        zypp::Pathname(m_fixture.systemSolv.toStdString()),
        zypp::sat::Pool::systemRepoAlias());

    // 準備 : フィクスチャが複数版ビットを実際にモデル化できたか記録する
    const auto selectable = zypp::ui::Selectable::get(zypp::ResKind::package, "qz-lib");
    m_multiversionModeled = selectable && selectable->multiversionInstall();

    manager.saveState();
}

/**
 * @brief 各テスト前に選択状態を復元し、明示的な候補指定を解除する
 * @note 前のテストの選択が次へ漏れないようにする
 */
void MultiversionSelectionTest::init()
{
    ZyppManager::instance().restoreState();
    const zypp::ResPoolProxy proxy = zypp::ResPool::instance().proxy();

    for (auto it = proxy.byKindBegin<zypp::Package>(); it != proxy.byKindEnd<zypp::Package>(); ++it) {
        (*it)->setCandidate(zypp::PoolItem());
    }
}

/**
 * @brief 存在しないパッケージ名がエラーになることを確認する
 * @note エラー文字列はZyppManager::lastErrorから取得する
 */
void MultiversionSelectionTest::unknownPackageRejected()
{
    // 実行 : 未知のパッケージへインストールを要求する
    const bool ok = ZyppManager::instance().setPackageVersionStatus(
        "qz-missing", "1.0", "noarch", "qz-avail", static_cast<int>(PackageStatus::Install));

    // 確認 : 拒否され、理由がm_lastErrorに設定される
    QVERIFY(!ok);
    QVERIFY(ZyppManager::instance().lastError().find("not found") != std::string::npos);
}

/**
 * @brief 存在しないバージョンがエラーになることを確認する
 * @note 版・arch・リポジトリの組合せで一致を判定する
 */
void MultiversionSelectionTest::unknownVersionRejected()
{
    // 実行 : 存在しないバージョンを指定する
    const bool ok = ZyppManager::instance().setPackageVersionStatus(
        "qz-lib", "9.9.9-1", "noarch", "qz-avail", static_cast<int>(PackageStatus::Install));

    // 確認 : 拒否される
    QVERIFY(!ok);
    QVERIFY(ZyppManager::instance().lastError().find("not found") != std::string::npos);
}

/**
 * @brief 許可されない状態値がエラーになることを確認する
 * @note 許可するのはInstall・NoInst・KeepInstalled・Delのみ
 */
void MultiversionSelectionTest::invalidStatusRejected()
{
    // 実行 : 未定義の状態値と、許可外の列挙値でそれぞれ要求する
    const bool unknownOk = ZyppManager::instance().setPackageVersionStatus(
        "qz-lib", "1.5-1", "noarch", "qz-avail", 999);
    const bool updateOk = ZyppManager::instance().setPackageVersionStatus(
        "qz-lib", "1.5-1", "noarch", "qz-avail", static_cast<int>(PackageStatus::Update));

    // 確認 : どちらも拒否される
    QVERIFY(!unknownOk);
    QVERIFY(!updateOk);
}

/**
 * @brief 複数版非対応パッケージへの版指定がエラーになることを確認する
 * @note 複数版非対応行は版単位の選択を許可しない
 */
void MultiversionSelectionTest::nonMultiversionRowRejected()
{
    // 実行 : 複数版非対応のqz-appの特定版を指定する
    const bool ok = ZyppManager::instance().setPackageVersionStatus(
        "qz-app", "2.0-1", "noarch", "qz-avail", static_cast<int>(PackageStatus::Install));

    // 確認 : 複数版非対応として拒否される
    QVERIFY(!ok);
    QVERIFY2(ZyppManager::instance().lastError().find("not multiversion-capable") != std::string::npos,
             ZyppManager::instance().lastError().c_str());
}

/**
 * @brief 版単位のインストール・削除が変更予定一覧へ1項目ずつ反映されることを確認する
 * @note フィクスチャが複数版ビットをモデル化できない場合はQSKIPする
 */
void MultiversionSelectionTest::perVersionSelectionAndPendingChanges()
{
    if (!m_multiversionModeled) {
        QSKIP("Fixture could not model multiversion: libzypp sets sat multiversion bits at pool load, "
              "and ZConfig::addMultiversionSpec after initialize() has no effect");
    }

    auto &manager = ZyppManager::instance();

    // 実行 : 利用可能な2版を同時にインストールする
    QVERIFY2(manager.setPackageVersionStatus(
                 "qz-lib", "1.5-1", "noarch", "qz-avail", static_cast<int>(PackageStatus::Install)),
             manager.lastError().c_str());
    QVERIFY2(manager.setPackageVersionStatus(
                 "qz-lib", "1.2-1", "noarch", "qz-alt", static_cast<int>(PackageStatus::Install)),
             manager.lastError().c_str());

    // 確認 : 2版が別々の項目として変更予定一覧に並ぶ
    std::vector<PackageInfo> changes = manager.getPendingChanges();
    QVERIFY2(hasVersion(changes, QStringLiteral("1.5-1")), manager.lastError().c_str());
    QVERIFY(hasVersion(changes, QStringLiteral("1.2-1")));

    // 実行 : インストール済み1.0の削除を要求する
    QVERIFY2(manager.setPackageVersionStatus(
                 "qz-lib", "1.0-1", "noarch", "@System", static_cast<int>(PackageStatus::Del)),
             manager.lastError().c_str());

    // 確認 : 削除項目がinstalledVersion付きで追加される
    changes = manager.getPendingChanges();
    bool foundRemoval = false;

    for (const PackageInfo &entry : changes) {
        if (entry.installedVersion == QStringLiteral("1.0-1")
            && entry.status == static_cast<int>(PackageStatus::Del)) {
            foundRemoval = true;
        }
    }

    QVERIFY(foundRemoval);

    // 実行 : 削除と追加インストールをそれぞれ取り消す
    QVERIFY2(manager.setPackageVersionStatus(
                 "qz-lib", "1.0-1", "noarch", "@System", static_cast<int>(PackageStatus::KeepInstalled)),
             manager.lastError().c_str());
    QVERIFY2(manager.setPackageVersionStatus(
                 "qz-lib", "1.2-1", "noarch", "qz-alt", static_cast<int>(PackageStatus::NoInst)),
             manager.lastError().c_str());

    // 確認 : 取り消した項目が変更予定一覧から消える
    changes = manager.getPendingChanges();
    QVERIFY(!hasVersion(changes, QStringLiteral("1.2-1")));

    for (const PackageInfo &entry : changes) {
        QVERIFY(entry.installedVersion != QStringLiteral("1.0-1"));
    }
}

QTEST_GUILESS_MAIN(MultiversionSelectionTest)
#include "tst_MultiversionSelection.moc"
