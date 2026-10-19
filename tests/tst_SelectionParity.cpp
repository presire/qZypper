#include <QStringList>
#include <QTemporaryDir>
#include <QtTest>
#include <zypp/RepoInfo.h>
#include <zypp/ResPool.h>
#include <zypp/ZConfig.h>
#include <zypp/sat/Pool.h>
#include <zypp/ui/Selectable.h>

#include "SelectionFixture.h"
#include "SearchOptions.h"
#include "SolverSettingsParse.h"
#include "ZyppManager.h"

using namespace qZypper;

namespace {
/**
 * @brief 指定名のパッケージ選択を取得する
 * @param name パッケージ名
 * @return 選択可能オブジェクト (存在しない場合はnullptr)
 */
zypp::ui::Selectable::Ptr selectable(const std::string &name)
{
    return zypp::ui::Selectable::get(zypp::ResKind::package, name);
}

/**
 * @brief 指定名のパッケージの要求状態を返す
 * @param name パッケージ名
 * @return 要求状態 (存在しない場合は-1)
 */
int statusOf(const std::string &name)
{
    const auto sel = selectable(name);
    return sel ? static_cast<int>(sel->status()) : -1;
}

/**
 * @brief 検索結果のパッケージ名を昇順で返す
 * @param packages 検索結果
 * @return パッケージ名の一覧
 */
QStringList namesOf(const std::vector<PackageInfo> &packages)
{
    QStringList names;
    for (const PackageInfo &info : packages)
        names.append(info.name);
    names.sort();
    return names;
}

/**
 * @brief 全パッケージの明示的な候補選択をリポジトリ既定へ戻す
 *
 * setCandidate(空のPoolItem)はユーザが指定した候補を解除し、
 * 利用可能な版から算出される既定候補へ戻す
 */
void resetCandidates()
{
    const zypp::ResPoolProxy proxy = zypp::ResPool::instance().proxy();
    for (auto it = proxy.byKindBegin<zypp::Package>();
         it != proxy.byKindEnd<zypp::Package>(); ++it)
        (*it)->setCandidate(zypp::PoolItem());
}

/**
 * @brief 指定リポジトリの版に対応するPoolItemを返す
 * @param name パッケージ名
 * @param version 版 (editionのversion)
 * @param repoAlias リポジトリエイリアス
 * @return 一致するPoolItem (存在しない場合は空)
 */
zypp::PoolItem poolItemOf(const std::string &name, const std::string &version,
                          const std::string &repoAlias)
{
    const auto sel = selectable(name);
    if (!sel)
        return {};
    for (auto it = sel->availableBegin(); it != sel->availableEnd(); ++it) {
        if (it->edition().version() == version && it->repoInfo().alias() == repoAlias)
            return *it;
    }
    return {};
}

/**
 * @brief テスト中だけ追加リポジトリをプールへ載せるスコープガード
 *
 * デストラクタで必ずリポジトリを取り除くため、途中で検証が失敗しても
 * 後続テストのプール構成は変わらない
 */
struct TempRepoGuard
{
    /**
     * @brief リポジトリをプールへ追加する
     * @param alias リポジトリエイリアス
     * @param solvPath solvファイルのパス
     */
    TempRepoGuard(const std::string &alias, const std::string &solvPath)
        : m_alias(alias)
    {
        zypp::RepoInfo info;
        info.setAlias(m_alias);
        zypp::sat::Pool::instance().addRepoSolv(zypp::Pathname(solvPath), info);
    }

    /** @brief 追加したリポジトリをプールから取り除く */
    ~TempRepoGuard()
    {
        zypp::sat::Pool::instance().reposErase(m_alias);
    }

    std::string m_alias;  // 取り除く対象のエイリアス
};

/**
 * @brief ロケール一覧から指定コードの項目を返す
 * @param locales ロケール一覧
 * @param code ロケールコード
 * @return 一致する項目 (存在しない場合は空)
 */
QVariantMap localeEntry(const QVariantList &locales, const QString &code)
{
    for (const QVariant &value : locales) {
        const QVariantMap entry = value.toMap();
        if (entry.value(QStringLiteral("code")).toString() == code)
            return entry;
    }
    return {};
}
}

/**
 * @brief 一括操作とサブパッケージ選択の挙動を検証する
 */
class SelectionParityTest : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void init();
    void cleanup();
    void batchInstallUpdate();
    void batchRemoveKeep();
    void batchProtectedSkipped();
    void batchUnknownName();
    void batchUpdateSwitchesCandidate();
    void batchUpdateForceSameVersion();
    void subpackagesDevel();
    void subpackagesRejectsSuffix();
    void searchPrefix();
    void searchCaseSensitive();
    void searchRecommends();
    void searchFileList();
    void classificationUpdatable();
    void packageInfoUpdatable();
    void repoLastRefreshField();
    void selectRecommendedInstallsExtra();
    void solverDefaults();
    void solverOverrideApplied();
    void solverOverrideAppliedWithoutResolve();
    void solverSettingsParse();
    void rpmGroupsTree();
    void packagesByGroupPrefix();
    void localeToggle();
    void localeRejectsBadCode();
    void localeOnlyPending();
    void localePendingClearedAfterSave();
    void cleanupTestCase();

