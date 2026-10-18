#include "ZyppManager.h"
#include "InputValidation.h"
#include <QtTest>
#include <limits>

using namespace qZypper;

class ValidationTest : public QObject {
    Q_OBJECT
private slots:
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
    void validatesArgumentBoundary()
    {
        // Given
        QFETCH(QVariantList, arguments); QFETCH(bool, valid); QString error;
        // When
        const bool accepted = validateArguments(arguments, error);
        // Then
        QCOMPARE(accepted, valid);
    }
    void validatesRepoFileContent()
    {
        QString error;
        const QString repo = QStringLiteral("[oss]\nname=Main\r\nbaseurl=https://download.opensuse.org/\n\tgpgcheck=1\n");
        QVERIFY(validateRepoFileContent({repo}, error));
        // 共通検査では改行を拒否するが、.repo 本文専用検査は許可する
        QVERIFY(!validateArguments({repo}, error));
        QVERIFY(validateRepoFileContent({QString(40 * 1024, QLatin1Char('a'))}, error));
        QVERIFY(!validateRepoFileContent({QString(64 * 1024 + 1, QLatin1Char('a'))}, error));
        QVERIFY(!validateRepoFileContent({QStringLiteral("[a]\nname=x\x01\n")}, error));
        QVERIFY(!validateRepoFileContent({QStringLiteral("a"), QStringLiteral("b")}, error));
        QVERIFY(!validateRepoFileContent({QVariant(42)}, error));
    }

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
    void checksUrl()
    {
        // Given
        QFETCH(QString, url); QFETCH(bool, valid); std::string error;
        // When
        const bool accepted = ZyppManager::validateUrl(url.toStdString(), error);
        // Then
        QCOMPARE(accepted, valid);
    }
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
    void checksAlias()
    {
        // Given
        QFETCH(QString, alias); QFETCH(bool, valid); std::string error;
        // When
        const bool accepted = ZyppManager::validateAlias(alias.toStdString(), error);
        // Then
        QCOMPARE(accepted, valid);
    }
    void validatesPriority_data()
    {
        QTest::addColumn<qlonglong>("priority"); QTest::addColumn<bool>("valid");
        QTest::newRow("negative") << qlonglong(-1) << false;
        QTest::newRow("zero") << qlonglong(0) << false;
        QTest::newRow("minimum") << qlonglong(1) << true;
        QTest::newRow("maximum") << qlonglong(200) << true;
        QTest::newRow("over") << qlonglong(201) << false;
    }
    void validatesPriority()
    {
        // Given
        QFETCH(qlonglong, priority); QFETCH(bool, valid); std::string error;
        // When
        const bool accepted = ZyppManager::validatePriority(priority, error);
        // Then
        QCOMPARE(accepted, valid);
    }
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
    void rejectsInvalidMapTypes()
    {
        // Given
        QFETCH(QVariantMap, map); RepoChanges changes; QString error;
        // When
        const bool accepted = RepoChanges::fromVariantMap(map, changes, error);
        // Then
        QVERIFY(!accepted);
    }
    void preservesOmittedPropertiesWhenParsingPartialUpdate()
    {
        // Given
        RepoChanges changes; QString error;
        // When
        QVERIFY(RepoChanges::fromVariantMap({{"enabled", false}, {"priority", 100.0}}, changes, error));
        // Then
        QVERIFY(changes.enabled.has_value()); QVERIFY(!*changes.enabled);
        QCOMPARE(*changes.priority, 100); QVERIFY(!changes.url.has_value()); QVERIFY(!changes.autoRefresh.has_value());
    }
    void acceptsOnlyKnownPackageStatuses_data()
    {
        QTest::addColumn<int>("status"); QTest::addColumn<bool>("valid");
        QTest::newRow("negative") << -1 << false;
        QTest::newRow("minimum") << 0 << true;
        QTest::newRow("maximum") << 9 << true;
        QTest::newRow("over") << 10 << false;
    }
    void acceptsOnlyKnownPackageStatuses()
    {
        // Given
        QFETCH(int, status); QFETCH(bool, valid);
        // When
        const bool accepted = ZyppManager::isValidPackageStatus(status);
        // Then
        QCOMPARE(accepted, valid);
    }
};

QTEST_GUILESS_MAIN(ValidationTest)
#include "tst_Validation.moc"
