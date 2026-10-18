#include "Types.h"
#include "ZyppManager.h"

#include <QtTest>
#include <QDir>
#include <QFile>
#include <QProcess>
#include <QStandardPaths>
#include <QTemporaryDir>

#include <zypp/RepoInfo.h>
#include <zypp/sat/Pool.h>

using namespace qZypper;

/**
 * @brief 一時ルートで ZyppManager を動かす結合テスト (root・ネットワーク不要)。
 *
 * ZYPP_READONLY_HACK で /run/zypp.pid のロックを避け、リポジトリ定義と
 * キャッシュは一時ルート配下だけを使う。
 */
class ZyppTempRootTest : public QObject {
    Q_OBJECT
private slots:
    void initTestCase();
    void refreshContinuesAfterFailedRepo();
    void reinitializeDoesNotReportStaleError();
    void rejectedPatchStatusFails();
    void modifyRepoRejectsPolicyViolation();
    void modifyRepoRejectsDisallowedUrl();
    void modifyRepoAcceptsValidUrl();
    void cleanupTestCase();

private:
    static bool writeFile(const QString &path, const QByteArray &data);
    static QByteArray readFile(const QString &path);
    QTemporaryDir m_root;
};

/** @brief 指定内容でファイルを作成する。 */
bool ZyppTempRootTest::writeFile(const QString &path, const QByteArray &data)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        return false;
    return file.write(data) == data.size();
}

/** @brief ファイル内容を読む (存在しなければ空)。 */
QByteArray ZyppTempRootTest::readFile(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return file.readAll();
}

/** @brief 到達不能・正常・modifyRepo 用のリポジトリを持つ一時ルートで初期化する。 */
void ZyppTempRootTest::initTestCase()
{
    qputenv("ZYPP_READONLY_HACK", "1");
    QVERIFY(m_root.isValid());
    const QString root = m_root.path();
    const QString reposDir = root + QStringLiteral("/etc/zypp/repos.d");
    QVERIFY(QDir().mkpath(root + QStringLiteral("/plain")));
    // 予約ポート 9 (discard) への接続は即座に拒否される
    QVERIFY(writeFile(reposDir + QStringLiteral("/a-broken.repo"),
        "[a-broken]\nname=a-broken\nenabled=1\nautorefresh=0\n"
        "baseurl=http://127.0.0.1:9/repo\ntype=rpm-md\ngpgcheck=1\n"));
    QVERIFY(writeFile(reposDir + QStringLiteral("/b-good.repo"),
        "[b-good]\nname=b-good\nenabled=1\nautorefresh=0\nbaseurl=dir:" + root.toUtf8()
        + "/plain\ntype=plaindir\n"));
    // modifyRepo 用 (無効化して更新対象から外す)。c-policy は https 以外の
    // ミラーリストを持つためポリシー違反、d-modify はポリシー準拠。
    QVERIFY(writeFile(reposDir + QStringLiteral("/c-policy.repo"),
        "[c-policy]\nname=c-policy\nenabled=0\nautorefresh=0\nbaseurl=dir:" + root.toUtf8()
        + "/plain\nmirrorlist=http://127.0.0.1:9/mirrors\ntype=plaindir\n"));
    QVERIFY(writeFile(reposDir + QStringLiteral("/d-modify.repo"),
        "[d-modify]\nname=d-modify\nenabled=0\nautorefresh=0\nbaseurl=dir:" + root.toUtf8()
        + "/plain\ntype=plaindir\n"));

    QVERIFY2(ZyppManager::instance().initialize(root.toStdString(), true),
             ZyppManager::instance().lastError().c_str());
}

/** @brief 1件のリポジトリが失敗しても残りを更新し、失敗とエラー文を返す。 */
void ZyppTempRootTest::refreshContinuesAfterFailedRepo()
{
    // Given
    auto &manager = ZyppManager::instance();
    QStringList reported;
    // When
    const bool ok = manager.refreshRepos([&](const std::string &alias, int) {
        reported << QString::fromStdString(alias);
    });
    // Then
    const QString error = QString::fromStdString(manager.lastError());
    QVERIFY(!ok);
    QVERIFY2(error.contains(QStringLiteral("a-broken")), qPrintable(error));
    QVERIFY2(!error.contains(QStringLiteral("b-good")), qPrintable(error));
    QVERIFY(reported.contains(QStringLiteral("b-good")));
    QVERIFY(!manager.lastOperationCancelled());
    QVERIFY(QFileInfo::exists(m_root.path() + QStringLiteral("/var/cache/zypp/solv/b-good/solv")));
}

/** @brief 初期化済みのまま再初期化しても前回の操作のエラーを返さない。 */
void ZyppTempRootTest::reinitializeDoesNotReportStaleError()
{
    // Given
    auto &manager = ZyppManager::instance();
    QVERIFY(!manager.refreshRepos());
    QVERIFY(!manager.lastError().empty());
    // When
    const bool ok = manager.initialize(m_root.path().toStdString(), true);
    // Then
    QVERIFY(ok);
    QVERIFY2(manager.lastError().empty(), manager.lastError().c_str());
}