private:
    QTemporaryDir m_root;
    SelectionFixture m_fixture;
    zypp::LocaleSet m_initialLocales;  // initTestCase時点の要求ロケール (各テストの基準)
};

/** @brief qz-availと@Systemを持つ一時ルートで初期化し、選択状態を保存する */
void SelectionParityTest::initTestCase()
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

    // @Systemの導入済みパッケージはavailable側に現れないため、リポジトリのsolvableを数える
    const zypp::Repository systemRepo =
        zypp::sat::Pool::instance().reposFind(zypp::sat::Pool::systemRepoAlias());
    std::size_t systemCount = 0;
    for (const auto &solvable : systemRepo.solvables()) {
        if (solvable.isKind<zypp::Package>())
            ++systemCount;
    }
    QCOMPARE(systemCount, std::size_t(3));

    const auto app = selectable("qz-app");
    QVERIFY(app);
    QVERIFY(!app->installedEmpty());
    QVERIFY(app->candidateObj());
    QCOMPARE(QString::fromStdString(app->candidateObj()->edition().version()),
             QStringLiteral("2.0"));

    manager.saveState();
    m_initialLocales = zypp::sat::Pool::instance().getRequestedLocales();
}

/** @brief 各テストの前に保存済みの選択状態・ロケール基準・既定候補へ戻す */
void SelectionParityTest::init()
{
    ZyppManager::instance().restoreState();
    ZyppManager::instance().resetSolverOverrides();
    resetCandidates();
    zypp::sat::Pool::instance().setRequestedLocales(m_initialLocales);
    ZyppManager::instance().saveState();
}

/** @brief 各テストの後に保存済みの選択状態・ロケール基準・既定候補へ戻す */
void SelectionParityTest::cleanup()
{
    ZyppManager::instance().restoreState();
    ZyppManager::instance().resetSolverOverrides();
    resetCandidates();
    zypp::sat::Pool::instance().setRequestedLocales(m_initialLocales);
    ZyppManager::instance().saveState();
}

/** @brief installを一括適用すると未導入はInstall、更新可能はUpdate、それ以外はskippedになる */
void SelectionParityTest::batchInstallUpdate()
{
    // 準備 : qz-new (未導入) / qz-lib (更新可能) / qz-old (導入済み・候補なし)
    auto &manager = ZyppManager::instance();

    // 実行 : 重複したqz-newを含む一括install
    const auto result = manager.setPackagesAction(
        {"qz-new", "qz-lib", "qz-old", "qz-new"}, "install");

    // 確認 : qz-oldは更新候補がないためskipped
    QCOMPARE(result.changed, 2);
    QCOMPARE(result.skipped, 1);
    QCOMPARE(result.failedCount, 0);
    QVERIFY(result.failed.empty());
    QCOMPARE(statusOf("qz-new"), static_cast<int>(zypp::ui::S_Install));
    QCOMPARE(statusOf("qz-lib"), static_cast<int>(zypp::ui::S_Update));
    QCOMPARE(statusOf("qz-old"), static_cast<int>(zypp::ui::S_KeepInstalled));

    const auto lib = selectable("qz-lib");
    QVERIFY(lib && lib->candidateObj());
    QCOMPARE(QString::fromStdString(lib->candidateObj()->edition().version()),
             QStringLiteral("1.5"));
}

/** @brief removeしてからkeepすると元の導入状態へ戻る */
void SelectionParityTest::batchRemoveKeep()
{
    // 準備 : qz-libは導入済み
    auto &manager = ZyppManager::instance();

    // 実行 : remove
    const auto removed = manager.setPackagesAction({"qz-lib"}, "remove");

    // 確認 : 削除マーク
    QCOMPARE(removed.changed, 1);
    QCOMPARE(removed.failedCount, 0);
    QCOMPARE(statusOf("qz-lib"), static_cast<int>(zypp::ui::S_Del));

    // 実行 : keep
    const auto kept = manager.setPackagesAction({"qz-lib"}, "keep");

    // 確認 : 導入維持へ戻る
    QCOMPARE(kept.changed, 1);
    QCOMPARE(kept.failedCount, 0);
    QCOMPARE(statusOf("qz-lib"), static_cast<int>(zypp::ui::S_KeepInstalled));
}

