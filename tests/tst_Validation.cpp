#include <QtTest>
#include <limits>
#include "ZyppManager.h"
#include "RepoPolicy.h"
#include "InputValidation.h"

using namespace qZypper;

/**
 * @class ValidationTest
 * @brief 入力検証とリポジトリポリシーの境界・不正値判定を検証する
 *
 * 引数境界と制御文字の拒否、.repo本文専用の検証、URL・別名・優先度の妥当性、
 * 不正な型のマップ拒否、部分更新での省略プロパティ保持、既知ステータスの判定を対象にする
 */
class ValidationTest : public QObject {
    Q_OBJECT
private slots:
    /** @brief validatesArgumentBoundaryの入力データを提供する */
    void validatesArgumentBoundary_data()
    {
        QTest::addColumn<QVariantList>("arguments"); QTest::addColumn<bool>("valid");
        QTest::newRow("valid") << QVariantList{QStringLiteral("rpm"), 127} << true;
        QTest::newRow("empty") << QVariantList{} << true;
        QTest::newRow("string-boundary") << QVariantList{QString(8192, 'a')} << true;
        QTest::newRow("string-over") << QVariantList{QString(8193, 'a')} << false;
        QTest::newRow("control") << QVariantList{QStringLiteral("repo\n[evil]")} << false;
        QTest::newRow("map-control") << QVariantList{QVariantMap{{"url", "https://host/\r"}}} << false;
        QTest::newRow("map-valid") << QVariantList{QVariantMap{{"enabled", false}}} << true;
    }
    /** @brief 引数の型・境界と制御文字の可否を検証する */
    void validatesArgumentBoundary()
    {
        // 準備 : 引数リストと期待値を入力する
        QFETCH(QVariantList, arguments); QFETCH(bool, valid); QString error;
        // 実行 : 引数を検証する
        const bool accepted = validateArguments(arguments, error);
        // 確認 : 可否が期待値と一致することを確認する
        QCOMPARE(accepted, valid);
    }
    /** @brief .repo本文専用の検証が改行や制御文字を扱うことを検証する */
    void validatesRepoFileContent()
    {
        QString error;
        const QString repo = QStringLiteral("[oss]\nname=Main\r\nbaseurl=https://download.opensuse.org/\n\tgpgcheck=1\n");
        QVERIFY(validateRepoFileContent({repo}, error));
        // 共通検査では改行を拒否するが、.repo本文専用検査は許可する
        QVERIFY(!validateArguments({repo}, error));
        QVERIFY(validateRepoFileContent({QString(40 * 1024, QLatin1Char('a'))}, error));
        QVERIFY(!validateRepoFileContent({QString(64 * 1024 + 1, QLatin1Char('a'))}, error));
        QVERIFY(!validateRepoFileContent({QStringLiteral("[a]\nname=x\x01\n")}, error));
        QVERIFY(!validateRepoFileContent({QStringLiteral("a"), QStringLiteral("b")}, error));
        QVERIFY(!validateRepoFileContent({QVariant(42)}, error));
    }

