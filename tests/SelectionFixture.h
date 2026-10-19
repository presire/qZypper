#ifndef QZYPPER_TESTS_SELECTIONFIXTURE_H
#define QZYPPER_TESTS_SELECTIONFIXTURE_H

#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QList>
#include <QProcess>
#include <QStandardPaths>
#include <QString>
#include <QStringList>

/**
 * @brief 選択系テスト用のrpm-mdリポジトリを生成する
 *
 * qz-avail (候補6件) と @System (導入済み3件)、および候補切り替え検証用の
 * qz-alt (2件) のprimary.xmlを書き出し、repo2solvでsolvへ変換する。
 * qz-altは既定では読み込まず、必要なテストだけが明示的にプールへ追加する
 */
struct SelectionFixture
{
    /** @brief rpm-mdパッケージ1件分のメタデータ */
    struct Package
    {
        QString name;
        QString version;
        QString group;
        QString summary;
        QString recommends;
        QString file;
        int checksumSeed;
    };

    QString skipReason;  // repo2solv不在時のスキップ理由
    QString error;       // 生成失敗時のエラー
    QString availSolv;   // qz-availのsolvパス
    QString systemSolv;  // @Systemのsolvパス
    QString altSolv;     // qz-altのsolvパス (候補切り替え検証用、既定では読み込まない)

    /**
     * @brief フィクスチャを生成する
     * @param rootPath 一時ルートのパス
     * @return 生成に成功した場合はtrue
     */
    bool build(const QString &rootPath)
    {
        const QString repo2solv = QStandardPaths::findExecutable(QStringLiteral("repo2solv"));
        if (repo2solv.isEmpty()) {
            skipReason = QStringLiteral("repo2solv is not installed");
            return false;
        }

        const QString base = rootPath + QStringLiteral("/selection-fixture");
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

private:
    static bool writeRepo(const QString &dir, const QList<Package> &packages)
    {
        QString xml = QStringLiteral("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
            "<metadata xmlns=\"http://linux.duke.edu/metadata/common\""
            " xmlns:rpm=\"http://linux.duke.edu/metadata/rpm\" packages=\"%1\">\n")
            .arg(packages.size());

        for (const auto &package : packages) {
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
                "    <rpm:vendor>openSUSE</rpm:vendor>\n%5%6  </format>\n"
                "</package>\n")
                .arg(package.name, package.version, checksum(package.checksumSeed),
                     package.summary, groupElement(package.group), extraElements(package));
        }
        xml += QStringLiteral("</metadata>\n");

        // ファイル一覧検索用のメタデータを生成する。primary.xmlのfile要素はrepo2solvが
        // 取り込まないため、repomdにfilelistsデータとして明示的に登録する
        int fileCount = 0;
        QString filelists = QStringLiteral("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
            "<filelists xmlns=\"http://linux.duke.edu/metadata/filelists\" packages=\"%1\">\n");
        for (const auto &package : packages) {
            if (package.file.isEmpty())
                continue;
            ++fileCount;
            filelists += QStringLiteral("<package pkgid=\"%1\" name=\"%2\" arch=\"noarch\">\n"
                "  <version epoch=\"0\" ver=\"%3\" rel=\"1\"/>\n"
                "  <file>%4</file>\n"
                "</package>\n")
                .arg(checksum(package.checksumSeed), package.name, package.version, package.file);
        }
        filelists += QStringLiteral("</filelists>\n");
        filelists = filelists.arg(fileCount);

        const QString repomd = QStringLiteral("<?xml version=\"1.0\"?>\n"
            "<repomd xmlns=\"http://linux.duke.edu/metadata/repo\">\n"
            " <data type=\"primary\"><location href=\"repodata/primary.xml\"/></data>\n"
            " <data type=\"filelists\"><location href=\"repodata/filelists.xml\"/></data>\n"
            "</repomd>\n");

        return writeFile(dir + QStringLiteral("/repodata/primary.xml"), xml.toUtf8())
            && writeFile(dir + QStringLiteral("/repodata/filelists.xml"), filelists.toUtf8())
            && writeFile(dir + QStringLiteral("/repodata/repomd.xml"), repomd.toUtf8());
    }