/** @brief Protectedのパッケージはkeep以外の一括操作で変更されない */
void SelectionParityTest::batchProtectedSkipped()
{
    // 準備 : qz-libをProtectedにする
    auto &manager = ZyppManager::instance();
    QCOMPARE(manager.setPackagesAction({"qz-lib"}, "protect").changed, 1);
    QCOMPARE(statusOf("qz-lib"), static_cast<int>(zypp::ui::S_Protected));

    // 実行 : remove
    const auto removed = manager.setPackagesAction({"qz-lib"}, "remove");

    // 確認 : skippedとなり状態は変わらない
    QCOMPARE(removed.changed, 0);
    QCOMPARE(removed.skipped, 1);
    QCOMPARE(removed.failedCount, 0);
    QCOMPARE(statusOf("qz-lib"), static_cast<int>(zypp::ui::S_Protected));

    // 実行 : keep
    const auto kept = manager.setPackagesAction({"qz-lib"}, "keep");

    // 確認 : Protectedから復帰できる
    QCOMPARE(kept.changed, 1);
    QCOMPARE(statusOf("qz-lib"), static_cast<int>(zypp::ui::S_KeepInstalled));
}

/** @brief 存在しないパッケージはfailedへ記録され、一覧は100件で打ち切られる */
void SelectionParityTest::batchUnknownName()
{
    // 準備 : 未知の名前1件
    auto &manager = ZyppManager::instance();

    // 実行 :
    const auto result = manager.setPackagesAction({"qz-missing"}, "install");

    // 確認 :
    QCOMPARE(result.changed, 0);
    QCOMPARE(result.skipped, 0);
    QCOMPARE(result.failedCount, 1);
    QCOMPARE(result.failed.size(), std::size_t(1));
    QCOMPARE(QString::fromStdString(result.failed.front()), QStringLiteral("qz-missing"));

    // 準備 : 未知の名前150件
    std::vector<std::string> missing;
    for (int index = 0; index < 150; ++index)
        missing.push_back("qz-missing-" + std::to_string(index));

    // 実行 :
    const auto many = manager.setPackagesAction(missing, "install");

    // 確認 : failedCountは総数、failedは100件まで
    QCOMPARE(many.failedCount, 150);
    QCOMPARE(many.failed.size(), std::size_t(100));
}

/** @brief 明示的に選んだ旧版候補でもupdateで既定の最新候補へ切り替わる */
void SelectionParityTest::batchUpdateSwitchesCandidate()
{
    // 準備 : 既定では読み込まないqz-altだけを追加し、qz-libの候補を1.2へ固定する
    TempRepoGuard altRepo("qz-alt", m_fixture.altSolv.toStdString());
    const auto lib = selectable("qz-lib");
    QVERIFY(lib);
    const zypp::PoolItem altLib = poolItemOf("qz-lib", "1.2", "qz-alt");
    QVERIFY(altLib);
    lib->setCandidate(altLib);
    QVERIFY(lib->candidateObj());
    QCOMPARE(QString::fromStdString(lib->candidateObj()->edition().version()),
             QStringLiteral("1.2"));

    auto &manager = ZyppManager::instance();

    // 実行 : 候補を変えたままupdateを一括適用する
    const auto result = manager.setPackagesAction({"qz-lib"}, "update");

    // 確認 : 既定の最新候補1.5へ切り替わり、更新マークが付く
    QCOMPARE(result.changed, 1);
    QCOMPARE(result.skipped, 0);
    QCOMPARE(result.failedCount, 0);
    QCOMPARE(statusOf("qz-lib"), static_cast<int>(zypp::ui::S_Update));
    QVERIFY(lib->candidateObj());
    QCOMPARE(QString::fromStdString(lib->candidateObj()->edition().version()),
             QStringLiteral("1.5"));
}

/** @brief updateForceは同一版でも再インストールとして扱い、繰り返しはskippedになる */
void SelectionParityTest::batchUpdateForceSameVersion()
{
    // 準備 : qz-altのqz-app 1.0 (導入済みと同一版) を明示候補にする
    TempRepoGuard altRepo("qz-alt", m_fixture.altSolv.toStdString());
    const auto app = selectable("qz-app");
    QVERIFY(app);
    const zypp::PoolItem altApp = poolItemOf("qz-app", "1.0", "qz-alt");
    QVERIFY(altApp);
    app->setCandidate(altApp);

    auto &manager = ZyppManager::instance();

    // 実行 : updateForceを一括適用する
    const auto first = manager.setPackagesAction({"qz-app"}, "updateForce");

    // 確認 : 同一版でも再インストールの更新マークが付く
    QCOMPARE(first.changed, 1);
    QCOMPARE(first.skipped, 0);
    QCOMPARE(first.failedCount, 0);
    QCOMPARE(statusOf("qz-app"), static_cast<int>(zypp::ui::S_Update));

    // 実行 : 同じ操作をもう一度適用する
    const auto second = manager.setPackagesAction({"qz-app"}, "updateForce");

    // 確認 : 既に更新マーク済みのためskipped
    QCOMPARE(second.changed, 0);
    QCOMPARE(second.skipped, 1);
    QCOMPARE(second.failedCount, 0);
}

