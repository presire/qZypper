/**
 * @file tst_LicenseConfirmation.cpp
 * @brief ライセンス承認の識別と準備判定を単体で検証する
 *
 * Qt Testから実行され、承認キー、拒否状態、再選択検出、D-Bus転送結果を検証する
 *
 * 入力 : 固定の候補情報、ライセンス本文、変更予定
 * 出力 : Qt Testの成功または失敗
 * 依存 : Qt Test、LicenseConfirmation、LicensePreparation
 */
#include <QtTest>
#include "LicenseConfirmation.h"
#include "LicenseConfirmationResult.h"
#include "LicensePreparation.h"

using namespace qZypper;

/**
 * @class LicenseConfirmationTest
 * @brief ライセンス承認の識別境界と準備ループの純粋ロジックを検証する
 *
 * 外部サービスに接続せず、ライセンス確認フローの値オブジェクトを検証する
 */
class LicenseConfirmationTest : public QObject
{
    Q_OBJECT
private slots:
    /** @brief 承認キーが候補の全識別値とライセンス本文から決まることを検証する */
    void approvalKeyBindsEveryField()
    {
        const auto key = licenseApprovalKey("pkg", "1-1", "x86_64", "repo", "terms");
        QCOMPARE(key, licenseApprovalKey("pkg", "1-1", "x86_64", "repo", "terms"));
        QVERIFY(key != licenseApprovalKey("other", "1-1", "x86_64", "repo", "terms"));
        QVERIFY(key != licenseApprovalKey("pkg", "2-1", "x86_64", "repo", "terms"));
        QVERIFY(key != licenseApprovalKey("pkg", "1-1", "aarch64", "repo", "terms"));
        QVERIFY(key != licenseApprovalKey("pkg", "1-1", "x86_64", "other", "terms"));
        QVERIFY(key != licenseApprovalKey("pkg", "1-1", "x86_64", "repo", "changed"));
        QVERIFY(licenseApprovalKey("a|b", "c", "d", "e", "terms")
                != licenseApprovalKey("a", "b|c", "d", "e", "terms"));
        const std::string longText(300000, 'x');
        QVERIFY(licenseApprovalKey("pkg", "1", "noarch", "repo", longText)
                != licenseApprovalKey("pkg", "1", "noarch", "repo", longText + "y"));
        QCOMPARE(licenseApprovalKey("", "", "", "", "").substr(8),
                 std::string("e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"));
    }

    /** @brief 拒否時の導入候補と更新候補が異なる状態へ変換されることを検証する */
    void rejectionMapping()
    {
        for (const auto status : {zypp::ui::S_Install, zypp::ui::S_AutoInstall}) {
            QCOMPARE(licenseRejectedStatus(status).value(), zypp::ui::S_Taboo);
        }

        for (const auto status : {zypp::ui::S_Update, zypp::ui::S_AutoUpdate}) {
            QCOMPARE(licenseRejectedStatus(status).value(), zypp::ui::S_Protected);
        }

        for (const auto status : {zypp::ui::S_Del, zypp::ui::S_AutoDel,
                                  zypp::ui::S_KeepInstalled, zypp::ui::S_NoInst,
                                  zypp::ui::S_Taboo, zypp::ui::S_Protected}) {
            QVERIFY(!licenseRejectedStatus(status));
        }
    }

    /** @brief 再解決で拒否済み候補が導入または更新対象へ戻ることを検出する */
    void detectsReintroducedPackages()
    {
        const QStringList rejected{QStringLiteral("declined")};
        for (const auto status : {PackageStatus::Install, PackageStatus::AutoInstall,
                                 PackageStatus::Update, PackageStatus::AutoUpdate}) {
            const QVariantMap item{{QStringLiteral("name"), QStringLiteral("declined")},
                                   {QStringLiteral("status"), static_cast<int>(status)}};
            QCOMPARE(reintroducedLicensePackages(rejected, {item, item}), rejected);
            QCOMPARE(reintroducedLicensePackages({}, {item}), QStringList());
        }
        for (const auto status : {PackageStatus::Del, PackageStatus::AutoDel,
                                 PackageStatus::Taboo, PackageStatus::Protected}) {
            const QVariantMap item{{QStringLiteral("name"), QStringLiteral("declined")},
                                   {QStringLiteral("status"), static_cast<int>(status)}};
            QVERIFY(reintroducedLicensePackages(rejected, {item}).isEmpty());
        }
        const QVariantMap other{{QStringLiteral("name"), QStringLiteral("other")},
                                {QStringLiteral("status"), static_cast<int>(PackageStatus::Install)}};
        QVERIFY(reintroducedLicensePackages(rejected, {other}).isEmpty());
    }

    /** @brief ライセンス確認結果がD-Bus転送形式で往復できることを検証する */
    void resultRoundTrip()
    {
        LicenseConfirmationResult result;
        result.success = true;
        result.changed = true;
        result.rejectedPackages = {QStringLiteral("declined")};
        result.acceptedCount = 2;
        result.errorMessage = QStringLiteral("partial error");
        const auto parsed = LicenseConfirmationResult::fromVariantMap(result.toVariantMap());
        QCOMPARE(parsed.success, result.success);
        QCOMPARE(parsed.changed, result.changed);
        QCOMPARE(parsed.rejectedPackages, result.rejectedPackages);
        QCOMPARE(parsed.acceptedCount, result.acceptedCount);
        QCOMPARE(parsed.errorMessage, result.errorMessage);
        QVERIFY(!LicenseConfirmationResult::fromVariantMap({}).success);
    }
};

QTEST_GUILESS_MAIN(LicenseConfirmationTest)
#include "tst_LicenseConfirmation.moc"
