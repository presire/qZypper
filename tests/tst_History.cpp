#include "ZyppHistory.h"

#include <zypp/HistoryLogData.h>
#include <zypp/parser/HistoryLogReader.h>

#include <QtTest>

#include <vector>

using namespace qZypper;

class HistoryTest : public QObject {
    Q_OBJECT
private slots:
    void usesDefaultHistoryOptionsWhenMapIsEmpty()
    {
        // Given
        HistoryOptions options;
        std::string error;
        // When
        const bool accepted = parseHistoryOptions(QVariantMap{}, options, error);
        // Then
        QVERIFY(accepted);
        QCOMPARE(options.limit, 1000u);
        QCOMPARE(options.offset, 0u);
        QCOMPARE(options.since, 0);
        QCOMPARE(options.until, 0);
        QVERIFY(options.actions.isEmpty());
    }

    void parsesValidHistoryOptions()
    {
        // Given
        const QVariantMap map{{"since", 100}, {"until", 200}, {"limit", 50},
                              {"offset", 10}, {"actions", QStringList{"install", "remove"}}};
        HistoryOptions options;
        std::string error;
        // When
        const bool accepted = parseHistoryOptions(map, options, error);
        // Then
        QVERIFY(accepted);
        QCOMPARE(options.since, 100);
        QCOMPARE(options.until, 200);
        QCOMPARE(options.limit, 50u);
        QCOMPARE(options.offset, 10u);
        const QStringList expectedActions{QStringLiteral("install"), QStringLiteral("remove")};
        QCOMPARE(options.actions, expectedActions);
    }

    void rejectsInvalidHistoryOptions_data()
    {
        QTest::addColumn<QVariantMap>("map");
        QTest::newRow("limit-zero") << QVariantMap{{"limit", 0}};
        QTest::newRow("limit-over") << QVariantMap{{"limit", static_cast<quint32>(kMaxHistoryLimit + 1)}};
        QTest::newRow("negative-since") << QVariantMap{{"since", -1}};
        QTest::newRow("negative-until") << QVariantMap{{"until", -5}};
        QTest::newRow("since-after-until") << QVariantMap{{"since", 200}, {"until", 100}};
        QTest::newRow("unknown-action") << QVariantMap{{"actions", QStringList{"bogus"}}};
        QTest::newRow("actions-not-list") << QVariantMap{{"actions", "install"}};
    }

    void rejectsInvalidHistoryOptions()
    {
        // Given
        QFETCH(QVariantMap, map);
        HistoryOptions options;
        std::string error;
        // When
        const bool accepted = parseHistoryOptions(map, options, error);
        // Then
        QVERIFY(!accepted);
        QVERIFY(!error.empty());
    }

    void readsSampleHistoryWithoutRoot()
    {
        // Given
        std::vector<zypp::HistoryLogData::Ptr> entries;
        zypp::parser::HistoryLogReader reader(
            QZYPPER_HISTORY_SAMPLE,
            zypp::parser::HistoryLogReader::Options(zypp::parser::HistoryLogReader::IGNORE_INVALID_ITEMS),
            [&entries](const zypp::HistoryLogData::Ptr &entry) {
                entries.push_back(entry);
                return true;
            });
        // When
        reader.readAll();
        // Then
        QCOMPARE(entries.size(), std::size_t(3));
        QCOMPARE(entries[0]->action(), zypp::HistoryActionID::INSTALL);
        const auto install = zypp::dynamic_pointer_cast<zypp::HistoryLogDataInstall>(entries[0]);
        QVERIFY(install);
        QCOMPARE(QString::fromStdString(install->name()), QStringLiteral("bash"));
        QCOMPARE(entries[2]->action(), zypp::HistoryActionID::REPO_ADD);
        const auto repoAdd = zypp::dynamic_pointer_cast<zypp::HistoryLogDataRepoAdd>(entries[2]);
        QVERIFY(repoAdd);
        QCOMPARE(QString::fromStdString((*repoAdd)[zypp::HistoryLogDataRepoAdd::ALIAS_INDEX]),
                 QStringLiteral("my-repo"));
    }

    void serializesHistoryEntryToVariantMap()
    {
        // Given
        HistoryEntry entry;
        entry.timestamp = 1700000000;
        entry.action = QStringLiteral("install");
        entry.name = QStringLiteral("bash");
        entry.edition = QStringLiteral("5.2-1.1");
        entry.arch = QStringLiteral("x86_64");
        entry.repoAlias = QStringLiteral("repo-oss");
        entry.requestedBy = QStringLiteral("root@host");
        entry.userdata = QStringLiteral("trans1");
        entry.command = QStringLiteral("zypper install bash");
        // When
        const QVariantMap map = entry.toVariantMap();
        // Then
        QCOMPARE(map.value(QStringLiteral("timestamp")).toLongLong(), qint64(1700000000));
        QCOMPARE(map.value(QStringLiteral("action")).toString(), QStringLiteral("install"));
        QCOMPARE(map.value(QStringLiteral("name")).toString(), QStringLiteral("bash"));
        QCOMPARE(map.value(QStringLiteral("edition")).toString(), QStringLiteral("5.2-1.1"));
        QCOMPARE(map.value(QStringLiteral("arch")).toString(), QStringLiteral("x86_64"));
        QCOMPARE(map.value(QStringLiteral("repoAlias")).toString(), QStringLiteral("repo-oss"));
        QCOMPARE(map.value(QStringLiteral("requestedBy")).toString(), QStringLiteral("root@host"));
        QCOMPARE(map.value(QStringLiteral("userdata")).toString(), QStringLiteral("trans1"));
        QCOMPARE(map.value(QStringLiteral("command")).toString(), QStringLiteral("zypper install bash"));
    }

    void omitsEmptyOptionalHistoryKeys()
    {
        // Given
        HistoryEntry entry;
        entry.timestamp = 1;
        entry.action = QStringLiteral("install");
        entry.name = QStringLiteral("bash");
        // When
        const QVariantMap map = entry.toVariantMap();
        // Then
        QVERIFY(!map.contains(QStringLiteral("requestedBy")));
        QVERIFY(!map.contains(QStringLiteral("userdata")));
        QVERIFY(!map.contains(QStringLiteral("command")));
    }
};

QTEST_GUILESS_MAIN(HistoryTest)
#include "tst_History.moc"
