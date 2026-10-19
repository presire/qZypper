#include <QtTest>
#include "RepoFileParser.h"

using namespace qZypper;

namespace {

/**
 * @brief 常に妥当と判定する別名・URL検証関数 (未使用引数は無視する)
 * @return 常にtrue
 */
bool alwaysValid(const QString &, QString &)
{
    return true;
}

/**
 * @brief goodで始まる値だけを妥当とし他はエラーにする検証関数
 * @param value 検証する文字列
 * @param err 失敗時に理由を格納する出力先
 * @return 妥当ならtrue、不正ならfalse
 */
bool onlyGoodUrls(const QString &value, QString &err)
{
    if (value.startsWith(QLatin1String("good")))
        return true;
    err = QStringLiteral("bad url");
    return false;
}

constexpr std::size_t kCap = 64 * 1024; ///< 既定の解析サイズ上限

} // namespace

/**
 * @class RepoFileParserTest
 * @brief リポジトリ定義ファイルの解析と検証を検証する
 *
 * 複数セクションの解析、コメント・空行の無視、未知キーを含むセクションのスキップ、サイズ上限、URL検証、
 * セクション外キーや不正ヘッダのエラー、baseurl欠落や未対応type・mirrorlist・重複別名の拒否、セクションのVariantMap変換を対象にする
 */
class RepoFileParserTest : public QObject {
    Q_OBJECT
private slots:
    /** @brief 複数セクションを解析し各項目を読み取ることを検証する */
    void parsesMultipleSections();
    /** @brief コメント行と空行を無視することを検証する */
    void ignoresCommentsAndBlankLines();
    /** @brief 未知キーを含むセクションだけをスキップすることを検証する */
    void rejectsInvalidKeyPerSection();
    /** @brief サイズ上限を超える内容を拒否することを検証する */
    void enforcesSizeCap();
    /** @brief URL検証に失格したセクションをスキップすることを検証する */
    void rejectsBadUrlFromValidator();
    /** @brief セクション外のキーで解析を失敗させることを検証する */
    void failsOnKeyOutsideSection();
    /** @brief 閉じていないヘッダで解析を失敗させることを検証する */
    void failsOnMalformedHeader();
    /** @brief baseurlのないセクションをスキップすることを検証する */
    void skipsMissingBaseurl();
    /** @brief 未対応のtypeを持つセクションを拒否することを検証する */
    void rejectsUnsupportedType();
    /** @brief mirrorlistを持つセクションを拒否することを検証する */
    void rejectsMirrorlist();
    /** @brief 重複した別名を拒否し最初の定義を残すことを検証する */
    void rejectsDuplicateAlias();
    /** @brief セクションをVariantMapへ変換することを検証する */
    void mapsSectionToVariantMap();
};

void RepoFileParserTest::parsesMultipleSections()
{
    const QString content = QStringLiteral(
        "[repo-a]\n"
        "name=Repo A\n"
        "baseurl=https://a.example/\n"
        "enabled=1\n"
        "autorefresh=yes\n"
        "priority=50\n"
        "gpgcheck=1\n"
        "[repo-b]\n"
        "baseurl=https://b.example/\n");
    RepoFileParseResult out;
    QVERIFY(parseRepoFile(content, kCap, alwaysValid, alwaysValid, out));
    QVERIFY(out.ok);
    QCOMPARE(out.sections.size(), std::size_t(2));
    QCOMPARE(out.sections[0].alias, QStringLiteral("repo-a"));
    QCOMPARE(out.sections[0].name, QStringLiteral("Repo A"));
    QCOMPARE(out.sections[0].enabled, true);
    QCOMPARE(out.sections[0].autoRefresh, true);
    QCOMPARE(out.sections[0].priority, 50LL);
    QVERIFY(out.sections[0].gpgCheck.has_value());
    QVERIFY(*out.sections[0].gpgCheck);
    QCOMPARE(out.sections[1].alias, QStringLiteral("repo-b"));
    QCOMPARE(out.sections[1].name, QStringLiteral("repo-b"));
    QCOMPARE(out.sections[1].enabled, true);
}

void RepoFileParserTest::ignoresCommentsAndBlankLines()
{
    const QString content = QStringLiteral(
        "; leading comment\n"
        "# another comment\n"
        "\n"
        "[only]\n"
        "   # indented comment\n"
        "baseurl=https://only.example/\n");
    RepoFileParseResult out;
    QVERIFY(parseRepoFile(content, kCap, alwaysValid, alwaysValid, out));
    QCOMPARE(out.sections.size(), std::size_t(1));
    QCOMPARE(out.sections[0].alias, QStringLiteral("only"));
}

void RepoFileParserTest::rejectsInvalidKeyPerSection()
{
    const QString content = QStringLiteral(
        "[good]\n"
        "baseurl=https://good.example/\n"
        "[bad]\n"
        "baseurl=https://bad.example/\n"
        "bogus=1\n");
    RepoFileParseResult out;
    QVERIFY(parseRepoFile(content, kCap, alwaysValid, alwaysValid, out));
    QCOMPARE(out.sections.size(), std::size_t(1));
    QCOMPARE(out.sections[0].alias, QStringLiteral("good"));
    QCOMPARE(out.skipped.size(), std::size_t(1));
    QCOMPARE(out.skipped[0].first, QStringLiteral("bad"));
    QVERIFY(out.skipped[0].second.contains(QLatin1String("unknown key")));
}

