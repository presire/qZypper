/**
 * @file PackagesByNamesFixture.h
 * @brief 名前指定取得テスト用の一時リポジトリを生成する
 *
 * PackagesByNamesTestが候補、導入済み、代替候補と依存関係を再現するために使う
 * 入力 : 一時ルートのパス
 * 出力 : rpm-mdメタデータとsolvファイル
 * 依存 : Qt Core、repo2solv
 */
#ifndef QZYPPER_TESTS_PACKAGESBYNAMESFIXTURE_H
#define QZYPPER_TESTS_PACKAGESBYNAMESFIXTURE_H

#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QList>
#include <QProcess>
#include <QStandardPaths>
#include <QString>

/**
 * @brief 名前指定の部分取り直しテスト用のrpm-mdリポジトリを生成する
 *
 * qz-avail (候補6件)、@System (導入済み5件)、qz-alt (1件) のprimary.xmlを書き出し、
 * repo2solvでsolvへ変換する
 * 依存パッケージを引くqz-req-topと、
 * 複数版同時インストール対象のqz-mv (multiversion(kernel)を提供) を持つ点が
 * SelectionFixtureと異なる
 */
struct PackagesByNamesFixture
{
    /** @brief rpm-mdパッケージ1件分のメタデータ */
    struct Package
    {
        QString name;      // パッケージ名
        QString version;   // バージョン
        QString group;     // テストデータのグループ
        QString summary;   // 概要
        QString requires;  // 依存名
        QString provides;  // 提供名
        int checksumSeed;  // チェックサム生成用の値
    };

    QString skipReason;  // repo2solv不在時のスキップ理由
    QString error;       // 生成失敗時のエラー
    QString availSolv;   // qz-availのsolvパス
    QString systemSolv;  // @Systemのsolvパス
    QString altSolv;     // qz-altのsolvパス (候補切り替え検証用)

    /**
     * @brief フィクスチャを生成する
     * @param rootPath 一時ルートのパス
     * @return 生成に成功した場合はtrue
     * @note 一時ルートにファイルを書き出し、失敗理由またはsolvパスを保存する
     */
    bool build(const QString &rootPath)
    {
        const QString repo2solv = QStandardPaths::findExecutable(QStringLiteral("repo2solv"));
        if (repo2solv.isEmpty()) {
            skipReason = QStringLiteral("repo2solv is not installed");
            return false;
        }

        const QString base = rootPath + QStringLiteral("/packages-by-names-fixture");
        const QString availDir = base + QStringLiteral("/qz-avail");
        const QString systemDir = base + QStringLiteral("/system");
        const QString altDir = base + QStringLiteral("/qz-alt");
        if (!writeRepo(availDir, availablePackages()))
            return false;
        if (!writeRepo(systemDir, systemPackages()))
            return false;
        if (!writeRepo(altDir, alternativePackages()))
            return false;

        availSolv = base + QStringLiteral("/qz-avail.solv");
        systemSolv = base + QStringLiteral("/system.solv");
        altSolv = base + QStringLiteral("/qz-alt.solv");
        return runRepo2Solv(repo2solv, availDir, availSolv)
            && runRepo2Solv(repo2solv, systemDir, systemSolv)
            && runRepo2Solv(repo2solv, altDir, altSolv);
    }

    /**
     * @brief ファイルを書き出す (親ディレクトリは自動作成)
     * @param path 書き出し先
     * @param data 書き出す内容
     * @return 全量を書き出せた場合はtrue
     */
    static bool writeFile(const QString &path, const QByteArray &data)
    {
        QDir().mkpath(QFileInfo(path).absolutePath());
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly))
            return false;
        return file.write(data) == data.size();
    }

    /**
     * @brief repo2solvでリポジトリをsolvへ変換する
     * @param repo2solv repo2solvのパス
     * @param dir rpm-mdリポジトリのディレクトリ
     * @param output 出力するsolvファイル
     * @return 変換に成功した場合はtrue
     * @note 外部プロセスを実行し、失敗時はerrorを設定する
     */
    bool runRepo2Solv(const QString &repo2solv, const QString &dir, const QString &output)
    {
        QProcess process;
        process.start(repo2solv, {QStringLiteral("-o"), output, dir});
        if (!process.waitForFinished(60000)) {
            error = QStringLiteral("repo2solv timed out");
            return false;
        }
        if (process.exitCode() != 0) {
            error = QStringLiteral("repo2solv failed: ")
                + QString::fromLocal8Bit(process.readAllStandardError());
            return false;
        }
        return true;
    }