/** @brief -devel選択でqz-app-develだけがInstallになり、-debuginfoは変わらない */
void SelectionParityTest::subpackagesDevel()
{
    // 準備 : qz-appのみ対応する-develを持つ
    auto &manager = ZyppManager::instance();

    // 実行 :
    const auto result = manager.selectSubpackages("-devel");

    // 確認 :
    QCOMPARE(result.changed, 1);
    QCOMPARE(result.failedCount, 0);
    QCOMPARE(statusOf("qz-app-devel"), static_cast<int>(zypp::ui::S_Install));
    QCOMPARE(statusOf("qz-app-debuginfo"), static_cast<int>(zypp::ui::S_NoInst));
}

/** @brief 許可されていない接尾辞は空結果とエラーになる */
void SelectionParityTest::subpackagesRejectsSuffix()
{
    // 準備 :
    auto &manager = ZyppManager::instance();

    // 実行 :
    const auto result = manager.selectSubpackages("-foo");

    // 確認 :
    QCOMPARE(result.changed, 0);
    QCOMPARE(result.skipped, 0);
    QCOMPARE(result.failedCount, 0);
    QVERIFY(!manager.lastError().empty());
}

/** @brief 前方一致検索は名前の先頭だけに一致し、メタ文字をリテラルとして扱う */
void SelectionParityTest::searchPrefix()
{
    // 準備 : 名前の前方一致でqz-aを指定する
    auto &manager = ZyppManager::instance();
    SearchOptions options;
    options.query = QStringLiteral("qz-a");
    options.flags = static_cast<int>(SearchFlag::Name);
    options.match = SearchMatchMode::Prefix;

    // 実行 :
    const auto result = manager.searchPackagesEx(options);

    // 確認 : qz-aで始まる3件だけが返り、qz-newは含まれない
    const QStringList expected{QStringLiteral("qz-app"), QStringLiteral("qz-app-debuginfo"),
                               QStringLiteral("qz-app-devel")};
    QCOMPARE(namesOf(result), expected);

    // 実行 : 正規表現では任意文字になる"."を含む前方一致を指定する
    options.query = QStringLiteral("qz-a.");
    const auto dotResult = manager.searchPackagesEx(options);

    // 確認 : メタ文字はリテラル扱いのため1件も一致しない
    QVERIFY(dotResult.empty());

    // 実行 : 正規表現では直前文字の繰り返しになる"+"を含む前方一致を指定する
    options.query = QStringLiteral("qz-a+");
    const auto plusResult = manager.searchPackagesEx(options);

    // 確認 : エスケープされ例外にならず、qz-appにも一致しない
    QVERIFY(plusResult.empty());
    const QString lastError = QString::fromStdString(manager.lastError());
    QVERIFY(!lastError.contains(QStringLiteral("Invalid search regex")));
    QVERIFY(!lastError.contains(QStringLiteral("Search error")));

    // 準備 : 部分一致で名前の一部"app"を検索する
    options.query = QStringLiteral("app");
    options.match = SearchMatchMode::Substring;
    const auto substringResult = manager.searchPackagesEx(options);

    // 確認 : 名前に"app"を含むqz-app系だけが返る
    const QStringList appNames{QStringLiteral("qz-app"), QStringLiteral("qz-app-debuginfo"),
                               QStringLiteral("qz-app-devel")};
    QCOMPARE(namesOf(substringResult), appNames);

    // 実行 : 同じ問い合わせを前方一致で行う
    options.match = SearchMatchMode::Prefix;
    const auto prefixResult = manager.searchPackagesEx(options);

    // 確認 : 名前が"app"で始まるパッケージは無く、アンカーが効いている
    QVERIFY(prefixResult.empty());
}

/** @brief 概要検索で大文字小文字の区別を切り替えられる */
void SelectionParityTest::searchCaseSensitive()
{
    // 準備 : qz-newの概要"QZ New Tool"を小文字だけで検索する
    auto &manager = ZyppManager::instance();
    SearchOptions options;
    options.query = QStringLiteral("qz new");
    options.flags = static_cast<int>(SearchFlag::Summary);
    options.caseSensitive = true;

    // 実行 :
    const auto sensitive = manager.searchPackagesEx(options);

    // 確認 : 区別する場合は一致しない
    QVERIFY(sensitive.empty());

    // 実行 : 大文字小文字を区別しない設定で再検索する
    options.caseSensitive = false;
    const auto insensitive = manager.searchPackagesEx(options);

    // 確認 : 区別しない場合はqz-newが一致する
    QCOMPARE(namesOf(insensitive), QStringList{QStringLiteral("qz-new")});
}