void RepoFileParserTest::enforcesSizeCap()
{
    const QString content = QStringLiteral("[big]\nbaseurl=https://big.example/\n");
    RepoFileParseResult out;
    QVERIFY(!parseRepoFile(content, 8, alwaysValid, alwaysValid, out));
    QVERIFY(out.error.contains(QLatin1String("exceeds")));
}

void RepoFileParserTest::rejectsBadUrlFromValidator()
{
    const QString content = QStringLiteral("[r]\nbaseurl=evilvalue\n");
    RepoFileParseResult out;
    QVERIFY(parseRepoFile(content, kCap, alwaysValid, onlyGoodUrls, out));
    QCOMPARE(out.sections.size(), std::size_t(0));
    QCOMPARE(out.skipped.size(), std::size_t(1));
    QCOMPARE(out.skipped[0].second, QStringLiteral("bad url"));
}

void RepoFileParserTest::failsOnKeyOutsideSection()
{
    const QString content = QStringLiteral("baseurl=https://x/\n[r]\nbaseurl=https://x/\n");
    RepoFileParseResult out;
    QVERIFY(!parseRepoFile(content, kCap, alwaysValid, alwaysValid, out));
    QVERIFY(out.error.contains(QLatin1String("outside")));
}

void RepoFileParserTest::failsOnMalformedHeader()
{
    const QString content = QStringLiteral("[unterminated\nbaseurl=https://x/\n");
    RepoFileParseResult out;
    QVERIFY(!parseRepoFile(content, kCap, alwaysValid, alwaysValid, out));
    QVERIFY(out.error.contains(QLatin1String("malformed")));
}

void RepoFileParserTest::skipsMissingBaseurl()
{
    const QString content = QStringLiteral("[nourl]\nname=No Url\n");
    RepoFileParseResult out;
    QVERIFY(parseRepoFile(content, kCap, alwaysValid, alwaysValid, out));
    QCOMPARE(out.sections.size(), std::size_t(0));
    QCOMPARE(out.skipped.size(), std::size_t(1));
    QVERIFY(out.skipped[0].second.contains(QLatin1String("baseurl")));
}

void RepoFileParserTest::rejectsUnsupportedType()
{
    const QString content = QStringLiteral("[t]\nbaseurl=https://t/\ntype=banana\n");
    RepoFileParseResult out;
    QVERIFY(parseRepoFile(content, kCap, alwaysValid, alwaysValid, out));
    QCOMPARE(out.skipped.size(), std::size_t(1));
    QVERIFY(out.skipped[0].second.contains(QLatin1String("unsupported type")));
}

void RepoFileParserTest::rejectsMirrorlist()
{
    const QString content = QStringLiteral("[m]\nbaseurl=https://m/\nmirrorlist=https://list\n");
    RepoFileParseResult out;
    QVERIFY(parseRepoFile(content, kCap, alwaysValid, alwaysValid, out));
    QCOMPARE(out.skipped.size(), std::size_t(1));
    QVERIFY(out.skipped[0].second.contains(QLatin1String("not supported")));
}

void RepoFileParserTest::rejectsDuplicateAlias()
{
    const QString content = QStringLiteral(
        "[dup]\nbaseurl=https://a/\n"
        "[dup]\nbaseurl=https://b/\n");
    RepoFileParseResult out;
    QVERIFY(parseRepoFile(content, kCap, alwaysValid, alwaysValid, out));
    QCOMPARE(out.sections.size(), std::size_t(1));
    QCOMPARE(out.sections[0].url, QStringLiteral("https://a/"));
    QCOMPARE(out.skipped.size(), std::size_t(1));
    QVERIFY(out.skipped[0].second.contains(QLatin1String("duplicate")));
}

void RepoFileParserTest::mapsSectionToVariantMap()
{
    const QString content = QStringLiteral(
        "[mapped]\nname=Display\nbaseurl=https://map.example/\n"
        "priority=7\nkeepPackages=1\ngpgkey=https://map.example/key\n");
    RepoFileParseResult out;
    QVERIFY(parseRepoFile(content, kCap, alwaysValid, alwaysValid, out));
    QCOMPARE(out.sections.size(), std::size_t(1));
    const QVariantMap map = repoFileSectionToMap(out.sections[0]);
    QCOMPARE(map.value(QStringLiteral("alias")).toString(), QStringLiteral("mapped"));
    QCOMPARE(map.value(QStringLiteral("name")).toString(), QStringLiteral("Display"));
    QCOMPARE(map.value(QStringLiteral("url")).toString(), QStringLiteral("https://map.example/"));
    QCOMPARE(map.value(QStringLiteral("priority")).toLongLong(), 7LL);
    QCOMPARE(map.value(QStringLiteral("keepPackages")).toBool(), true);
    QVERIFY(!map.contains(QStringLiteral("type")));
}

QTEST_GUILESS_MAIN(RepoFileParserTest)
#include "tst_RepoFileParser.moc"
