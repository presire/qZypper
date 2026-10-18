#include "TransactionSummary.h"

#include <QtTest>

using namespace qZypper;

namespace {

/**
 * @brief テスト用の版比較 (辞書順)。
 * @param oldVersion 旧バージョン
 * @param newVersion 新バージョン
 * @return 旧 &gt; 新 なら正
 */
int compareEdition(const QString &oldVersion, const QString &newVersion)
{
    return QString::compare(oldVersion, newVersion);
}

/**
 * @brief 遷移前後情報を組み立てる。
 * @param hasOld 旧が存在するか
 * @param hasNew 新が存在するか
 * @return 遷移前後情報
 */
ChangeTuple makeTuple(bool hasOld, bool hasNew)
{
    ChangeTuple tuple;
    tuple.hasOld = hasOld;
    tuple.hasNew = hasNew;
    tuple.oldVersion = QStringLiteral("1.0");
    tuple.oldVendor = QStringLiteral("openSUSE");
    tuple.oldArch = QStringLiteral("x86_64");
    tuple.newVersion = QStringLiteral("1.0");
    tuple.newVendor = QStringLiteral("openSUSE");
    tuple.newArch = QStringLiteral("x86_64");
    return tuple;
}

} // namespace

class TransactionSummaryTest : public QObject {
    Q_OBJECT
private slots:
    void classifiesInstallAndRemove()
    {
        // Given
        const ChangeTuple installTuple = makeTuple(false, true);
        const ChangeTuple removeTuple = makeTuple(true, false);

        // When
        const ChangeRisk installRisk = classifyChange(installTuple, compareEdition);
        const ChangeRisk removeRisk = classifyChange(removeTuple, compareEdition);

        // Then
        QVERIFY(installRisk.install);
        QVERIFY(!installRisk.remove && !installRisk.update && !installRisk.downgrade);
        QVERIFY(!installRisk.vendorChange && !installRisk.archChange);
        QVERIFY(removeRisk.remove);
        QVERIFY(!removeRisk.install && !removeRisk.update && !removeRisk.downgrade);
    }

    void classifiesUpdateAndDowngrade()
    {
        // Given: 版が上がる場合と下がる場合 (ベンダ・アーキは同一)
        ChangeTuple upgrade = makeTuple(true, true);
        upgrade.oldVersion = QStringLiteral("1.0");
        upgrade.newVersion = QStringLiteral("2.0");
        ChangeTuple downgrade = makeTuple(true, true);
        downgrade.oldVersion = QStringLiteral("2.0");
        downgrade.newVersion = QStringLiteral("1.0");

        // When
        const ChangeRisk upgradeRisk = classifyChange(upgrade, compareEdition);
        const ChangeRisk downgradeRisk = classifyChange(downgrade, compareEdition);

        // Then
        QVERIFY(upgradeRisk.update);
        QVERIFY(!upgradeRisk.downgrade);
        QVERIFY(downgradeRisk.downgrade);
        QVERIFY(!downgradeRisk.update);
    }

    void classifiesVendorAndArchChange()
    {
        // Given: 版は上がるがベンダとアーキが変わる
        ChangeTuple tuple = makeTuple(true, true);
        tuple.oldVersion = QStringLiteral("1.0");
        tuple.newVersion = QStringLiteral("2.0");
        tuple.oldVendor = QStringLiteral("VendorA");
        tuple.newVendor = QStringLiteral("VendorB");
        tuple.oldArch = QStringLiteral("x86_64");
        tuple.newArch = QStringLiteral("aarch64");

        // When
        const ChangeRisk risk = classifyChange(tuple, compareEdition);

        // Then
        QVERIFY(risk.update);
        QVERIFY(risk.vendorChange);
        QVERIFY(risk.archChange);
        QVERIFY(!risk.downgrade && !risk.install && !risk.remove);
    }

    void classifiesNewWithoutOld()
    {
        // Given: 旧も新も無い (解消済み) 遷移
        const ChangeTuple tuple = makeTuple(false, false);

        // When
        const ChangeRisk risk = classifyChange(tuple, compareEdition);

        // Then
        QVERIFY(!risk.install && !risk.remove && !risk.update && !risk.downgrade);
        QVERIFY(!risk.vendorChange && !risk.archChange);
    }

    void countsDowngradeWithVendorChangeOncePerCategory()
    {
        // Given: ベンダ変更を伴うダウングレード
        ChangeEntry entry;
        entry.name = QStringLiteral("pkg");
        entry.oldVersion = QStringLiteral("2.0");
        entry.newVersion = QStringLiteral("1.0");
        entry.oldVendor = QStringLiteral("VendorA");
        entry.newVendor = QStringLiteral("VendorB");
        entry.oldArch = QStringLiteral("x86_64");
        entry.newArch = QStringLiteral("x86_64");
        entry.repoAlias = QStringLiteral("repo-oss");
        ChangeTuple tuple = makeTuple(true, true);
        tuple.oldVersion = QStringLiteral("2.0");
        tuple.newVersion = QStringLiteral("1.0");
        tuple.oldVendor = QStringLiteral("VendorA");
        tuple.newVendor = QStringLiteral("VendorB");
        entry.risk = classifyChange(tuple, compareEdition);

        TransactionTally tally;

        // When
        accumulateChange(tally, entry);

        // Then
        QCOMPARE(tally.downgradeCount, 1);
        QCOMPARE(tally.vendorChangeCount, 1);
        QCOMPARE(tally.archChangeCount, 0);
        QCOMPARE(tally.updateCount, 0);
        QCOMPARE(tally.installCount, 0);
        QCOMPARE(tally.removeCount, 0);
        QCOMPARE(tally.downgrades.size(), std::size_t(1));
        QCOMPARE(tally.vendorChanges.size(), std::size_t(1));
        QCOMPARE(tally.archChanges.size(), std::size_t(0));
        QCOMPARE(tally.removals.size(), std::size_t(0));
        QCOMPARE(tally.downgrades.at(0).name, QStringLiteral("pkg"));
    }