/** @brief 推奨ケーパビリティを対象にした検索が一致する */
void SelectionParityTest::searchRecommends()
{
    // 準備 : qz-appの推奨にqz-extraがある
    auto &manager = ZyppManager::instance();
    SearchOptions options;
    options.query = QStringLiteral("qz-extra");
    options.flags = static_cast<int>(SearchFlag::Recommends);

    // 実行 :
    const auto result = manager.searchPackagesEx(options);

    // 確認 : 推奨元のqz-appだけが返る
    QCOMPARE(namesOf(result), QStringList{QStringLiteral("qz-app")});
}

/** @brief インストールファイル一覧を対象にした検索が一致する */
void SelectionParityTest::searchFileList()
{
    // 準備 : qz-appのファイル一覧に/usr/bin/qz-appがある
    auto &manager = ZyppManager::instance();
    SearchOptions options;
    options.query = QStringLiteral("/usr/bin/qz-app");
    options.flags = static_cast<int>(SearchFlag::FileList);

    // 実行 :
    const auto result = manager.searchPackagesEx(options);

    // 確認 : ファイルを所有するqz-appが返る
    QCOMPARE(namesOf(result), QStringList{QStringLiteral("qz-app")});

    // 実行 : スラッシュを含まない完全一致は従来どおりベース名で照合する
    options.query = QStringLiteral("qz-app");
    options.match = SearchMatchMode::Exact;
    const auto basenameResult = manager.searchPackagesEx(options);

    // 確認 : ベース名が一致するqz-appが返る
    QCOMPARE(namesOf(basenameResult), QStringList{QStringLiteral("qz-app")});
}

/** @brief 推奨パッケージを展開すると未導入のqz-extraがAutoInstallになる */
void SelectionParityTest::selectRecommendedInstallsExtra()
{
    // 準備 : qz-appは導入維持のまま
    auto &manager = ZyppManager::instance();
    QCOMPARE(statusOf("qz-app"), static_cast<int>(zypp::ui::S_KeepInstalled));
    QCOMPARE(statusOf("qz-extra"), static_cast<int>(zypp::ui::S_NoInst));

    // 実行 :
    const auto result = manager.selectAllRecommended();

    // 確認 :
    QVERIFY2(result.success, manager.lastError().c_str());
    QVERIFY(result.problems.empty());
    QCOMPARE(statusOf("qz-extra"), static_cast<int>(zypp::ui::S_AutoInstall));
    QCOMPARE(statusOf("qz-app"), static_cast<int>(zypp::ui::S_KeepInstalled));
}

/**
 * @brief ソルバー設定の既定値がzypp.confと一致することを検証する
 */
void SelectionParityTest::solverDefaults()
{
    // 実行 :
    const QVariantMap settings = ZyppManager::instance().getSolverSettings();

    // 確認 : 有効値と既定値の両方が3項目揃い、上書き前は両者が一致する
    const QVariantMap defaults = settings.value(QStringLiteral("defaults")).toMap();
    const zypp::ZConfig &config = zypp::ZConfig::instance();
    QCOMPARE(defaults.size(), 3);
    QCOMPARE(defaults.value(QStringLiteral("installRecommends")).toBool(),
             !config.solver_onlyRequires());
    QCOMPARE(defaults.value(QStringLiteral("cleandepsOnRemove")).toBool(),
             config.solver_cleandepsOnRemove());
    QCOMPARE(defaults.value(QStringLiteral("allowVendorChange")).toBool(),
             config.solver_allowVendorChange());
    QCOMPARE(settings.value(QStringLiteral("installRecommends")).toBool(),
             defaults.value(QStringLiteral("installRecommends")).toBool());
    QCOMPARE(settings.value(QStringLiteral("cleandepsOnRemove")).toBool(),
             defaults.value(QStringLiteral("cleandepsOnRemove")).toBool());
    QCOMPARE(settings.value(QStringLiteral("allowVendorChange")).toBool(),
             defaults.value(QStringLiteral("allowVendorChange")).toBool());
}

/**
 * @brief 推奨導入の上書きがリゾルバへ反映され、解除で既定へ戻ることを検証する
 */
