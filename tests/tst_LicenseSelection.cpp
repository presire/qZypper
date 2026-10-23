/**
 * @file tst_LicenseSelection.cpp
 * @brief libzypp候補のライセンス承認と拒否を統合して検証する
 *
 * Qt Testから実行され、solvリポジトリへEULAを注入してZyppManagerの確認フローを検証する
 *
 * 入力 : SelectionFixtureのsolv候補とテスト用ライセンス本文
 * 出力 : Qt Testの成功または失敗
 * 依存 : Qt Test、libsolv、libzypp、ZyppManager
 */
#include <QtTest>
#include <QTemporaryDir>
#include <cstdio>
#include <memory>
#include <solv/pool.h>
#include <solv/repo.h>
#include <solv/repo_solv.h>
#include <solv/repo_write.h>
#include <solv/repodata.h>
#include <zypp/RepoInfo.h>
#include <zypp/sat/Pool.h>
#include "SelectionFixture.h"
#include "ZyppManager.h"

using namespace qZypper;

namespace {
/**
 * @struct FileCloser
 * @brief std::unique_ptrでFILEを閉じるための削除子
 *
 * fcloseの関数ポインタをテンプレート引数に使うと、glibcの属性付き宣言により
 * -Wignored-attributes警告になるため、関数オブジェクトで包む
 */
struct FileCloser {
    /**
     * @brief ファイルを閉じる
     * @param file 閉じるファイル(nullptrの場合は何もしない)
     */
    void operator()(FILE *file) const
    {
        // 開けなかったファイルはnullptrのため閉じない
        if (file) {
            fclose(file);
        }
    }
};

/**
 * @brief solvリポジトリの全候補にライセンス本文を設定して書き出す
 * @param source 元となるsolvファイル
 * @param destination ライセンスを設定したsolvファイルの出力先
 * @param text 設定するEULA本文
 * @return 書き出しに成功した場合はtrue
 */
bool addLicenses(const QString &source, const QString &destination, const QString &text)
{
    std::unique_ptr<::Pool, decltype(&pool_free)> pool(pool_create(), pool_free);
    // プールが作成できない環境では不完全なsolvを生成しない
    if (!pool) {
        return false;
    }

    ::Repo *repo = repo_create(pool.get(), "licenses");
    std::unique_ptr<FILE, FileCloser> input(fopen(QFile::encodeName(source).constData(), "rb"));
    // 元solvを読めなければ、後続の候補に誤ったライセンスを設定しない
    if (!input || repo_add_solv(repo, input.get(), 0) != 0) {
        return false;
    }

    ::Repodata *data = repo_add_repodata(repo, 0);
    for (Id id = repo->start; id < repo->end; ++id) {
        repodata_set_str(data, id, SOLVABLE_EULA, text.toUtf8().constData());
    }

    repo_internalize(repo);
    std::unique_ptr<FILE, FileCloser> output(fopen(QFile::encodeName(destination).constData(), "wb"));
    return output && repo_write(repo, output.get()) == 0;
}

/**
 * @brief パッケージ名に対応するlibzyppのSelectableを取得する
 * @param name 取得するパッケージ名
 * @return 存在する場合はそのSelectable
 */
zypp::ui::Selectable::Ptr package(const std::string &name)
{
    return zypp::ui::Selectable::get(zypp::ResKind::package, name);
}
}

/**
 * @class LicenseSelectionTest
 * @brief 実際のプール候補にEULAを設定し、承認と拒否の選択への影響を検証する
 *
 * 一時ルートのsolv候補をZyppManagerに読み込み、ライセンスの承認状態と選択変更を検証する
 */
