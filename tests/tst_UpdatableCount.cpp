/**
 * @file tst_UpdatableCount.cpp
 * @brief 更新可能件数と分類結果の一致を検証する
 *
 * QTestからZyppManagerを呼び出し、一時プールで保護状態の影響も確認する
 * 入力 : ZYPPTMPDIR
 * 出力 : QTestの合否
 * 依存 : Qt Test、libzypp、SelectionFixture、repo2solv
 */
#include <QTemporaryDir>
#include <QtTest>
#include <cstddef>
#include <zypp/RepoInfo.h>
#include <zypp/sat/Pool.h>

#include "SelectionFixture.h"
#include "Types.h"
#include "ZyppManager.h"

using namespace qZypper;

/**
 * @brief 更新可能件数の専用取得が分類updatableの件数と一致することを検証する
 *
 * 一時ルートへqz-availとシステムリポジトリを読み込み、rootやネットワークなしで動かす
 */
class UpdatableCountTest : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void countMatchesClassification();
    void protectedPackageIsExcluded();
    void cleanup();
    void cleanupTestCase();

private:
    QTemporaryDir m_root;        // libzyppの一時ルート
    SelectionFixture m_fixture;  // テスト用リポジトリ
};

/** @brief 一時ルートを構築し、更新候補を持つ導入済みパッケージを読み込む */
void UpdatableCountTest::initTestCase()
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
    zypp::sat::Pool::instance().addRepoSolv(zypp::Pathname(m_fixture.availSolv.toStdString()),
                                           available);
    zypp::sat::Pool::instance().reposErase(zypp::sat::Pool::systemRepoAlias());
    zypp::sat::Pool::instance().addRepoSolv(zypp::Pathname(m_fixture.systemSolv.toStdString()),
                                           zypp::sat::Pool::systemRepoAlias());
    QCOMPARE(manager.getPackagesByRepo("qz-avail").size(), std::size_t(6));
    manager.saveState();
}

/** @brief 専用取得の件数が分類updatableの件数と一致し、2件以上あることを確認する */
void UpdatableCountTest::countMatchesClassification()
{
    auto &manager = ZyppManager::instance();

    // 準備 : qz-app 1.0→2.0 と qz-lib 1.0→1.5 が更新可能
    const std::size_t expected = manager.getPackagesByClassification("updatable").size();
    QVERIFY(expected >= 2);

    // 確認 : 専用取得は分類と同じ件数を返す
    QCOMPARE(manager.countUpdatablePackages(), expected);
}

/** @brief 更新候補を持つqz-libを保護すると件数が1減ることを確認する */
void UpdatableCountTest::protectedPackageIsExcluded()
{
    auto &manager = ZyppManager::instance();

    // 準備 : 保護前の件数を記録する
    const std::size_t before = manager.countUpdatablePackages();
    QVERIFY(before >= 2);

    // 実行 : 更新候補を持つqz-libを保護状態にする
    QVERIFY2(manager.setPackageStatus("qz-lib", static_cast<int>(PackageStatus::Protected)),
             manager.lastError().c_str());

    // 確認 : Protectedは除外され、分類の件数とも一致する
    QCOMPARE(manager.countUpdatablePackages(), before - 1);
    QCOMPARE(manager.countUpdatablePackages(), manager.getPackagesByClassification("updatable").size());

    // 後始末 : 保存済み状態へ戻す
    manager.restoreState();
}

/** @brief 失敗したテストでも保護状態を残さない */
void UpdatableCountTest::cleanup()
{
    ZyppManager::instance().restoreState();
}

/** @brief 一時プールを解放する */
void UpdatableCountTest::cleanupTestCase()
{
    ZyppManager::instance().shutdown();
}

QTEST_GUILESS_MAIN(UpdatableCountTest)
#include "tst_UpdatableCount.moc"