    static QString groupElement(const QString &group)
    {
        if (group.isEmpty())
            return {};
        return QStringLiteral("    <rpm:group>%1</rpm:group>\n").arg(group);
    }

    static QString extraElements(const Package &package)
    {
        QString elements;
        if (!package.recommends.isEmpty()) {
            elements += QStringLiteral("    <rpm:recommends><rpm:entry name=\"%1\"/></rpm:recommends>\n")
                .arg(package.recommends);
        }
        if (!package.file.isEmpty())
            elements += QStringLiteral("    <file>%1</file>\n").arg(package.file);
        return elements;
    }

    /** @brief 64桁の16進チェックサムを生成する */
    static QString checksum(int seed)
    {
        QString value(60, QLatin1Char('0'));
        value += QStringLiteral("%1").arg(seed, 4, 16, QLatin1Char('0'));
        return value;
    }

    static QList<Package> availablePackages()
    {
        return {
            {QStringLiteral("qz-app"), QStringLiteral("2.0"),
             QStringLiteral("Productivity/Text/Editors"), QStringLiteral("Qz Application"),
             QStringLiteral("qz-extra"), QStringLiteral("/usr/bin/qz-app"), 1},
            {QStringLiteral("qz-app-devel"), QStringLiteral("2.0"),
             QStringLiteral("Development/Libraries/C and C++"), QStringLiteral("Qz Application Development"),
             {}, {}, 2},
            {QStringLiteral("qz-app-debuginfo"), QStringLiteral("2.0"),
             QStringLiteral("Development/Debug"), QStringLiteral("Qz Application Debug"),
             {}, {}, 3},
            {QStringLiteral("qz-lib"), QStringLiteral("1.5"),
             QStringLiteral("Productivity/Text/Editors"), QStringLiteral("Qz Library"),
             {}, {}, 4},
            {QStringLiteral("qz-extra"), QStringLiteral("1.0"),
             {}, QStringLiteral("Qz Extra"), {}, {}, 5},
            {QStringLiteral("qz-new"), QStringLiteral("1.0"),
             QStringLiteral("Productivity/Text"), QStringLiteral("QZ New Tool"), {}, {}, 6},
        };
    }

    static QList<Package> systemPackages()
    {
        return {
            {QStringLiteral("qz-app"), QStringLiteral("1.0"),
             QStringLiteral("Productivity/Text/Editors"), QStringLiteral("Qz Application"),
             QStringLiteral("qz-extra"), QStringLiteral("/usr/bin/qz-app"), 7},
            {QStringLiteral("qz-lib"), QStringLiteral("1.0"),
             QStringLiteral("Productivity/Text/Editors"), QStringLiteral("Qz Library"),
             {}, {}, 8},
            {QStringLiteral("qz-old"), QStringLiteral("1.0"),
             QStringLiteral("Amusements/Games"), QStringLiteral("Qz Old Game"),
             {}, {}, 9},
        };
    }

    static QList<Package> alternativePackages()
    {
        return {
            {QStringLiteral("qz-lib"), QStringLiteral("1.2"),
             QStringLiteral("Productivity/Text/Editors"), QStringLiteral("Qz Library"),
             {}, {}, 10},
            {QStringLiteral("qz-app"), QStringLiteral("1.0"),
             QStringLiteral("Productivity/Text/Editors"), QStringLiteral("Qz Application"),
             {}, {}, 11},
        };
    }

    static bool writeFile(const QString &path, const QByteArray &data)
    {
        QDir().mkpath(QFileInfo(path).absolutePath());
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly))
            return false;
        return file.write(data) == data.size();
    }

    bool runRepo2Solv(const QString &repo2solv, const QString &dir, const QString &output)
    {
        QProcess process;
        // ファイル一覧検索を検証できるよう、filelists.xmlの内容をsolvへ取り込む
        process.start(repo2solv, {QStringLiteral("-F"), QStringLiteral("-o"), output, dir});
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
};

#endif