void SelectionParityTest::solverOverrideApplied()
{
    // 準備 : zypp.conf由来の推奨導入の既定値を取得する
    auto &manager = ZyppManager::instance();
    const QVariantMap defaults = manager.getSolverSettings().value(QStringLiteral("defaults")).toMap();
    const bool defaultInstallRecommends =
        defaults.value(QStringLiteral("installRecommends")).toBool();

    // 実行 : 既定値の逆を上書きして解決する
    ZyppManager::SolverOverrides patch;
    patch.installRecommends = !defaultInstallRecommends;
    QVERIFY(manager.setSolverSettings(patch));
    QCOMPARE(manager.getSolverSettings().value(QStringLiteral("installRecommends")).toBool(),
             !defaultInstallRecommends);

    const auto result = manager.resolveDependencies();

    // 確認 : 上書きに応じてonlyRequiresで解決している
    QVERIFY2(result.success, manager.lastError().c_str());
    QCOMPARE(zypp::getZYpp()->resolver()->onlyRequires(), defaultInstallRecommends);

    // 実行 : 上書きを解除して再度解決する
    manager.resetSolverOverrides();
    const auto resetResult = manager.resolveDependencies();

    // 確認 : 既定値へ戻り、解決にも既定値が使われる
    QVERIFY2(resetResult.success, manager.lastError().c_str());
    QCOMPARE(manager.getSolverSettings().value(QStringLiteral("installRecommends")).toBool(),
             defaultInstallRecommends);
    QCOMPARE(zypp::getZYpp()->resolver()->onlyRequires(), !defaultInstallRecommends);

    // 実行・確認 : true/falseを順に明示設定し、都度解決へ反映される
    for (const bool installRecommends : {true, false}) {
        ZyppManager::SolverOverrides explicitPatch;
        explicitPatch.installRecommends = installRecommends;
        QVERIFY(manager.setSolverSettings(explicitPatch));

        const auto explicitResult = manager.resolveDependencies();
        QVERIFY2(explicitResult.success, manager.lastError().c_str());
        QCOMPARE(zypp::getZYpp()->resolver()->onlyRequires(), !installRecommends);
    }
}

/**
 * @brief 解決を挟まずに上書きがリゾルバへ反映されることを検証する
 */
void SelectionParityTest::solverOverrideAppliedWithoutResolve()
{
    // 準備 : zypp.conf由来の推奨導入の既定値を取得する
    auto &manager = ZyppManager::instance();
    const QVariantMap defaults = manager.getSolverSettings().value(QStringLiteral("defaults")).toMap();
    const bool defaultInstallRecommends =
        defaults.value(QStringLiteral("installRecommends")).toBool();

    // 実行 : 既定値の逆を上書きする (resolveDependenciesは呼ばない)
    ZyppManager::SolverOverrides patch;
    patch.installRecommends = !defaultInstallRecommends;
    QVERIFY(manager.setSolverSettings(patch));

    // 確認 : 分類取得などが使うリゾルバ設定が即座に更新されている
    QCOMPARE(zypp::getZYpp()->resolver()->onlyRequires(), defaultInstallRecommends);

    // 準備 : 推奨導入を無効化した上書きで全推奨を選択する
    ZyppManager::SolverOverrides noRecommends;
    noRecommends.installRecommends = false;
    QVERIFY(manager.setSolverSettings(noRecommends));
    const auto result = manager.selectAllRecommended();
    QVERIFY2(result.success, manager.lastError().c_str());

    // 確認 : 選択後にソルバー設定が上書き状態へ復元される
    QCOMPARE(zypp::getZYpp()->resolver()->onlyRequires(), true);
    QCOMPARE(zypp::getZYpp()->resolver()->ignoreAlreadyRecommended(), true);
}

/**
 * @brief ソルバー設定マップの厳格な解析を検証する
 */
void SelectionParityTest::solverSettingsParse()
{
    // 準備 :
    ZyppManager::SolverOverrides parsed;
    QString error;

    // 実行 : 既知のキーと真偽値だけを持つマップ
    const bool accepted = parseSolverSettings(
        QVariantMap{{QStringLiteral("installRecommends"), false},
                    {QStringLiteral("allowVendorChange"), true}},
        parsed, error);

    // 確認 : 指定した項目だけが設定される
    QVERIFY2(accepted, qPrintable(error));
    QVERIFY(parsed.installRecommends.has_value());
    QCOMPARE(parsed.installRecommends.value(), false);
    QVERIFY(!parsed.cleandepsOnRemove.has_value());
    QVERIFY(parsed.allowVendorChange.has_value());
    QCOMPARE(parsed.allowVendorChange.value(), true);

    // 準備 : 失敗時に出力が変更されないことを確認するための空の格納先
    ZyppManager::SolverOverrides rejected;

    // 実行と確認 : 未知のキーは拒否する
    error.clear();
    QVERIFY(!parseSolverSettings(QVariantMap{{QStringLiteral("foo"), true}}, rejected, error));
    QVERIFY(!error.isEmpty());
    QVERIFY(!rejected.installRecommends.has_value());

    // 実行と確認 : 真偽値以外は拒否する
    error.clear();
    QVERIFY(!parseSolverSettings(
        QVariantMap{{QStringLiteral("installRecommends"), QStringLiteral("yes")}}, rejected, error));
    QVERIFY(!error.isEmpty());

    // 実行と確認 : 空のマップは拒否する
    error.clear();
    QVERIFY(!parseSolverSettings(QVariantMap{}, rejected, error));
    QVERIFY(!error.isEmpty());
}

