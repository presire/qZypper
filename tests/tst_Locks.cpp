#include <QtTest>
#include "ZyppLocks.h"

using namespace qZypper;

/**
 * @class LocksTest
 * @brief ロック情報のマップ検証とVariantMap往復変換を検証する
 *
 * 名前・種別・一致方法などの検証、不正入力の拒否、toVariantMapとvalidateLockMapを通した情報の保持、空コメント項目の省略を対象にする
 */
class LocksTest : public QObject {
    Q_OBJECT
private slots:
    /** @brief validatesLockMapの入力データを提供する */
    void validatesLockMap_data()
    {
        QTest::addColumn<QVariantMap>("map");
        QTest::addColumn<bool>("valid");
        QTest::addColumn<QString>("kind");
        QTest::addColumn<QString>("match");
        QTest::newRow("minimal") << QVariantMap{{"name", "bash"}} << true << "package" << "exact";
        QTest::newRow("glob-name") << QVariantMap{{"name", "libfoo*"}} << true << "package" << "glob";
        QTest::newRow("question-name") << QVariantMap{{"name", "libfoo?"}} << true << "package" << "glob";
        QTest::newRow("explicit-kind") << QVariantMap{{"name", "bash"}, {"kind", "pattern"}} << true << "pattern" << "exact";
        QTest::newRow("explicit-match") << QVariantMap{{"name", "bash"}, {"match", "regex"}} << true << "package" << "regex";
        QTest::newRow("glob-key") << QVariantMap{{"name", "bash"}, {"glob", "substring"}} << true << "package" << "substring";
        QTest::newRow("full") << QVariantMap{{"name", "bash"}, {"kind", "package"}, {"arch", "x86_64"}, {"version", "5.2-1.1"}, {"repoAlias", "repo-oss"}} << true << "package" << "exact";
        QTest::newRow("missing-name") << QVariantMap{} << false << QString() << QString();
        QTest::newRow("blank-name") << QVariantMap{{"name", "   "}} << false << QString() << QString();
        QTest::newRow("control-name") << QVariantMap{{"name", "bash\n[evil]"}} << false << QString() << QString();
        QTest::newRow("slash-name") << QVariantMap{{"name", "a/b"}} << false << QString() << QString();
        QTest::newRow("bad-kind") << QVariantMap{{"name", "bash"}, {"kind", "kernel"}} << false << QString() << QString();
        QTest::newRow("bad-match") << QVariantMap{{"name", "bash"}, {"match", "fuzzy"}} << false << QString() << QString();
        QTest::newRow("bad-alias") << QVariantMap{{"name", "bash"}, {"repoAlias", "../evil"}} << false << QString() << QString();
        QTest::newRow("bad-arch") << QVariantMap{{"name", "bash"}, {"arch", "x86\n64"}} << false << QString() << QString();
        QTest::newRow("bad-version") << QVariantMap{{"name", "bash"}, {"version", "5.2/1"}} << false << QString() << QString();
    }

    /** @brief ロック定義マップを検証し、種別と一致方法を決定することを検証する */
    void validatesLockMap()
    {
        // 準備 : 名前や種別を含むロック定義マップを入力する
        QFETCH(QVariantMap, map);
        QFETCH(bool, valid);
        LockInfo info;
        std::string error;
        // 実行 : マップを検証する
        const bool accepted = validateLockMap(map, info, error);
        // 確認 : 妥当性と種別・一致方法・エラーの有無を確認する
        QCOMPARE(accepted, valid);
        if (valid) {
            QFETCH(QString, kind);
            QFETCH(QString, match);
            QCOMPARE(info.kind, kind);
            QCOMPARE(info.match, match);
            QVERIFY(error.empty());
        } else {
            QVERIFY(!error.empty());
        }
    }

    /** @brief LockInfoをVariantMapへ変換して、再解析しても値が保たれることを検証する */
    void roundTripsLockInfoThroughVariantMap()
    {
        // 準備 : 全項目を設定したLockInfoを用意する
        LockInfo info;
        info.name = QStringLiteral("bash");
        info.kind = QStringLiteral("package");
        info.match = QStringLiteral("exact");
        info.repoAlias = QStringLiteral("repo-oss");
        info.arch = QStringLiteral("x86_64");
        info.version = QStringLiteral("5.2-1.1");
        info.matchCount = 0;
        // 実行 : VariantMapへ変換して再解析する
        const QVariantMap map = info.toVariantMap();
        LockInfo parsed;
        std::string error;
        const bool accepted = validateLockMap(map, parsed, error);
        // 確認 : 元の値が保持されることを確認する
        QVERIFY(accepted);
        QCOMPARE(parsed.name, info.name);
        QCOMPARE(parsed.kind, info.kind);
        QCOMPARE(parsed.match, info.match);
        QCOMPARE(parsed.repoAlias, info.repoAlias);
        QCOMPARE(parsed.arch, info.arch);
        QCOMPARE(parsed.version, info.version);
    }

    /** @brief 空のcomment項目は、VariantMapに含めないことを検証する */
    void omitsEmptyCommentFromVariantMap()
    {
        // 準備 : コメントを設定しないLockInfoを用意する
        LockInfo info;
        info.name = QStringLiteral("bash");
        info.kind = QStringLiteral("package");
        info.match = QStringLiteral("exact");
        // 実行 : VariantMapへ変換する
        const QVariantMap map = info.toVariantMap();
        // 確認 : commentキーが無く、matchCountキーは存在することを確認する
        QVERIFY(!map.contains(QStringLiteral("comment")));
        QVERIFY(map.contains(QStringLiteral("matchCount")));
    }
};

QTEST_GUILESS_MAIN(LocksTest)
#include "tst_Locks.moc"