class LicenseSelectionTest : public QObject
{
    Q_OBJECT
private slots:
    /** @brief 読み取り専用のテスト環境とlibzypp候補プールを初期化する */
    void initTestCase()
    {
        qputenv("ZYPP_READONLY_HACK", "1");
        QVERIFY(m_root.isValid());
        if (!m_fixture.build(m_root.path())) {
            if (!m_fixture.skipReason.isEmpty()) {
                QSKIP(qPrintable(m_fixture.skipReason));
            }

            QVERIFY2(false, qPrintable(m_fixture.error));
        }
        auto &manager = ZyppManager::instance();
        QVERIFY2(manager.initialize(m_root.path().toStdString(), true), manager.lastError().c_str());
        zypp::sat::Pool::instance().reposErase(zypp::sat::Pool::systemRepoAlias());
        zypp::sat::Pool::instance().addRepoSolv(zypp::Pathname(m_fixture.systemSolv.toStdString()),
                                              zypp::sat::Pool::systemRepoAlias());
    }

    /** @brief 各テストの候補状態とライセンス付きリポジトリを初期状態へ戻す */
    void init()
    {
        auto &manager = ZyppManager::instance();
        manager.operationState().cancelFlag().store(false);
        for (const auto &item : zypp::ResPool::instance()) {
            item.status().resetTransact(zypp::ResStatus::USER);
        }
        zypp::sat::Pool::instance().reposErase("qz-avail");
        ++m_generation;
        QVERIFY(loadLicensedRepo(QStringLiteral("Test terms %1").arg(m_generation)));
        manager.saveState();
    }

    /** @brief 未確認ライセンスでは対話も候補変更もせずコミットを拒否することを検証する */
    void commitFailsClosedWithoutPromptOrMutation()
    {
        auto &manager = ZyppManager::instance();
        const auto sel = package("qz-new");
        QVERIFY(sel && sel->candidateObj());
        QVERIFY(sel->candidateObj().satSolvable().needToAcceptLicense());
        QVERIFY(sel->setStatus(zypp::ui::S_Install));
        const auto before = sel->candidateObj().status();
        int promptCount = 0;
        const auto result = manager.commit({}, {}, [&promptCount](const PromptRequest &) {
            ++promptCount;
            return QVariantMap{{QStringLiteral("action"), QStringLiteral("accept")}};
        });
        QVERIFY(!result.success);
        QVERIFY(QString::fromStdString(result.errorMessage).contains(QStringLiteral("not confirmed")));
        QCOMPARE(promptCount, 0);
        QCOMPARE(sel->status(), zypp::ui::S_Install);
        QCOMPARE(sel->candidateObj().status(), before);
        QVERIFY(!sel->candidateObj().status().isLicenceConfirmed());
    }

    /** @brief 導入と更新の拒否がTABOOとPROTECTEDへ変換されることを検証する */
    void rejectInstallAndUpdate()
    {
        auto &manager = ZyppManager::instance();
        const auto fresh = package("qz-new");
        const auto update = package("qz-app");
        QVERIFY(fresh->setStatus(zypp::ui::S_Install));
        QVERIFY(update->setStatus(zypp::ui::S_Update));
        int prompts = 0;
        const auto result = manager.confirmLicenses([&prompts](const PromptRequest &request) {
            ++prompts;
            if (request.type != QLatin1String("license")) {
                return QVariantMap();
            }

            return QVariantMap{{QStringLiteral("action"), QStringLiteral("reject")}};
        });
        QVERIFY2(result.success, qPrintable(result.errorMessage));
        QVERIFY(result.changed);
        QCOMPARE(prompts, 2);
        QCOMPARE(result.acceptedCount, 0);
        QCOMPARE(result.rejectedPackages.size(), 2);
        QVERIFY(result.rejectedPackages.contains(QStringLiteral("qz-new")));
        QVERIFY(result.rejectedPackages.contains(QStringLiteral("qz-app")));
        QCOMPARE(fresh->status(), zypp::ui::S_Taboo);
        QCOMPARE(update->status(), zypp::ui::S_Protected);
        const auto resolved = manager.resolveDependencies();
        QVERIFY(resolved.success);
        QCOMPARE(fresh->status(), zypp::ui::S_Taboo);
        QCOMPARE(update->status(), zypp::ui::S_Protected);
        const auto again = manager.confirmLicenses({});
        QVERIFY(again.success);
        QVERIFY(!again.changed);
        QVERIFY(manager.getPendingChanges().empty());
    }

