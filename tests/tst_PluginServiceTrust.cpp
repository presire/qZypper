/**
 * @file tst_PluginServiceTrust.cpp
 * @brief プラグインサービスの出所検査の単体テスト
 *
 * ユーザー所有のファイル、シンボリックリンクによる脱出、書き換え可能な中間リンク、
 * シェルメタ文字を拒否し、
 * root所有で書き込み不可のシステムファイルを許容することを検証する
 *
 * 入力: なし
 * 出力: テスト結果 (標準出力)
 * 依存: RepoPolicy.h、libzypp、Qt Test
 */

#include <QtTest>
#include <QTemporaryDir>
#include <QFile>
#include <filesystem>
#include <string>
#include <zypp/RepoManager.h>
#include <zypp/ServiceInfo.h>
#include <zypp/Url.h>
#include "RepoPolicy.h"

using namespace qZypper;

namespace {

/**
 * @brief 実行パスからPLUGIN型のサービス情報を組み立てる
 * @param path 実行スクリプトの絶対パス
 * @return libzyppがプラグインサービスに対して持つのと同じ形式のServiceInfo
 */
zypp::ServiceInfo makePluginService(const std::string &path)
{
    zypp::ServiceInfo svc;

    // libzyppはプラグインサービスをfileスキームのURLで保持する
    zypp::Url url("file://" + path);
    svc.setAlias(std::filesystem::path(path).filename().string());
    svc.setUrl(url);
    svc.setType(zypp::repo::ServiceType::PLUGIN);
    return svc;
}

/**
 * @brief 中身のある実行可能ファイルを作る
 * @param path 作成先パス
 * @return 作成できた場合はtrue
 */
bool writeScript(const std::filesystem::path &path)
{
    // QFileでテスト用スクリプトを開いて書き込む
    QFile file(QString::fromStdString(path.string()));
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return false;
    }

    file.write("#!/bin/sh\nexit 0\n");
    file.close();

    // 実行可能ファイルとして扱えるよう権限を設定する
    std::filesystem::permissions(path,
        std::filesystem::perms::owner_read | std::filesystem::perms::owner_write
            | std::filesystem::perms::owner_exec | std::filesystem::perms::group_read
            | std::filesystem::perms::group_exec | std::filesystem::perms::others_read
            | std::filesystem::perms::others_exec);
    return true;
}

} // namespace

/**
 * @class PluginServiceTrustTest
 * @brief プラグインサービスの実行スクリプト出所検査を検証する
 */
class PluginServiceTrustTest : public QObject {
    Q_OBJECT
private slots:
    /** @brief テストユーザー所有のディレクトリ内のスクリプトを拒否することを検証する */
    void rejectsUserOwnedScript()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());

        const std::filesystem::path script = std::filesystem::path(dir.path().toStdString()) / "svc.sh";
        QVERIFY(writeScript(script));

        const zypp::ServiceInfo svc = makePluginService(script.string());
        std::string err;
        QVERIFY(!RepoPolicy::checkPluginServiceTrusted(svc, dir.path().toStdString(), err));
        QVERIFY(!err.empty());
    }

    /** @brief プラグインディレクトリ外へ抜けるシンボリックリンクを拒否することを検証する */
    void rejectsSymlinkEscape()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());

        const std::filesystem::path base = dir.path().toStdString();
        const std::filesystem::path services = base / "services";
        std::filesystem::create_directory(services);

        const std::filesystem::path outside = base / "outside.sh";
        QVERIFY(writeScript(outside));

        const std::filesystem::path link = services / "evil.sh";
        std::error_code ec;
        std::filesystem::create_symlink(outside, link, ec);
        QVERIFY2(!ec, ec.message().c_str());

        const zypp::ServiceInfo svc = makePluginService(link.string());
        std::string err;
        QVERIFY(!RepoPolicy::checkPluginServiceTrusted(svc, services.string(), err));
        QVERIFY(!err.empty());
    }

    /**
     * @brief 書き換え可能な中間リンクを経由して信頼済み領域へ戻るパスを拒否することを検証する
     * @note 利用者所有のディレクトリにroot所有の/usr/binを指すリンクを置き、そのリンク経由のパスを検査する
     */
    void rejectsWritableIntermediateSymlink()
    {
        // テスト環境にシステムファイルが無い場合は検査できないためスキップする
        if (!std::filesystem::exists("/usr/bin/env")) {
            QSKIP("system test file not available");
        }

        QTemporaryDir dir;
        QVERIFY(dir.isValid());

        const std::filesystem::path link = std::filesystem::path(dir.path().toStdString()) / "bin";
        std::error_code ec;
        std::filesystem::create_directory_symlink("/usr/bin", link, ec);
        QVERIFY2(!ec, ec.message().c_str());

        // 正規化後は/usr/bin/envとなり所有者の検査は通るが、リンクは利用者が差し替え可能なため拒否する
        const zypp::ServiceInfo svc = makePluginService((link / "env").string());
        std::string err;
        QVERIFY(!RepoPolicy::checkPluginServiceTrusted(svc, "/usr/bin", err));
        QVERIFY(!err.empty());
    }

    /** @brief root所有で書き込み不可のシステムファイルを信頼できると判定することを検証する */
    void acceptsRootOwnedSystemFile()
    {
        const std::string script = "/usr/bin/env";
        // テスト環境にシステムファイルが無い場合は検査できないためスキップする
        if (!std::filesystem::exists(script)) {
            QSKIP("system test file not available");
        }

        const zypp::ServiceInfo svc = makePluginService(script);
        std::string err;
        QVERIFY2(RepoPolicy::checkPluginServiceTrusted(svc, "/usr/bin", err), err.c_str());

        // 通常のポリシー検査はPLUGIN型を拒否し、refresh経路のフラグでのみ許容する
        std::string policyErr;
        QVERIFY(!RepoPolicy::checkServicePolicy(svc, policyErr, false));
        QVERIFY(RepoPolicy::checkServicePolicy(svc, policyErr, true));
    }

    /** @brief シェルメタ文字を含むパスを拒否することを検証する */
    void rejectsShellMetacharacters()
    {
        const zypp::ServiceInfo svc = makePluginService("/usr/bin/env;id");
        std::string err;
        QVERIFY(!RepoPolicy::checkPluginServiceTrusted(svc, "/usr/bin", err));
        QVERIFY(!err.empty());
    }
};

QTEST_GUILESS_MAIN(PluginServiceTrustTest)
#include "tst_PluginServiceTrust.moc"