/**
 * @brief 更新可能分類が導入済みかつ更新候補ありのパッケージだけを返すことを検証する
 */
void SelectionParityTest::classificationUpdatable()
{
    // 準備 : qz-app/qz-libは導入済みで更新候補あり、qz-oldは候補なし、qz-newは未導入
    auto &manager = ZyppManager::instance();

    // 実行 : 更新可能分類を取得する
    const QStringList updatable = namesOf(manager.getPackagesByClassification("updatable"));

    // 確認 : 更新候補を持つ導入済みパッケージだけが昇順で返る
    QCOMPARE(updatable, QStringList({QStringLiteral("qz-app"), QStringLiteral("qz-lib")}));

    // 準備 : 更新候補を持つqz-libを保護状態にする
    QVERIFY2(manager.setPackageStatus("qz-lib", static_cast<int>(PackageStatus::Protected)),
             manager.lastError().c_str());

    // 実行 : 保護状態の変更後にもう一度取得する
    const QStringList afterProtected =
        namesOf(manager.getPackagesByClassification("updatable"));

    // 確認 : 保護されたqz-libは更新可能から外れる
    QCOMPARE(afterProtected, QStringList({QStringLiteral("qz-app")}));
}

/**
 * @brief PackageInfoのupdatableが候補の版の文字列ではなくlibzyppの更新候補で決まることを検証する
 */
void SelectionParityTest::packageInfoUpdatable()
{
    auto &manager = ZyppManager::instance();

    // 実行と確認 : 導入済み1.0に対して1.5があるqz-libは更新可能
    QVERIFY(manager.getPackageDetails("qz-lib").basic.updatable);

    // 実行と確認 : 候補の無い導入済みqz-oldと、未導入のqz-newは更新不可
    QVERIFY(!manager.getPackageDetails("qz-old").basic.updatable);
    QVERIFY(!manager.getPackageDetails("qz-new").basic.updatable);

    // 準備 : qz-altのqz-app 1.0 (導入済みと同一版) を明示候補にする
    TempRepoGuard altRepo("qz-alt", m_fixture.altSolv.toStdString());
    const auto app = selectable("qz-app");
    QVERIFY(app);
    const zypp::PoolItem altApp = poolItemOf("qz-app", "1.0", "qz-alt");
    QVERIFY(altApp);
    app->setCandidate(altApp);

    // 実行 : 候補の版が導入済みと同じ状態でパッケージ情報を生成する
    const PackageInfo info = manager.getPackageDetails("qz-app").basic;

    // 確認 : 候補は導入済みと同じ版でも、より新しい2.0があるため更新可能
    QCOMPARE(info.version, info.installedVersion);
    QVERIFY(info.updatable);

    // 確認 : D-Bus転送用マップを往復してもupdatableが保持される
    QVERIFY(PackageInfo::fromVariantMap(info.toVariantMap()).updatable);
}

/** @brief lastRefreshがD-Bus転送用マップを往復しても保持されることを検証する */
void SelectionParityTest::repoLastRefreshField()
{
    // 準備 : 最終更新日時を持つリポジトリ情報
    RepoInfo info;
    info.alias = QStringLiteral("qz-avail");
    info.lastRefresh = 1234567890;

    // 実行 : マップへ変換して復元する
    const RepoInfo restored = RepoInfo::fromVariantMap(info.toVariantMap());

    // 確認 : 最終更新日時がそのまま復元される
    QCOMPARE(restored.lastRefresh, qint64(1234567890));
}

/** @brief RPMグループのツリーが件数と階層付きで返ることを検証する */
void SelectionParityTest::rpmGroupsTree()
{
    // 準備 : 初期化済みのマネージャ
    auto &manager = ZyppManager::instance();

    // 実行 : グループツリーを取得する
    const QVariantList groups = manager.getRpmGroups();

    // 確認 : 件数、階層、未設定ノードの位置を検証する
    QVERIFY(!groups.isEmpty());
    int productivityCount = -1;
    int productivityDepth = -1;
    int editorsDepth = -1;
    QString lastPath;
    for (const QVariant &value : groups) {
        const QVariantMap node = value.toMap();
        const QString path = node.value(QStringLiteral("path")).toString();
        if (path == QStringLiteral("Productivity"))
            productivityCount = node.value(QStringLiteral("count")).toInt();
        if (path == QStringLiteral("Productivity"))
            productivityDepth = node.value(QStringLiteral("depth")).toInt();
        if (path == QStringLiteral("Productivity/Text/Editors"))
            editorsDepth = node.value(QStringLiteral("depth")).toInt();
        lastPath = path;
    }
    QVERIFY(productivityCount >= 3);
    QCOMPARE(productivityDepth, 0);
    QCOMPARE(editorsDepth, 2);
    QCOMPARE(lastPath, QStringLiteral("Unspecified"));
}

