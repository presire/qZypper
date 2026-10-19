#include <QtTest>
#include "SearchOptions.h"

using namespace qZypper;

/**
 * @class SearchOptionsTest
 * @brief 検索オプションの解析・既定値・不正入力の拒否を検証する
 *
 * クエリのみを渡した時の既定値、一致モードの解析、全オプションの反映、POSIX EREと範囲付き繰り返しの受理、正規表現DoSを含む不正オプションの拒否を対象にする
 */
class SearchOptionsTest : public QObject {
    Q_OBJECT
private slots:
    /** @brief クエリのみを渡すと既定のオプションが設定されることを検証する */
    void appliesDefaultsWhenOnlyQueryGiven()
    {
        // 準備 : クエリのみを指定する
        const QVariantMap options{{QStringLiteral("query"), QStringLiteral("kernel")}};
        SearchOptions out;
        QString error;

        // 実行 : オプションを解析する
        const bool accepted = parseSearchOptions(options, out, error);

        // 確認 : クエリが反映され他は既定値になることを確認する
        QVERIFY2(accepted, qPrintable(error));
        QCOMPARE(out.query, QStringLiteral("kernel"));
        QCOMPARE(out.flags, 1);
        QCOMPARE(static_cast<int>(out.match), static_cast<int>(SearchMatchMode::Substring));
        QCOMPARE(out.offset, 0u);
        QCOMPARE(out.limit, 0u);
        QVERIFY(!out.installedOnly);
        QVERIFY(out.repoAlias.isEmpty());
    }

    /** @brief parsesMatchModesの入力データを提供する */
    void parsesMatchModes_data()
    {
        QTest::addColumn<QString>("match");
        QTest::addColumn<int>("expected");

        QTest::newRow("substring") << QStringLiteral("substring") << static_cast<int>(SearchMatchMode::Substring);
        QTest::newRow("prefix") << QStringLiteral("prefix") << static_cast<int>(SearchMatchMode::Prefix);
        QTest::newRow("exact") << QStringLiteral("exact") << static_cast<int>(SearchMatchMode::Exact);
        QTest::newRow("glob") << QStringLiteral("glob") << static_cast<int>(SearchMatchMode::Glob);
        QTest::newRow("regex") << QStringLiteral("regex") << static_cast<int>(SearchMatchMode::Regex);
    }

    /** @brief 一致モード文字列を対応するモードへ解析することを検証する */
    void parsesMatchModes()
    {
        // 準備 : 一致モード文字列と期待値を受け取る
        QFETCH(QString, match);
        QFETCH(int, expected);
        const QVariantMap options{{QStringLiteral("query"), QStringLiteral("k.*l")},
                                  {QStringLiteral("match"), match}};
        SearchOptions out;
        QString error;

        // 実行 : オプションを解析する
        const bool accepted = parseSearchOptions(options, out, error);

        // 確認 : 解析されたモードが期待値と一致することを確認する
        QVERIFY2(accepted, qPrintable(error));
        QCOMPARE(static_cast<int>(out.match), expected);
    }

    /** @brief 既定値と異なる全オプションを反映することを検証する */
    void parsesFullOptionSet()
    {
        // 準備 : 既定値と異なる全オプションを明示する
        const QVariantMap options{{QStringLiteral("query"), QStringLiteral("libzypp")},
                                  {QStringLiteral("flags"), 7},
                                  {QStringLiteral("match"), QStringLiteral("glob")},
                                  {QStringLiteral("offset"), 5},
                                  {QStringLiteral("limit"), 25},
                                  {QStringLiteral("installedOnly"), true},
                                  {QStringLiteral("caseSensitive"), true},
                                  {QStringLiteral("repoAlias"), QStringLiteral("repo-oss")}};
        SearchOptions out;
        QString error;

        // 実行 : オプションを解析する
        const bool accepted = parseSearchOptions(options, out, error);

        // 確認 : 指定した値がすべて反映されることを確認する
        QVERIFY2(accepted, qPrintable(error));
        QCOMPARE(out.query, QStringLiteral("libzypp"));
        QCOMPARE(out.flags, 7);
        QCOMPARE(static_cast<int>(out.match), static_cast<int>(SearchMatchMode::Glob));
        QCOMPARE(out.offset, 5u);
        QCOMPARE(out.limit, 25u);
        QVERIFY(out.installedOnly);
        QVERIFY(out.caseSensitive);
        QCOMPARE(out.repoAlias, QStringLiteral("repo-oss"));
    }

