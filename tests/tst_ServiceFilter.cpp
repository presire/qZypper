/**
 * @file tst_ServiceFilter.cpp
 * @brief サービス所属リポジトリによる一覧の絞り込みを検証する
 *
 * QTestからZyppManagerを呼び出し、一時プールでリポジトリ別一覧と比較する
 * 入力 : ZYPPTMPDIR
 * 出力 : QTestの合否
 * 依存 : Qt Test、libzypp、SelectionFixture、repo2solv
 */
#include <QTemporaryDir>
#include <QtTest>
#include <algorithm>
#include <cstddef>
#include <zypp/RepoInfo.h>
#include <zypp/sat/Pool.h>

#include "SelectionFixture.h"
#include "ZyppManager.h"

using namespace qZypper;

namespace {
/** @brief パッケージ名を昇順に並べた一覧へ変換する
 * @param packages 比較対象のパッケージ
 * @return 昇順の名前一覧
 */
QStringList sortedNames(const std::vector<PackageInfo> &packages)
{
    QStringList result;
    for (const PackageInfo &info : packages)
        result.append(info.name);
    std::sort(result.begin(), result.end());
    return result;
}

/** @brief 名前が一致するパッケージ情報を検索する
 * @param packages 検索対象の一覧
 * @param name 検索する名前
 * @return 一致する要素への借用ポインタ、存在しない場合はnullptr
 */
const PackageInfo *findByName(const std::vector<PackageInfo> &packages, const QString &name)
{
    const auto found = std::find_if(packages.begin(), packages.end(),
        [&name](const PackageInfo &info) { return info.name == name; });
    return found == packages.end() ? nullptr : &*found;
}
}

/**
 * @brief サービス別のパッケージ取得がサービス所属リポジトリだけを対象にすることを検証する
 *
 * 一時ルートへqz-avail (サービス無し) とqz-alt (サービスqz-svc) を読み込み、
 * rootやネットワークなしでサービスの絞り込みを確かめる
 */
class ServiceFilterTest : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void serviceSelectsOnlyServiceRepositories();
    void rowsMatchRepositoryView();
    void unknownOrEmptyServiceReturnsEmpty();
    void cleanupTestCase();

private:
    QTemporaryDir m_root;        // libzyppの一時ルート
    SelectionFixture m_fixture;  // テスト用リポジトリ
};

/** @brief 一時プールにサービス付きとサービスなしのリポジトリを読み込む
 * @note addRepoSolv後もRepoInfo::service()が保持されることを絞り込みの前提として確認する
 */
void ServiceFilterTest::initTestCase()
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

    zypp::RepoInfo alt;
    alt.setAlias("qz-alt");
    alt.setService("qz-svc");
    zypp::sat::Pool::instance().addRepoSolv(
        zypp::Pathname(m_fixture.altSolv.toStdString()), alt);

    // 前提 : addRepoSolvで読み込んだリポジトリはsetServiceの値を保持する
    QCOMPARE(QString::fromStdString(
                 zypp::sat::Pool::instance().reposFind("qz-alt").info().service()),
             QStringLiteral("qz-svc"));

    QCOMPARE(manager.getPackagesByRepo("qz-avail").size(), std::size_t(6));
    QCOMPARE(manager.getPackagesByRepo("qz-alt").size(), std::size_t(2));
    manager.saveState();
}

/** @brief 他のリポジトリを混ぜずサービス所属のパッケージだけを返すことを検証する */
void ServiceFilterTest::serviceSelectsOnlyServiceRepositories()
{
    auto &manager = ZyppManager::instance();
    const auto byService = manager.getPackagesByService("qz-svc");
    const auto byRepo = manager.getPackagesByRepo("qz-alt");

    QStringList expected{QStringLiteral("qz-app"), QStringLiteral("qz-lib")};
    expected.sort();
    QCOMPARE(sortedNames(byService), expected);
    QCOMPARE(sortedNames(byService), sortedNames(byRepo));

    // サービス無しのリポジトリだけにあるパッケージは対象外になる
    const QStringList names = sortedNames(byService);
    QVERIFY(!names.contains(QStringLiteral("qz-new")));
    QVERIFY(!names.contains(QStringLiteral("qz-extra")));
    QVERIFY(!names.contains(QStringLiteral("qz-app-devel")));
    QVERIFY(!names.contains(QStringLiteral("qz-app-debuginfo")));
}

/** @brief サービス別一覧の各属性がリポジトリ別ビューと一致することを検証する */
void ServiceFilterTest::rowsMatchRepositoryView()
{
    auto &manager = ZyppManager::instance();
    const auto byService = manager.getPackagesByService("qz-svc");
    const auto byRepo = manager.getPackagesByRepo("qz-alt");
    QCOMPARE(byService.size(), byRepo.size());

    for (const PackageInfo &info : byService) {
        const PackageInfo *same = findByName(byRepo, info.name);
        QVERIFY2(same != nullptr, qPrintable(info.name));
        QCOMPARE(info.toVariantMap(), same->toVariantMap());
    }
}

/** @brief 存在しないサービス名と空文字で無関係な行を返さないことを検証する */
void ServiceFilterTest::unknownOrEmptyServiceReturnsEmpty()
{
    auto &manager = ZyppManager::instance();
    QVERIFY(manager.getPackagesByService("qz-missing").empty());
    QVERIFY(manager.getPackagesByService("").empty());
}

/** @brief 一時プールへ追加したqz-altを取り除く */
void ServiceFilterTest::cleanupTestCase()
{
    zypp::sat::Pool::instance().reposErase("qz-alt");
}

QTEST_MAIN(ServiceFilterTest)
#include "tst_ServiceFilter.moc"
