/**
 * @file tst_PackageDetails.cpp
 * @brief PackageDetailsのQVariantMap往復変換を検証する
 *
 * 入力 :
 *   なし
 * 出力 :
 *   QtTestの終了コード
 * 依存 :
 *   Qt6::Test、qzypper-common
 */

#include <QtTest>

#include "PackageInfo.h"

using namespace qZypper;

/**
 * @class PackageDetailsTest
 * @brief PackageDetailsのQVariantMap往復変換を検証する
 * @note 各テストはD-Busやlibzyppに依存せず単体で実行できる
 */
class PackageDetailsTest : public QObject
{
    Q_OBJECT

private slots:
    void roundTripPreservesTechnicalData();
    void emptyTechnicalDataStaysEmpty();
};

/**
 * @brief 技術情報を持つPackageDetailsがキーを保ったまま復元されることを検証する
 * @note 失敗時はQCOMPAREが不一致のキーを報告する
 */
void PackageDetailsTest::roundTripPreservesTechnicalData()
{
    // 準備 : 全項目に値を設定したPackageDetailsを組み立てる
    PackageDetails details;
    details.basic.name             = QStringLiteral("kernel-default");
    details.basic.summary          = QStringLiteral("The Linux Kernel");
    details.basic.version          = QStringLiteral("6.9.9");
    details.basic.installedVersion = QStringLiteral("6.8.8");
    details.basic.arch             = QStringLiteral("x86_64");
    details.basic.repoAlias        = QStringLiteral("repo-oss");
    details.basic.repoName         = QStringLiteral("Main Repository");
    details.basic.size             = 123456789;
    details.basic.status           = 4;
    details.basic.updatable        = true;

    details.description       = QStringLiteral("Kernel description");
    details.license           = QStringLiteral("GPL-2.0-only");
    details.group             = QStringLiteral("System/Kernel");
    details.vendor            = QStringLiteral("SUSE");
    details.buildHost         = QStringLiteral("buildhost");
    details.buildTime         = QStringLiteral("2026-10-01T12:34:56");
    details.sourceRpm         = QStringLiteral("kernel-default-6.9.9-1.src");
    details.url               = QStringLiteral("https://www.kernel.org/");
    details.installSize       = 987654321;
    details.downloadSize      = 12345678;
    details.installTime       = QStringLiteral("2026-10-20T08:00:00");
    details.multiversion      = true;
    details.mixedMultiversion = true;
    details.provides          = {QStringLiteral("kernel-default")};
    details.requirements      = {QStringLiteral("filesystem")};
    details.conflicts         = {QStringLiteral("kernel-default-base")};
    details.obsoletes         = {QStringLiteral("kernel-default-old")};
    details.recommends        = {QStringLiteral("kernel-firmware")};
    details.suggests          = {QStringLiteral("kernel-devel")};
    details.fileList          = {QStringLiteral("/boot/vmlinuz")};
    details.changeLog         = {QStringLiteral("* Mon Oct 01 2026 - maintainer")};

    QVariantMap versionRow;
    versionRow[QStringLiteral("version")]      = QStringLiteral("6.9.9");
    versionRow[QStringLiteral("arch")]         = QStringLiteral("x86_64");
    versionRow[QStringLiteral("repoAlias")]    = QStringLiteral("repo-oss");
    versionRow[QStringLiteral("repoName")]     = QStringLiteral("Main Repository");
    versionRow[QStringLiteral("installed")]    = false;
    versionRow[QStringLiteral("status")]       = 1;
    versionRow[QStringLiteral("multiversion")] = true;
    versionRow[QStringLiteral("size")]         = QVariant::fromValue<qint64>(987654321);
    versionRow[QStringLiteral("downloadSize")] = QVariant::fromValue<qint64>(12345678);
    versionRow[QStringLiteral("buildTime")]    = QStringLiteral("2026-10-01T12:34:56");
    versionRow[QStringLiteral("installTime")]  = QString();
    details.availableVersions.append(versionRow);

    // 実行 : Mapへ変換して復元する
    const QVariantMap map = details.toVariantMap();
    const PackageDetails restored = PackageDetails::fromVariantMap(map);

    // 確認 : 技術情報のキーがそのまま復元されることを確認する
    QCOMPARE(map.value(QStringLiteral("installSize")).toLongLong(), qint64(987654321));
    QCOMPARE(map.value(QStringLiteral("downloadSize")).toLongLong(), qint64(12345678));
    QCOMPARE(map.value(QStringLiteral("buildTime")).toString(), QStringLiteral("2026-10-01T12:34:56"));
    QCOMPARE(map.value(QStringLiteral("installTime")).toString(), QStringLiteral("2026-10-20T08:00:00"));
    QCOMPARE(map.value(QStringLiteral("multiversion")).toBool(), true);
    QCOMPARE(map.value(QStringLiteral("mixedMultiversion")).toBool(), true);

    QCOMPARE(restored.basic.name, details.basic.name);
    QCOMPARE(restored.basic.size, details.basic.size);
    QCOMPARE(restored.buildTime, details.buildTime);
    QCOMPARE(restored.installSize, details.installSize);
    QCOMPARE(restored.downloadSize, details.downloadSize);
    QCOMPARE(restored.installTime, details.installTime);
    QCOMPARE(restored.multiversion, details.multiversion);
    QCOMPARE(restored.mixedMultiversion, details.mixedMultiversion);
    QCOMPARE(restored.provides, details.provides);
    QCOMPARE(restored.requirements, details.requirements);
    QCOMPARE(restored.conflicts, details.conflicts);
    QCOMPARE(restored.obsoletes, details.obsoletes);
    QCOMPARE(restored.recommends, details.recommends);
    QCOMPARE(restored.suggests, details.suggests);
    QCOMPARE(restored.fileList, details.fileList);
    QCOMPARE(restored.changeLog, details.changeLog);
    QCOMPARE(restored.availableVersions.size(), 1);
    QCOMPARE(restored.availableVersions.first(), versionRow);
}

/**
 * @brief 未設定の技術情報が0・空文字のまま復元されることを検証する
 * @note 1970年の日時が表示されないこともここで検証する
 */
void PackageDetailsTest::emptyTechnicalDataStaysEmpty()
{
    // 準備・実行 : 既定値のPackageDetailsを往復変換する
    const PackageDetails details;
    const PackageDetails restored = PackageDetails::fromVariantMap(details.toVariantMap());

    // 確認 : 0や空文字が維持され、1970年の日時などが入らないことを確認する
    QCOMPARE(restored.installSize, qint64(0));
    QCOMPARE(restored.downloadSize, qint64(0));
    QVERIFY(restored.buildTime.isEmpty());
    QVERIFY(restored.installTime.isEmpty());
    QVERIFY(!restored.multiversion);
    QVERIFY(!restored.mixedMultiversion);
    QVERIFY(restored.availableVersions.isEmpty());
}

QTEST_GUILESS_MAIN(PackageDetailsTest)
#include "tst_PackageDetails.moc"