    /** @brief caseSensitiveの真偽がそのまま反映されることを検証する */
    void parsesCaseSensitive()
    {
        // 準備 : 真偽それぞれを指定する
        SearchOptions out;
        QString error;

        // 実行 : 大文字小文字を区別しない指定を解析する
        const bool insensitive = parseSearchOptions(
            {{QStringLiteral("query"), QStringLiteral("Qz")},
             {QStringLiteral("caseSensitive"), false}}, out, error);

        // 確認 : 既定と同じく区別しない設定になることを確認する
        QVERIFY2(insensitive, qPrintable(error));
        QVERIFY(!out.caseSensitive);

        // 実行 : 大文字小文字を区別する指定を解析する
        const bool sensitive = parseSearchOptions(
            {{QStringLiteral("query"), QStringLiteral("Qz")},
             {QStringLiteral("caseSensitive"), true}}, out, error);

        // 確認 : 区別する設定になることを確認する
        QVERIFY2(sensitive, qPrintable(error));
        QVERIFY(out.caseSensitive);
    }

    /** @brief 一度に指定できる検索フラグの上限を検証する */
    void validatesFlagsBoundary()
    {
        // 準備 : 全ビットの和と、その1つ上を指定する
        SearchOptions out;
        QString error;

        // 実行 : 上限値255を解析する
        const bool accepted = parseSearchOptions(
            {{QStringLiteral("query"), QStringLiteral("a")},
             {QStringLiteral("flags"), 255}}, out, error);

        // 確認 : 受理されフラグが保持されることを確認する
        QVERIFY2(accepted, qPrintable(error));
        QCOMPARE(out.flags, 255);

        // 実行 : 上限を超える256を解析する
        const bool rejected = parseSearchOptions(
            {{QStringLiteral("query"), QStringLiteral("a")},
             {QStringLiteral("flags"), 256}}, out, error);

        // 確認 : 拒否されることを確認する
        QVERIFY(!rejected);
        QVERIFY(!error.isEmpty());
    }

    /** @brief bool以外のcaseSensitiveは固有のエラーで拒否されることを検証する */
    void rejectsNonBooleanCaseSensitive()
    {
        // 準備 : 整数のcaseSensitiveを指定する
        const QVariantMap options{{QStringLiteral("query"), QStringLiteral("a")},
                                  {QStringLiteral("caseSensitive"), 1}};
        SearchOptions out;
        QString error;

        // 実行 : オプションを解析する
        const bool accepted = parseSearchOptions(options, out, error);

        // 確認 : メッセージがbool要求であることを確認する
        QVERIFY(!accepted);
        QCOMPARE(error, QStringLiteral("Option 'caseSensitive' must be a boolean"));
    }

    /** @brief 未知の前方一致別名は候補一覧を示すエラーで拒否されることを検証する */
    void rejectsStartswithMatchAlias()
    {
        // 準備 : prefixではなくstartswithを指定する
        const QVariantMap options{{QStringLiteral("query"), QStringLiteral("qz-a")},
                                  {QStringLiteral("match"), QStringLiteral("startswith")}};
        SearchOptions out;
        QString error;

        // 実行 : オプションを解析する
        const bool accepted = parseSearchOptions(options, out, error);

        // 確認 : 拒否され、エラーにprefixが含まれることを確認する
        QVERIFY(!accepted);
        QVERIFY2(error.contains(QStringLiteral("prefix")), qPrintable(error));
        qInfo() << "startswith rejection message:" << error;
    }

    /** @brief 妥当なPOSIX拡張正規表現を受理することを検証する */
    void acceptsValidPosixEre()
    {
        // 準備 : 妥当なPOSIX拡張正規表現を指定する
        const QVariantMap options{{QStringLiteral("query"), QStringLiteral("^lib.*-devel$")},
                                  {QStringLiteral("match"), QStringLiteral("regex")}};
        SearchOptions out;
        QString error;

        // 実行 : オプションを解析する
        const bool accepted = parseSearchOptions(options, out, error);

        // 確認 : 受理され正規表現モードになることを確認する
        QVERIFY2(accepted, qPrintable(error));
        QCOMPARE(out.query, QStringLiteral("^lib.*-devel$"));
        QCOMPARE(static_cast<int>(out.match), static_cast<int>(SearchMatchMode::Regex));
    }

    /** @brief acceptsBoundedRepetitionの入力データを提供する */
    void acceptsBoundedRepetition_data()
    {
        QTest::addColumn<QString>("query");

        QTest::newRow("small-bounds") << QStringLiteral("[0-9]{1,3}\\.[0-9]{1,3}");
        QTest::newRow("brace-in-bracket") << QStringLiteral("[{]x[}]");
        QTest::newRow("class-then-bound") << QStringLiteral("[[:digit:]]{2}");
        QTest::newRow("bound-at-limit") << QStringLiteral("a{1000}");
        QTest::newRow("escaped-brace") << QStringLiteral("a\\{99999\\}");
        QTest::newRow("quantified-plain-group") << QStringLiteral("(ab)+c*");
        QTest::newRow("sequential-bounds") << QStringLiteral("a{30}b{40}");
        QTest::newRow("quantifier-inside-unquantified-group") << QStringLiteral("(lib|libx)-devel.*");
        QTest::newRow("quantifier-char-in-bracket") << QStringLiteral("[+*?]+");
    }

