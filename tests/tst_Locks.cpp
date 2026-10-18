#include "ZyppLocks.h"

#include <QtTest>

using namespace qZypper;

class LocksTest : public QObject {
    Q_OBJECT
private slots:
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

    void validatesLockMap()
    {
        // Given
        QFETCH(QVariantMap, map);
        QFETCH(bool, valid);
        LockInfo info;
        std::string error;
        // When
        const bool accepted = validateLockMap(map, info, error);
        // Then
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

    void roundTripsLockInfoThroughVariantMap()
    {
        // Given
        LockInfo info;
        info.name = QStringLiteral("bash");
        info.kind = QStringLiteral("package");
        info.match = QStringLiteral("exact");
        info.repoAlias = QStringLiteral("repo-oss");
        info.arch = QStringLiteral("x86_64");
        info.version = QStringLiteral("5.2-1.1");
        info.matchCount = 0;
        // When
        const QVariantMap map = info.toVariantMap();
        LockInfo parsed;
        std::string error;
        const bool accepted = validateLockMap(map, parsed, error);
        // Then
        QVERIFY(accepted);
        QCOMPARE(parsed.name, info.name);
        QCOMPARE(parsed.kind, info.kind);
        QCOMPARE(parsed.match, info.match);
        QCOMPARE(parsed.repoAlias, info.repoAlias);
        QCOMPARE(parsed.arch, info.arch);
        QCOMPARE(parsed.version, info.version);
    }

    void omitsEmptyCommentFromVariantMap()
    {
        // Given
        LockInfo info;
        info.name = QStringLiteral("bash");
        info.kind = QStringLiteral("package");
        info.match = QStringLiteral("exact");
        // When
        const QVariantMap map = info.toVariantMap();
        // Then
        QVERIFY(!map.contains(QStringLiteral("comment")));
        QVERIFY(map.contains(QStringLiteral("matchCount")));
    }
};

QTEST_GUILESS_MAIN(LocksTest)
#include "tst_Locks.moc"