/** @brief libzypp が状態変更を拒否した場合に成功を返さない。 */
void ZyppTempRootTest::rejectedPatchStatusFails()
{
    // Given: updateinfo だけを持つ rpm-md を solv に変換してプールへ追加する
    const QString repo2solv = QStandardPaths::findExecutable(QStringLiteral("repo2solv"));
    if (repo2solv.isEmpty())
        QSKIP("repo2solv is not installed");
    const QString md = m_root.path() + QStringLiteral("/patchmd");
    QVERIFY(writeFile(md + QStringLiteral("/repodata/updateinfo.xml"),
        "<?xml version=\"1.0\"?>\n<updates>\n"
        " <update from=\"test\" status=\"stable\" type=\"security\" version=\"1\">\n"
        "  <id>qzypper-test-1</id>\n  <title>test patch</title>\n"
        "  <severity>important</severity>\n  <issued date=\"1700000000\"/>\n"
        "  <description>test</description>\n"
        "  <pkglist><collection><package name=\"qzypper-dummy\" epoch=\"0\" version=\"1\""
        " release=\"1\" arch=\"noarch\" src=\"x\"><filename>x.rpm</filename></package>"
        "</collection></pkglist>\n </update>\n</updates>\n"));
    QVERIFY(writeFile(md + QStringLiteral("/repodata/repomd.xml"),
        "<?xml version=\"1.0\"?>\n<repomd xmlns=\"http://linux.duke.edu/metadata/repo\">\n"
        " <data type=\"updateinfo\"><location href=\"repodata/updateinfo.xml\"/></data>\n"
        "</repomd>\n"));
    const QString solv = m_root.path() + QStringLiteral("/patches.solv");
    QProcess proc;
    proc.start(repo2solv, {QStringLiteral("-o"), solv, md});
    QVERIFY(proc.waitForFinished(30000));
    QVERIFY2(proc.exitCode() == 0, proc.readAllStandardError().constData());
    zypp::RepoInfo info;
    info.setAlias("qzypper-patches");
    zypp::sat::Pool::instance().addRepoSolv(zypp::Pathname(solv.toStdString()), info);

    auto &manager = ZyppManager::instance();
    auto patchId = [&manager] {
        for (const PatchInfo &patch : manager.getPatches())
            if (patch.name == QStringLiteral("qzypper-test-1")) return patch.id.toStdString();
        return std::string();
    };
    std::string id = patchId();
    QVERIFY2(!id.empty(), "test patch not found in pool");
    QVERIFY2(manager.setPatchStatus(id, int(PackageStatus::Install)), manager.lastError().c_str());
    QVERIFY(manager.setPatchStatus(id, int(PackageStatus::NoInst)));

    // 同じパッチを導入済み側 (@System) だけに置くと、libzypp は S_Install を拒否する
    // (候補が無い対象はインストールできない)
    zypp::sat::Pool::instance().reposErase("qzypper-patches");
    zypp::sat::Pool::instance().reposErase(zypp::sat::Pool::systemRepoAlias());
    zypp::sat::Pool::instance().addRepoSolv(zypp::Pathname(solv.toStdString()),
                                            zypp::sat::Pool::systemRepoAlias());
    id = patchId();
    QVERIFY2(!id.empty(), "installed test patch not found in pool");

    // When
    const bool changed = manager.setPatchStatus(id, int(PackageStatus::Install));
    // Then
    QVERIFY(!changed);
    QVERIFY2(QString::fromStdString(manager.lastError()).contains(QStringLiteral("Cannot change status")),
             manager.lastError().c_str());
}

/** @brief 保存済み設定がポリシー違反なら URL 変更を保存しない。 */
void ZyppTempRootTest::modifyRepoRejectsPolicyViolation()
{
    // Given
    const QString file = m_root.path() + QStringLiteral("/etc/zypp/repos.d/c-policy.repo");
    const QByteArray before = readFile(file);
    QVERIFY(!before.isEmpty());
    RepoChanges changes;
    changes.url = QStringLiteral("dir:") + m_root.path() + QStringLiteral("/plain2");
    // When
    const bool ok = ZyppManager::instance().modifyRepo("c-policy", changes);
    // Then
    const QString error = QString::fromStdString(ZyppManager::instance().lastError());
    QVERIFY(!ok);
    QVERIFY2(error.contains(QStringLiteral("violates policy")), qPrintable(error));
    QVERIFY2(error.contains(QStringLiteral("mirrorlist")), qPrintable(error));
    QCOMPARE(readFile(file), before);
}

/** @brief 許可されないスキームへの URL 変更は失敗し、設定を変えない。 */
void ZyppTempRootTest::modifyRepoRejectsDisallowedUrl()
{
    // Given
    const QString file = m_root.path() + QStringLiteral("/etc/zypp/repos.d/d-modify.repo");
    const QByteArray before = readFile(file);
    QVERIFY(!before.isEmpty());
    RepoChanges changes;
    changes.url = QStringLiteral("plugin:evil?x=1");
    // When
    const bool ok = ZyppManager::instance().modifyRepo("d-modify", changes);
    // Then
    QVERIFY(!ok);
    QCOMPARE(readFile(file), before);
}

/** @brief ポリシー準拠の URL への変更は保存される。 */
void ZyppTempRootTest::modifyRepoAcceptsValidUrl()
{
    // Given
    const QString file = m_root.path() + QStringLiteral("/etc/zypp/repos.d/d-modify.repo");
    const QString newUrl = QStringLiteral("dir:") + m_root.path() + QStringLiteral("/plain2");
    RepoChanges changes;
    changes.url = newUrl;
    // When
    const bool ok = ZyppManager::instance().modifyRepo("d-modify", changes);
    // Then
    QVERIFY2(ok, ZyppManager::instance().lastError().c_str());
    QVERIFY2(readFile(file).contains(newUrl.toUtf8()), readFile(file).constData());
}

/** @brief libzypp の資源を解放する。 */
void ZyppTempRootTest::cleanupTestCase()
{
    ZyppManager::instance().shutdown();
}

QTEST_GUILESS_MAIN(ZyppTempRootTest)
#include "tst_ZyppTempRoot.moc"
