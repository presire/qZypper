#include "SearchOptions.h"

#include <QtTest>

using namespace qZypper;

class SearchOptionsTest : public QObject {
    Q_OBJECT
private slots:
    void appliesDefaultsWhenOnlyQueryGiven()
    {
        // Given
        const QVariantMap options{{QStringLiteral("query"), QStringLiteral("kernel")}};
        SearchOptions out;
        QString error;

        // When
        const bool accepted = parseSearchOptions(options, out, error);

        // Then
        QVERIFY2(accepted, qPrintable(error));
        QCOMPARE(out.query, QStringLiteral("kernel"));
        QCOMPARE(out.flags, 1);
        QCOMPARE(static_cast<int>(out.match), static_cast<int>(SearchMatchMode::Substring));
        QCOMPARE(out.offset, 0u);
        QCOMPARE(out.limit, 0u);
        QVERIFY(!out.installedOnly);
        QVERIFY(out.repoAlias.isEmpty());
    }

    void parsesMatchModes_data()
    {
        QTest::addColumn<QString>("match");
        QTest::addColumn<int>("expected");

        QTest::newRow("substring") << QStringLiteral("substring")
                                   << static_cast<int>(SearchMatchMode::Substring);
        QTest::newRow("exact") << QStringLiteral("exact")
                               << static_cast<int>(SearchMatchMode::Exact);
        QTest::newRow("glob") << QStringLiteral("glob")
                              << static_cast<int>(SearchMatchMode::Glob);
        QTest::newRow("regex") << QStringLiteral("regex")
                               << static_cast<int>(SearchMatchMode::Regex);
    }

    void parsesMatchModes()
    {
        // Given
        QFETCH(QString, match);
        QFETCH(int, expected);
        const QVariantMap options{{QStringLiteral("query"), QStringLiteral("k.*l")},
                                  {QStringLiteral("match"), match}};
        SearchOptions out;
        QString error;

        // When
        const bool accepted = parseSearchOptions(options, out, error);

        // Then
        QVERIFY2(accepted, qPrintable(error));
        QCOMPARE(static_cast<int>(out.match), expected);
    }

    void parsesFullOptionSet()
    {
        // Given: 既定値と異なる全オプションを明示する
        const QVariantMap options{{QStringLiteral("query"), QStringLiteral("libzypp")},
                                  {QStringLiteral("flags"), 7},
                                  {QStringLiteral("match"), QStringLiteral("glob")},
                                  {QStringLiteral("offset"), 5},
                                  {QStringLiteral("limit"), 25},
                                  {QStringLiteral("installedOnly"), true},
                                  {QStringLiteral("repoAlias"), QStringLiteral("repo-oss")}};
        SearchOptions out;
        QString error;

        // When
        const bool accepted = parseSearchOptions(options, out, error);

        // Then
        QVERIFY2(accepted, qPrintable(error));
        QCOMPARE(out.query, QStringLiteral("libzypp"));
        QCOMPARE(out.flags, 7);
        QCOMPARE(static_cast<int>(out.match), static_cast<int>(SearchMatchMode::Glob));
        QCOMPARE(out.offset, 5u);
        QCOMPARE(out.limit, 25u);
        QVERIFY(out.installedOnly);
        QCOMPARE(out.repoAlias, QStringLiteral("repo-oss"));
    }

    void rejectsInvalidOptions_data()
    {
        QTest::addColumn<QVariantMap>("options");

        QTest::newRow("missing-query") << QVariantMap{{QStringLiteral("flags"), 1}};
        QTest::newRow("query-wrong-type") << QVariantMap{{QStringLiteral("query"), 42}};
        QTest::newRow("query-too-long")
            << QVariantMap{{QStringLiteral("query"), QString(257, QLatin1Char('a'))}};
        QTest::newRow("flags-zero")
            << QVariantMap{{QStringLiteral("query"), QStringLiteral("a")}, {QStringLiteral("flags"), 0}};
        QTest::newRow("flags-too-large")
            << QVariantMap{{QStringLiteral("query"), QStringLiteral("a")}, {QStringLiteral("flags"), 128}};
        QTest::newRow("flags-wrong-type")
            << QVariantMap{{QStringLiteral("query"), QStringLiteral("a")},
                           {QStringLiteral("flags"), QStringLiteral("1")}};
        QTest::newRow("match-unknown")
            << QVariantMap{{QStringLiteral("query"), QStringLiteral("a")},
                           {QStringLiteral("match"), QStringLiteral("fuzzy")}};
        QTest::newRow("offset-negative")
            << QVariantMap{{QStringLiteral("query"), QStringLiteral("a")},
                           {QStringLiteral("offset"), -1}};
        QTest::newRow("limit-wrong-type")
            << QVariantMap{{QStringLiteral("query"), QStringLiteral("a")},
                           {QStringLiteral("limit"), QStringLiteral("10")}};
        QTest::newRow("installed-only-wrong-type")
            << QVariantMap{{QStringLiteral("query"), QStringLiteral("a")},
                           {QStringLiteral("installedOnly"), 1}};
        QTest::newRow("repo-alias-wrong-type")
            << QVariantMap{{QStringLiteral("query"), QStringLiteral("a")},
                           {QStringLiteral("repoAlias"), 3}};
        QTest::newRow("repo-alias-path")
            << QVariantMap{{QStringLiteral("query"), QStringLiteral("a")},
                           {QStringLiteral("repoAlias"), QStringLiteral("../etc")}};
        QTest::newRow("unknown-key")
            << QVariantMap{{QStringLiteral("query"), QStringLiteral("a")},
                           {QStringLiteral("limitx"), 1}};
        QTest::newRow("query-control-character")
            << QVariantMap{{QStringLiteral("query"), QStringLiteral("a\nb")}};
        QTest::newRow("regex-does-not-compile")
            << QVariantMap{{QStringLiteral("query"), QStringLiteral("([unclosed")},
                           {QStringLiteral("match"), QStringLiteral("regex")}};
    }

    void rejectsInvalidOptions()
    {
        // Given
        QFETCH(QVariantMap, options);
        SearchOptions out;
        QString error;

        // When
        const bool accepted = parseSearchOptions(options, out, error);

        // Then
        QVERIFY(!accepted);
        QVERIFY(!error.isEmpty());
    }
};

QTEST_GUILESS_MAIN(SearchOptionsTest)
#include "tst_SearchOptions.moc"