    /** @brief 範囲付き繰り返しを含む正規表現を受理することを検証する */
    void acceptsBoundedRepetition()
    {
        // 準備 : 範囲付き繰り返しを含むクエリを受け取る
        QFETCH(QString, query);
        const QVariantMap options{{QStringLiteral("query"), query},
                                  {QStringLiteral("match"), QStringLiteral("regex")}};
        SearchOptions out;
        QString error;

        // 実行 : オプションを解析する
        const bool accepted = parseSearchOptions(options, out, error);

        // 確認 : エラーなく受理されることを確認する
        QVERIFY2(accepted, qPrintable(error));
    }

    /** @brief rejectsInvalidOptionsの入力データを提供する */
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
            << QVariantMap{{QStringLiteral("query"), QStringLiteral("a")}, {QStringLiteral("flags"), 256}};
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
        QTest::newRow("case-sensitive-wrong-type")
            << QVariantMap{{QStringLiteral("query"), QStringLiteral("a")},
                           {QStringLiteral("caseSensitive"), 0}};
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
        QTest::newRow("regex-posix-unmatched-brace")
            << QVariantMap{{QStringLiteral("query"), QStringLiteral("a{1")},
                           {QStringLiteral("match"), QStringLiteral("regex")}};
        QTest::newRow("regex-posix-unmatched-paren")
            << QVariantMap{{QStringLiteral("query"), QStringLiteral("(")},
                           {QStringLiteral("match"), QStringLiteral("regex")}};
        QTest::newRow("regex-pcre-only-inline-flag")
            << QVariantMap{{QStringLiteral("query"), QStringLiteral("(?i)abc")},
                           {QStringLiteral("match"), QStringLiteral("regex")}};
        QTest::newRow("regex-nested-repetition")
            << QVariantMap{{QStringLiteral("query"), QStringLiteral("(a{32}){32}")},
                           {QStringLiteral("match"), QStringLiteral("regex")}};
        QTest::newRow("regex-huge-nested-repetition")
            << QVariantMap{{QStringLiteral("query"), QStringLiteral("(a{32767}){32767}")},
                           {QStringLiteral("match"), QStringLiteral("regex")}};
        QTest::newRow("regex-open-bound-over-limit")
            << QVariantMap{{QStringLiteral("query"), QStringLiteral("a{1001,}")},
                           {QStringLiteral("match"), QStringLiteral("regex")}};
        QTest::newRow("regex-nested-plus-groups")
            << QVariantMap{{QStringLiteral("query"), QString(20, QLatin1Char('(')) + QStringLiteral("a")
                                                         + QStringLiteral(")+").repeated(20)},
                           {QStringLiteral("match"), QStringLiteral("regex")}};
        QTest::newRow("regex-consecutive-plus")
            << QVariantMap{{QStringLiteral("query"), QStringLiteral("a") + QString(50, QLatin1Char('+'))},
                           {QStringLiteral("match"), QStringLiteral("regex")}};
        QTest::newRow("regex-quantified-quantified-group")
            << QVariantMap{{QStringLiteral("query"), QStringLiteral("(a+)+")},
                           {QStringLiteral("match"), QStringLiteral("regex")}};
        QTest::newRow("regex-star-then-bound")
            << QVariantMap{{QStringLiteral("query"), QStringLiteral("a*{2}")},
                           {QStringLiteral("match"), QStringLiteral("regex")}};
        QTest::newRow("regex-sequential-bounds-over-total")
            << QVariantMap{{QStringLiteral("query"), QStringLiteral("a{600}b{600}")},
                           {QStringLiteral("match"), QStringLiteral("regex")}};
        QTest::newRow("regex-back-reference")
            << QVariantMap{{QStringLiteral("query"), QStringLiteral("(a*)*\\1")},
                           {QStringLiteral("match"), QStringLiteral("regex")}};
    }

    /** @brief 不正な検索オプションをエラーとして拒否することを検証する */
    void rejectsInvalidOptions()
    {
        // 準備 : 不正なオプションを入力する
        QFETCH(QVariantMap, options);
        SearchOptions out;
        QString error;

        // 実行 : オプションを解析する
        const bool accepted = parseSearchOptions(options, out, error);

        // 確認 : 拒否されエラーメッセージが設定されることを確認する
        QVERIFY(!accepted);
        QVERIFY(!error.isEmpty());
    }
};

QTEST_GUILESS_MAIN(SearchOptionsTest)
#include "tst_SearchOptions.moc"