    /** @brief 承認が候補状態と承認記録へ保存され、再確認を省略することを検証する */
    void acceptRecordsItemAndApproval()
    {
        auto &manager = ZyppManager::instance();
        const auto sel = package("qz-new");
        QVERIFY(sel->setStatus(zypp::ui::S_Install));
        int prompts = 0;
        const auto accept = [&prompts](const PromptRequest &) {
            ++prompts;
            return QVariantMap{{QStringLiteral("action"), QStringLiteral("accept")}};
        };
        const auto result = manager.confirmLicenses(accept);
        QVERIFY2(result.success, qPrintable(result.errorMessage));
        QVERIFY(!result.changed);
        QCOMPARE(result.acceptedCount, 1);
        QVERIFY(sel->candidateObj().status().isLicenceConfirmed());
        sel->candidateObj().status().setLicenceConfirmed(false);
        const auto again = manager.confirmLicenses(accept);
        QVERIFY(again.success);
        QCOMPARE(again.acceptedCount, 0);
        QCOMPARE(prompts, 1);
        QVERIFY(sel->candidateObj().status().isLicenceConfirmed());
    }

    /** @brief ライセンス本文の変更が新しい承認を必須にすることを検証する */
    void changedTextRequiresFreshApproval()
    {
        auto &manager = ZyppManager::instance();
        QVERIFY(package("qz-new")->setStatus(zypp::ui::S_Install));
        const auto accept = [](const PromptRequest &) {
            return QVariantMap{{QStringLiteral("action"), QStringLiteral("accept")}};
        };
        QVERIFY(manager.confirmLicenses(accept).success);
        zypp::sat::Pool::instance().reposErase("qz-avail");
        QVERIFY(loadLicensedRepo(QStringLiteral("Revised terms")));
        const auto sel = package("qz-new");
        QVERIFY(sel->setStatus(zypp::ui::S_Install));
        QVERIFY(!manager.commit().success);
        const auto result = manager.confirmLicenses(accept);
        QVERIFY2(result.success, qPrintable(result.errorMessage));
        QCOMPARE(result.acceptedCount, 1);
    }

    /** @brief 所有者への対話手段がない場合に安全側で失敗することを検証する */
    void missingPromptFailsClosed()
    {
        QVERIFY(package("qz-new")->setStatus(zypp::ui::S_Install));
        const auto result = ZyppManager::instance().confirmLicenses({});
        QVERIFY(!result.success);
        QVERIFY(!result.changed);
        QVERIFY(!result.errorMessage.isEmpty());
        QCOMPARE(package("qz-new")->status(), zypp::ui::S_Install);
    }

    /** @brief テスト後にZyppManagerを停止してグローバル状態を解放する */
    void cleanupTestCase()
    {
        ZyppManager::instance().shutdown();
    }

private:
    /**
     * @brief ライセンス本文を持つテスト用リポジトリを候補プールへ読み込む
     * @param text テスト候補へ設定するライセンス本文
     * @return 読み込みに成功した場合はtrue
     * @note libzyppプールへqz-availを追加して候補を差し替える
     */
    bool loadLicensedRepo(const QString &text)
    {
        const auto output = m_root.path() + QStringLiteral("/licensed.solv");
        if (!addLicenses(m_fixture.availSolv, output, text)) {
            return false;
        }
        zypp::RepoInfo repo;
        repo.setAlias("qz-avail");
        zypp::sat::Pool::instance().addRepoSolv(zypp::Pathname(output.toStdString()), repo);
        return true;
    }

    QTemporaryDir m_root;          // テスト用の隔離ルート
    SelectionFixture m_fixture;    // 最小solv候補を作成するフィクスチャ
    int m_generation = 0;          // テストごとのライセンス本文世代
};

QTEST_GUILESS_MAIN(LicenseSelectionTest)
#include "tst_LicenseSelection.moc"