private:
    /**
     * @brief パッケージの一覧からrpm-mdメタデータを書き出す
     * @param dir リポジトリのディレクトリ
     * @param packages 書き出すパッケージ
     * @return 両メタデータファイルの書き出しに成功した場合はtrue
     */
    static bool writeRepo(const QString &dir, const QList<Package> &packages)
    {
        QString xml = QStringLiteral("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
            "<metadata xmlns=\"http://linux.duke.edu/metadata/common\""
            " xmlns:rpm=\"http://linux.duke.edu/metadata/rpm\" packages=\"%1\">\n")
            .arg(packages.size());

        for (const auto &package : packages) {
            QString format;
            if (!package.provides.isEmpty()) {
                format += QStringLiteral("    <rpm:provides><rpm:entry name=\"%1\"/></rpm:provides>\n")
                              .arg(package.provides);
            }
            if (!package.requires.isEmpty()) {
                format += QStringLiteral("    <rpm:requires><rpm:entry name=\"%1\"/></rpm:requires>\n")
                              .arg(package.requires);
            }
            xml += QStringLiteral("<package type=\"rpm\">\n"
                "  <name>%1</name>\n"
                "  <arch>noarch</arch>\n"
                "  <version epoch=\"0\" ver=\"%2\" rel=\"1\"/>\n"
                "  <checksum type=\"sha256\" pkgid=\"YES\">%3</checksum>\n"
                "  <summary>%4</summary>\n"
                "  <description>qz test package</description>\n"
                "  <size package=\"1\" installed=\"1\" archive=\"1\"/>\n"
                "  <location href=\"%1.rpm\"/>\n"
                "  <format>\n"
                "    <rpm:vendor>openSUSE</rpm:vendor>\n%5  </format>\n"
                "</package>\n")
                .arg(package.name, package.version, checksum(package.checksumSeed),
                     package.summary, format);
        }
        xml += QStringLiteral("</metadata>\n");

        const QString repomd = QStringLiteral("<?xml version=\"1.0\"?>\n"
            "<repomd xmlns=\"http://linux.duke.edu/metadata/repo\">\n"
            "  <data type=\"primary\">\n"
            "    <location href=\"repodata/primary.xml\"/>\n"
            "  </data>\n"
            "</repomd>\n");

        return writeFile(dir + QStringLiteral("/repodata/primary.xml"), xml.toUtf8())
            && writeFile(dir + QStringLiteral("/repodata/repomd.xml"), repomd.toUtf8());
    }

    /** @brief テスト用の固定長チェックサムを生成する
     * @param seed 識別用の値
     * @return 16進数を末尾に付けた文字列
     */
    static QString checksum(int seed)
    {
        QString value(60, QLatin1Char('0'));
        value += QStringLiteral("%1").arg(seed, 4, 16, QLatin1Char('0'));
        return value;
    }

    /** @brief 候補リポジトリのテストデータを返す
     * @return 候補パッケージの一覧
     */
    static QList<Package> availablePackages()
    {
        return {
            {QStringLiteral("qz-app"), QStringLiteral("2.0"),
             QStringLiteral("Productivity/Text/Editors"), QStringLiteral("Qz Application"),
             {}, {}, 1},
            {QStringLiteral("qz-lib"), QStringLiteral("1.5"),
             QStringLiteral("Productivity/Text/Editors"), QStringLiteral("Qz Library"),
             {}, {}, 2},
            {QStringLiteral("qz-mv"), QStringLiteral("1.1"),
             QStringLiteral("System/Kernel"), QStringLiteral("Qz Multiversion"),
             {}, QStringLiteral("multiversion(kernel)"), 3},
            {QStringLiteral("qz-new"), QStringLiteral("1.0"),
             QStringLiteral("Productivity/Text"), QStringLiteral("QZ New Tool"),
             {}, {}, 4},
            {QStringLiteral("qz-req-dep"), QStringLiteral("1.0"),
             QStringLiteral("Development/Libraries"), QStringLiteral("Qz Required Dependency"),
             {}, {}, 5},
            {QStringLiteral("qz-req-top"), QStringLiteral("1.0"),
             QStringLiteral("Productivity/Text"), QStringLiteral("Qz Required Top"),
             QStringLiteral("qz-req-dep"), {}, 6},
        };
    }

    /** @brief 導入済みリポジトリのテストデータを返す
     * @return 導入済みパッケージの一覧
     */
    static QList<Package> systemPackages()
    {
        return {
            {QStringLiteral("qz-app"), QStringLiteral("1.0"),
             QStringLiteral("Productivity/Text/Editors"), QStringLiteral("Qz Application"),
             {}, {}, 7},
            {QStringLiteral("qz-inst"), QStringLiteral("1.0"),
             QStringLiteral("Productivity/Text"), QStringLiteral("Qz Installed"),
             {}, {}, 8},
            {QStringLiteral("qz-lib"), QStringLiteral("1.0"),
             QStringLiteral("Productivity/Text/Editors"), QStringLiteral("Qz Library"),
             {}, {}, 9},
            {QStringLiteral("qz-lock"), QStringLiteral("1.0"),
             QStringLiteral("Productivity/Text"), QStringLiteral("Qz Locked"),
             {}, {}, 10},
            {QStringLiteral("qz-mv"), QStringLiteral("1.0"),
             QStringLiteral("System/Kernel"), QStringLiteral("Qz Multiversion"),
             {}, QStringLiteral("multiversion(kernel)"), 11},
        };
    }

    /** @brief 別候補のリポジトリ用データを返す
     * @return 代替候補の一覧
     */
    static QList<Package> alternativePackages()
    {
        return {
            {QStringLiteral("qz-lib"), QStringLiteral("1.2"),
             QStringLiteral("Productivity/Text/Editors"), QStringLiteral("Qz Library"),
             {}, {}, 12},
        };
    }
};

#endif