    void accumulatesSeparateInstallAndRemoval()
    {
        // Given: 新規インストールと削除を 1 件ずつ
        ChangeEntry installEntry;
        installEntry.name = QStringLiteral("added");
        installEntry.risk = classifyChange(makeTuple(false, true), compareEdition);
        ChangeEntry removeEntry;
        removeEntry.name = QStringLiteral("dropped");
        removeEntry.risk = classifyChange(makeTuple(true, false), compareEdition);

        TransactionTally tally;

        // When
        accumulateChange(tally, installEntry);
        accumulateChange(tally, removeEntry);

        // Then
        QCOMPARE(tally.installCount, 1);
        QCOMPARE(tally.removeCount, 1);
        QCOMPARE(tally.removals.size(), std::size_t(1));
        QCOMPARE(tally.removals.at(0).name, QStringLiteral("dropped"));
    }

    void mapsTallyToAllSummaryKeys()
    {
        // Given
        TransactionTally tally;
        tally.installCount = 2;
        tally.updateCount = 3;
        tally.removeCount = 4;
        tally.downgradeCount = 5;
        tally.vendorChangeCount = 6;
        tally.archChangeCount = 7;
        tally.downloadSize = 4242;
        tally.installedSizeChange = -1024;

        // When
        const QVariantMap map = tallyToVariantMap(tally);

        // Then
        const QStringList expectedKeys{
            QStringLiteral("installCount"), QStringLiteral("updateCount"),
            QStringLiteral("removeCount"), QStringLiteral("downgradeCount"),
            QStringLiteral("vendorChangeCount"), QStringLiteral("archChangeCount"),
            QStringLiteral("downloadSize"), QStringLiteral("installedSizeChange"),
            QStringLiteral("removals"), QStringLiteral("downgrades"),
            QStringLiteral("vendorChanges"), QStringLiteral("archChanges")};
        for (const QString &key : expectedKeys)
            QVERIFY2(map.contains(key), qPrintable(key));

        QCOMPARE(map.value(QStringLiteral("installCount")).toInt(), 2);
        QCOMPARE(map.value(QStringLiteral("updateCount")).toInt(), 3);
        QCOMPARE(map.value(QStringLiteral("removeCount")).toInt(), 4);
        QCOMPARE(map.value(QStringLiteral("downgradeCount")).toInt(), 5);
        QCOMPARE(map.value(QStringLiteral("vendorChangeCount")).toInt(), 6);
        QCOMPARE(map.value(QStringLiteral("archChangeCount")).toInt(), 7);
        QCOMPARE(map.value(QStringLiteral("downloadSize")).toLongLong(), 4242LL);
        QCOMPARE(map.value(QStringLiteral("installedSizeChange")).toLongLong(), -1024LL);
        QCOMPARE(map.value(QStringLiteral("removals")).toList().size(), 0);
    }

    void mapsChangeEntryFields()
    {
        // Given
        ChangeEntry entry;
        entry.name = QStringLiteral("pkg");
        entry.oldVersion = QStringLiteral("1.0");
        entry.newVersion = QStringLiteral("2.0");
        entry.oldVendor = QStringLiteral("VendorA");
        entry.newVendor = QStringLiteral("VendorB");
        entry.oldArch = QStringLiteral("x86_64");
        entry.newArch = QStringLiteral("aarch64");
        entry.repoAlias = QStringLiteral("repo-oss");

        // When
        const QVariantMap map = changeEntryToVariantMap(entry);

        // Then
        QCOMPARE(map.value(QStringLiteral("name")).toString(), QStringLiteral("pkg"));
        QCOMPARE(map.value(QStringLiteral("oldVersion")).toString(), QStringLiteral("1.0"));
        QCOMPARE(map.value(QStringLiteral("newVersion")).toString(), QStringLiteral("2.0"));
        QCOMPARE(map.value(QStringLiteral("oldVendor")).toString(), QStringLiteral("VendorA"));
        QCOMPARE(map.value(QStringLiteral("newVendor")).toString(), QStringLiteral("VendorB"));
        QCOMPARE(map.value(QStringLiteral("oldArch")).toString(), QStringLiteral("x86_64"));
        QCOMPARE(map.value(QStringLiteral("newArch")).toString(), QStringLiteral("aarch64"));
        QCOMPARE(map.value(QStringLiteral("repoAlias")).toString(), QStringLiteral("repo-oss"));
    }
};

QTEST_GUILESS_MAIN(TransactionSummaryTest)
#include "tst_TransactionSummary.moc"
