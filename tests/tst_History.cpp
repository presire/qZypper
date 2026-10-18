#include "ZyppHistory.h"

#include <zypp/HistoryLogData.h>
#include <zypp/parser/HistoryLogReader.h>

#include <QFile>
#include <QTemporaryDir>
#include <QtTest>

#include <atomic>
#include <vector>

using namespace qZypper;

class HistoryTest : public QObject {
    Q_OBJECT

    const std::atomic<bool> m_noCancel{false};

    /// 日付昇順に count 件の install 行を書いた履歴ファイルを作る。
    static std::string writeHistory(const QTemporaryDir &dir, int count)
    {
        const QString path = dir.filePath(QStringLiteral("history"));
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly))
            return {};
        for (int i = 0; i < count; ++i) {
            const QString line = QStringLiteral("2024-01-%1 10:00:00|install|pkg%2|1.0-1|x86_64|root@host|repo-oss|abc|\n")
                                     .arg(i + 1, 2, 10, QLatin1Char('0'))
                                     .arg(i);
            file.write(line.toUtf8());
        }
        return path.toStdString();
    }

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
        QTest::newRow("offset-over-window")
            << QVariantMap{{"offset", static_cast<quint32>(kMaxHistoryWindow)}};
        QTest::newRow("offset-plus-limit-over-window")
            << QVariantMap{{"offset", static_cast<quint32>(kMaxHistoryWindow - 10)}, {"limit", 11}};
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

    void keepsOnlyNewestWindowWithStableTiebreak()
    {
        // Given: offset+limit = 3 の有界コレクタへファイル順に 5 件追加する
        BoundedHistoryCollector collector(1, 2);
        const auto add = [&collector](qint64 timestamp, const QString &name) {
            HistoryEntry entry;
            entry.timestamp = timestamp;
            entry.name = name;
            collector.add(entry);
        };
        add(100, QStringLiteral("a"));
        add(300, QStringLiteral("b"));
        add(300, QStringLiteral("c"));
        add(200, QStringLiteral("d"));
        add(400, QStringLiteral("e"));

        // When
        const std::vector<HistoryEntry> taken = collector.take();

        // Then: 保持件数は offset+limit に制限される
        QCOMPARE(collector.capacity(), std::size_t(3));
        QCOMPARE(collector.size(), std::size_t(3));
        // 新しい順 (同時刻はファイル出現順) の上位 3 件から offset=1 件読み飛ばす
        QCOMPARE(taken.size(), std::size_t(2));
        QCOMPARE(taken[0].name, QStringLiteral("b"));
        QCOMPARE(taken[0].timestamp, qint64(300));
        QCOMPARE(taken[1].name, QStringLiteral("c"));
        QCOMPARE(taken[1].timestamp, qint64(300));
    }

    void boundsRetainedEntriesToCapacity()
    {
        // Given: offset=0, limit=2 -> 容量 2 のコレクタへ 100 件追加する
        BoundedHistoryCollector collector(0, 2);
        for (qint64 timestamp = 1; timestamp <= 100; ++timestamp) {
            HistoryEntry entry;
            entry.timestamp = timestamp;
            entry.name = QString::number(timestamp);
            collector.add(entry);
        }

        // When
        const std::vector<HistoryEntry> taken = collector.take();

        // Then: 保持は容量内に収まり、最新 2 件のみが新しい順で返る
        QCOMPARE(collector.size(), std::size_t(2));
        QCOMPARE(taken.size(), std::size_t(2));
        QCOMPARE(taken[0].timestamp, qint64(100));
        QCOMPARE(taken[1].timestamp, qint64(99));
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

    void readsNewestEntriesWithinLimits()
    {
        // Given
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const std::string path = writeHistory(dir, 5);
        HistoryOptions options;
        options.limit = 2;
        std::vector<HistoryEntry> entries;
        std::string error;

        // When
        const bool ok = readHistoryFile(path, options, m_noCancel, {}, entries, error);

        // Then
        QVERIFY2(ok, error.c_str());
        QCOMPARE(entries.size(), std::size_t(2));
        QCOMPARE(entries[0].name, QStringLiteral("pkg4"));
        QCOMPARE(entries[1].name, QStringLiteral("pkg3"));
    }

    void rejectsHistoryOverEntryLimitInsteadOfReturningOldEntries()
    {
        // Given: the newest entry lies just after the scan limit
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const std::string path = writeHistory(dir, 4);
        HistoryOptions options;
        options.limit = 1;
        HistoryReadLimits limits;
        limits.maxEntries = 3;
        std::vector<HistoryEntry> entries;
        std::string error;

        // When
        const bool ok = readHistoryFile(path, options, m_noCancel, {}, entries, error, limits);

        // Then
        QVERIFY(!ok);
        QVERIFY(entries.empty());
        QVERIFY(QString::fromStdString(error).contains(QStringLiteral("too many entries")));
    }

    void rejectsOversizedHistoryEvenWhenFilterMatchesNothing()
    {
        // Given
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const std::string path = writeHistory(dir, 50);
        HistoryOptions options;
        options.actions = {QStringLiteral("radd")};
        HistoryReadLimits limits;
        limits.maxBytes = 256;
        std::vector<HistoryEntry> entries;
        std::string error;

        // When
        const bool ok = readHistoryFile(path, options, m_noCancel, {}, entries, error, limits);

        // Then
        QVERIFY(!ok);
        QVERIFY(entries.empty());
        QVERIFY(QString::fromStdString(error).contains(QStringLiteral("too large")));
    }

    void treatsMissingHistoryAsEmpty()
    {
        // Given
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const std::string path = dir.filePath(QStringLiteral("missing")).toStdString();
        std::vector<HistoryEntry> entries;
        std::string error;

        // When
        const bool ok = readHistoryFile(path, HistoryOptions(), m_noCancel, {}, entries, error);

        // Then
        QVERIFY2(ok, error.c_str());
        QVERIFY(entries.empty());
    }

    void stopsReadingWhenCancelled()
    {
        // Given
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const std::string path = writeHistory(dir, 5);
        const std::atomic<bool> cancel{true};
        std::vector<HistoryEntry> entries;
        std::string error;

        // When
        const bool ok = readHistoryFile(path, HistoryOptions(), cancel, {}, entries, error);

        // Then
        QVERIFY(!ok);
        QVERIFY(entries.empty());
        QCOMPARE(QString::fromStdString(error), QStringLiteral("Operation cancelled"));
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