    /** @brief checksUrlの入力データを提供する */
    void checksUrl_data()
    {
        QTest::addColumn<QString>("url"); QTest::addColumn<bool>("valid");
        QTest::newRow("https") << "https://download.opensuse.org/repositories/repo/" << true;
        QTest::newRow("file") << "file:///srv/repo" << true;
        QTest::newRow("nfs") << "nfs://server/srv/repo" << true;
        QTest::newRow("empty") << "" << false;
        QTest::newRow("plugin") << "plugin:/usr/bin/evil" << false;
        QTest::newRow("control") << "https://example.org/repo\ntype=plugin" << false;
        QTest::newRow("nested-plugin") << "iso:/?iso=image.iso&url=plugin%3A%2Fbad" << false;
    }
    /** @brief URLのスキーム許可と制御文字の拒否を検証する */
    void checksUrl()
    {
        // 準備 : URLと期待値を入力する
        QFETCH(QString, url); QFETCH(bool, valid); std::string error;
        // 実行 : URLを検証する
        const bool accepted = RepoPolicy::validateUrl(url.toStdString(), error);
        // 確認 : 可否が期待値と一致することを確認する
        QCOMPARE(accepted, valid);
    }
    /** @brief checksAliasの入力データを提供する */
    void checksAlias_data()
    {
        QTest::addColumn<QString>("alias"); QTest::addColumn<bool>("valid");
        QTest::newRow("normal") << "repo-oss_16.0" << true;
        QTest::newRow("empty") << "" << false;
        QTest::newRow("traversal") << "../repo" << false;
        QTest::newRow("ini") << "repo\n[evil]" << false;
        QTest::newRow("slash") << "repo/path" << false;
        QTest::newRow("backslash") << "repo\\path" << false;
        QTest::newRow("boundary") << QString(200, 'a') << true;
        QTest::newRow("too-long") << QString(201, 'a') << false;
    }
    /** @brief リポジトリ別名の許可文字と長さ境界を検証する */
    void checksAlias()
    {
        // 準備 : 別名と期待値を入力する
        QFETCH(QString, alias); QFETCH(bool, valid); std::string error;
        // 実行 : 別名を検証する
        const bool accepted = RepoPolicy::validateAlias(alias.toStdString(), error);
        // 確認 : 可否が期待値と一致することを確認する
        QCOMPARE(accepted, valid);
    }
    /** @brief validatesPriorityの入力データを提供する */
    void validatesPriority_data()
    {
        QTest::addColumn<qlonglong>("priority"); QTest::addColumn<bool>("valid");
        QTest::newRow("negative") << qlonglong(-1) << false;
        QTest::newRow("zero") << qlonglong(0) << false;
        QTest::newRow("minimum") << qlonglong(1) << true;
        QTest::newRow("maximum") << qlonglong(200) << true;
        QTest::newRow("over") << qlonglong(201) << false;
    }
    /** @brief 優先度の許容範囲の境界を検証する */
    void validatesPriority()
    {
        // 準備 : 優先度と期待値を入力する
        QFETCH(qlonglong, priority); QFETCH(bool, valid); std::string error;
        // 実行 : 優先度を検証する
        const bool accepted = RepoPolicy::validatePriority(priority, error);
        // 確認 : 可否が期待値と一致することを確認する
        QCOMPARE(accepted, valid);
    }
    /** @brief rejectsInvalidMapTypesの入力データを提供する */
    void rejectsInvalidMapTypes_data()
    {
        QTest::addColumn<QVariantMap>("map");
        QTest::newRow("boolean-string") << QVariantMap{{"enabled", "false"}};
        QTest::newRow("name-number") << QVariantMap{{"name", 1}};
        QTest::newRow("fraction") << QVariantMap{{"priority", 99.5}};
        QTest::newRow("nan") << QVariantMap{{"priority", std::numeric_limits<double>::quiet_NaN()}};
        QTest::newRow("infinity") << QVariantMap{{"priority", std::numeric_limits<double>::infinity()}};
        QTest::newRow("unsigned-overflow") << QVariantMap{{"priority", QVariant::fromValue(std::numeric_limits<qulonglong>::max())}};
    }
    /** @brief 不正な型のマップを拒否することを検証する */
    void rejectsInvalidMapTypes()
    {
        // 準備 : 不正な型のマップを入力する
        QFETCH(QVariantMap, map); RepoChanges changes; QString error;
        // 実行 : マップから変更を解析する
        const bool accepted = RepoChanges::fromVariantMap(map, changes, error);
        // 確認 : 拒否されることを確認する
        QVERIFY(!accepted);
    }
    /** @brief 部分更新の解析で省略プロパティを保持することを検証する */
    void preservesOmittedPropertiesWhenParsingPartialUpdate()
    {
        // 準備 : 一部のプロパティのみを与える
        RepoChanges changes; QString error;
        // 実行 : 部分更新を解析する
        QVERIFY(RepoChanges::fromVariantMap({{"enabled", false}, {"priority", 100.0}}, changes, error));
        // 確認 : 指定した値は保持し省略した値は未設定のままとなることを確認する
        QVERIFY(changes.enabled.has_value()); QVERIFY(!*changes.enabled);
        QCOMPARE(*changes.priority, 100); QVERIFY(!changes.url.has_value()); QVERIFY(!changes.autoRefresh.has_value());
    }
    /** @brief acceptsOnlyKnownPackageStatusesの入力データを提供する */
    void acceptsOnlyKnownPackageStatuses_data()
    {
        QTest::addColumn<int>("status"); QTest::addColumn<bool>("valid");
        QTest::newRow("negative") << -1 << false;
        QTest::newRow("minimum") << 0 << true;
        QTest::newRow("maximum") << 9 << true;
        QTest::newRow("over") << 10 << false;
    }
    /** @brief 既知のパッケージステータスのみ受理することを検証する */
    void acceptsOnlyKnownPackageStatuses()
    {
        // 準備 : ステータスと期待値を入力する
        QFETCH(int, status); QFETCH(bool, valid);
        // 実行 : ステータスを検証する
        const bool accepted = ZyppManager::isValidPackageStatus(status);
        // 確認 : 可否が期待値と一致することを確認する
        QCOMPARE(accepted, valid);
    }
};

QTEST_GUILESS_MAIN(ValidationTest)
#include "tst_Validation.moc"