/** @brief グループパスの前方一致で配下のパッケージが返ることを検証する */
void SelectionParityTest::packagesByGroupPrefix()
{
    // 準備 : 初期化済みのマネージャ
    auto &manager = ZyppManager::instance();

    // 実行 : 中間階層のグループを指定する
    const QStringList names = namesOf(manager.getPackagesByRpmGroup("Productivity/Text"));

    // 確認 : 子グループのパッケージも含まれる
    QVERIFY(names.contains(QStringLiteral("qz-new")));
    QVERIFY(names.contains(QStringLiteral("qz-app")));
    QCOMPARE(names, QStringList({QStringLiteral("qz-app"), QStringLiteral("qz-lib"),
                                 QStringLiteral("qz-new")}));

    // 実行 : グループ未指定の節点を指定する
    const QStringList unspecified = namesOf(manager.getPackagesByRpmGroup("Unspecified"));

    // 確認 : グループを持たないパッケージだけが返る
    QCOMPARE(unspecified, QStringList({QStringLiteral("qz-extra")}));
}

/** @brief ロケール要求の追加と復元を検証する */
void SelectionParityTest::localeToggle()
{
    // 準備 : jaを外した状態を保存済みの基準にする
    auto &manager = ZyppManager::instance();
    QVERIFY(manager.setLocaleRequested("ja", false));
    manager.saveState();
    QVERIFY(!localeEntry(manager.getLocales(), QStringLiteral("ja"))
                 .value(QStringLiteral("requested")).toBool());

    // 実行 : jaを要求する
    QVERIFY(manager.setLocaleRequested("ja", true));

    // 確認 : 要求済みとして返る
    QCOMPARE(localeEntry(manager.getLocales(), QStringLiteral("ja"))
                 .value(QStringLiteral("requested")).toBool(), true);

    // 実行 : 保存済み状態へ戻す
    manager.restoreState();

    // 確認 : jaの要求が取り消される
    QVERIFY(!localeEntry(manager.getLocales(), QStringLiteral("ja"))
                 .value(QStringLiteral("requested")).toBool());
}

/** @brief 不正なロケールコードが拒否されることを検証する */
void SelectionParityTest::localeRejectsBadCode()
{
    // 準備 : 初期化済みのマネージャと拒否対象のコード
    auto &manager = ZyppManager::instance();
    const std::vector<std::string> badCodes = {
        "../x", "", "ja_JP.UTF-8", std::string(300, 'a')
    };

    // 実行・確認 : すべて拒否され、理由が記録される
    for (const std::string &code : badCodes) {
        QVERIFY(!manager.setLocaleRequested(code, true));
        QVERIFY(!manager.lastError().empty());
    }

    // 確認 : 不正なコードは一覧に現れない
    for (const QVariant &value : manager.getLocales()) {
        QVERIFY(value.toMap().value(QStringLiteral("code")).toString()
                != QStringLiteral("../x"));
    }
}

/** @brief ロケール変更だけが未反映差分として返ることを検証する */
void SelectionParityTest::localeOnlyPending()
{
    // 準備 : jaを外した状態を保存済みの基準にする
    auto &manager = ZyppManager::instance();
    QVERIFY(manager.setLocaleRequested("ja", false));
    manager.saveState();

    // 実行 : jaを要求する
    QVERIFY(manager.setLocaleRequested("ja", true));

    // 確認 : ロケール差分だけが1件返る
    const QVariantList localeChanges = manager.getPendingLocaleChanges();
    QCOMPARE(localeChanges.size(), 1);
    const QVariantMap change = localeChanges.first().toMap();
    QCOMPARE(change.value(QStringLiteral("code")).toString(), QStringLiteral("ja"));
    QCOMPARE(change.value(QStringLiteral("action")).toString(), QStringLiteral("add"));

    // 確認 : パッケージ側の変更予定は空のまま
    QCOMPARE(manager.getPendingChanges().size(), std::size_t(0));
}

/** @brief 保存後のロケール差分が空になることを検証する */
void SelectionParityTest::localePendingClearedAfterSave()
{
    // 準備 : jaを外した状態を保存済みの基準にする
    auto &manager = ZyppManager::instance();
    QVERIFY(manager.setLocaleRequested("ja", false));
    manager.saveState();

    // 実行 : jaを要求してから保存する
    QVERIFY(manager.setLocaleRequested("ja", true));
    QVERIFY(!manager.getPendingLocaleChanges().isEmpty());
    manager.saveState();

    // 確認 : 差分が解消される
    QVERIFY(manager.getPendingLocaleChanges().isEmpty());
}

/** @brief zyppの状態を解放する */
void SelectionParityTest::cleanupTestCase()
{
    ZyppManager::instance().shutdown();
}

QTEST_GUILESS_MAIN(SelectionParityTest)
#include "tst_SelectionParity.moc"
