/**
 * @file tst_SearchModeDetect.cpp
 * @brief detectSearchMode()の判定順序と問い合わせ文字列の整形を検証する
 *
 * 空入力、完全一致、正規表現、ワイルドカード、部分一致の各入力に対する
 * モード名と問い合わせ文字列をデータ駆動で確認する
 * バックエンドやlibzyppには依存せず、root権限も不要
 *
 * 入力 : テストデータの検索文字列
 * 出力 : QTestの合否
 * 依存 : Qt Test、SearchModeDetect
 */
#include <QtTest>
#include "SearchModeDetect.h"

using namespace qZypper;

/**
 * @class SearchModeDetectTest
 * @brief detectSearchMode()の入力ごとの判定結果を検証する
 */
class SearchModeDetectTest : public QObject
{
    Q_OBJECT

private slots:
    /** @brief 入力ごとのモード名と問い合わせ文字列を検証する */
    void detectsMode_data();
    /** @brief データ行ごとにdetectSearchMode()を実行して結果を検証する */
    void detectsMode();
};

void SearchModeDetectTest::detectsMode_data()
{
    QTest::addColumn<QString>("input");
    QTest::addColumn<QString>("expectedMode");
    QTest::addColumn<QString>("expectedQuery");

    QTest::newRow("empty") << QString() << QStringLiteral("none") << QString();
    QTest::newRow("spaces only") << QStringLiteral("   ") << QStringLiteral("none") << QString();
    QTest::newRow("exact prefix") << QStringLiteral("=vim") << QStringLiteral("exact")
                                  << QStringLiteral("vim");
    QTest::newRow("exact with spaces") << QStringLiteral("= vim ") << QStringLiteral("exact")
                                       << QStringLiteral("vim");
    QTest::newRow("exact only") << QStringLiteral("=") << QStringLiteral("exact") << QString();
    QTest::newRow("exact only with spaces") << QStringLiteral("=  ") << QStringLiteral("exact")
                                            << QString();
    QTest::newRow("regex anchor and star") << QStringLiteral("^lib.*") << QStringLiteral("regex")
                                           << QStringLiteral("^lib.*");
    QTest::newRow("glob stars") << QStringLiteral("*nvidia*") << QStringLiteral("glob")
                                << QStringLiteral("*nvidia*");
    QTest::newRow("glob question") << QStringLiteral("lib?") << QStringLiteral("glob")
                                   << QStringLiteral("lib?");
    QTest::newRow("libstdc++ is substring") << QStringLiteral("libstdc++")
                                            << QStringLiteral("substring")
                                            << QStringLiteral("libstdc++");
    QTest::newRow("g++ is substring") << QStringLiteral("g++") << QStringLiteral("substring")
                                      << QStringLiteral("g++");
    QTest::newRow("regex pipe") << QStringLiteral("a|b") << QStringLiteral("regex")
                                << QStringLiteral("a|b");
    QTest::newRow("regex dot plus") << QStringLiteral("foo.+bar") << QStringLiteral("regex")
                                    << QStringLiteral("foo.+bar");
    QTest::newRow("substring dash") << QStringLiteral("kernel-default")
                                    << QStringLiteral("substring")
                                    << QStringLiteral("kernel-default");
    QTest::newRow("substring trimmed") << QStringLiteral("  vim ") << QStringLiteral("substring")
                                       << QStringLiteral("vim");
    QTest::newRow("regex char class") << QStringLiteral("lib[0-9]") << QStringLiteral("regex")
                                      << QStringLiteral("lib[0-9]");
    QTest::newRow("regex backslash") << QStringLiteral("c:\\x") << QStringLiteral("regex")
                                     << QStringLiteral("c:\\x");
}

void SearchModeDetectTest::detectsMode()
{
    QFETCH(QString, input);
    QFETCH(QString, expectedMode);
    QFETCH(QString, expectedQuery);

    // 実行 : 入力文字列から検索モードを判定する
    const DetectedSearch detected = detectSearchMode(input);

    // 確認 : モード名と問い合わせ文字列が期待どおりになる
    QCOMPARE(detected.mode, expectedMode);
    QCOMPARE(detected.query, expectedQuery);
}

QTEST_GUILESS_MAIN(SearchModeDetectTest)

#include "tst_SearchModeDetect.moc"
