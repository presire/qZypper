#include <QFile>
#include <QTemporaryDir>
#include <QtTest>
#include <atomic>
#include <vector>
#include <zypp/HistoryLogData.h>
#include <zypp/parser/HistoryLogReader.h>
#include "ZyppHistory.h"

using namespace qZypper;

/**
 * @class HistoryTest
 * @brief Zypp履歴の読み込みとオプション解析・シリアライズを検証する
 *
 * 履歴オプションの検証、有界コレクタの窓取り出し、履歴ファイル読み込みの制限 (件数・容量・取消し)、サンプルファイルの解析、
 * HistoryEntryのVariantMap変換を対象にする
 *
 * root権限は不要
 */
class HistoryTest : public QObject {
    Q_OBJECT

    const std::atomic<bool> m_noCancel{false}; ///< 取消しを行わない読み込みで使うダミーフラグ

    /**
     * @brief 日付昇順にcount件のinstall行を持つ履歴ファイルを作成する
     * @param dir 書き込み先の一時ディレクトリ
     * @param count 生成するinstall行の件数
     * @return 作成した履歴ファイルのパス (失敗時は空文字列)
     */
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
    /** @brief 空のマップでは履歴オプションの既定値が設定されることを検証する */
    void usesDefaultHistoryOptionsWhenMapIsEmpty()
    {
        // 準備 : オプションを指定しない
        HistoryOptions options;
        std::string error;
        // 実行 : 空のマップを解析する
        const bool accepted = parseHistoryOptions(QVariantMap{}, options, error);
        // 確認 : 既定の件数・範囲・操作種別になることを確認する
        QVERIFY(accepted);
        QCOMPARE(options.limit, 1000u);
        QCOMPARE(options.offset, 0u);
        QCOMPARE(options.since, 0);
        QCOMPARE(options.until, 0);
        QVERIFY(options.actions.isEmpty());
    }

    /** @brief 妥当な履歴オプションをすべて解析して反映することを検証する */
    void parsesValidHistoryOptions()
    {
        // 準備 : 期間・件数・操作種別を指定したマップを用意する
        const QVariantMap map{{"since", 100}, {"until", 200}, {"limit", 50},
                              {"offset", 10}, {"actions", QStringList{"install", "remove"}}};
        HistoryOptions options;
        std::string error;
        // 実行 : マップを解析する
        const bool accepted = parseHistoryOptions(map, options, error);
        // 確認 : 指定した値がそのまま反映されることを確認する
        QVERIFY(accepted);
        QCOMPARE(options.since, 100);
        QCOMPARE(options.until, 200);
        QCOMPARE(options.limit, 50u);
        QCOMPARE(options.offset, 10u);
        const QStringList expectedActions{QStringLiteral("install"), QStringLiteral("remove")};
        QCOMPARE(options.actions, expectedActions);
    }

    /** @brief rejectsInvalidHistoryOptionsの入力データを提供する */
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
        QTest::newRow("offset-over-window") << QVariantMap{{"offset", static_cast<quint32>(kMaxHistoryWindow)}};
        QTest::newRow("offset-plus-limit-over-window") << QVariantMap{{"offset", static_cast<quint32>(kMaxHistoryWindow - 10)}, {"limit", 11}};
    }

    /** @brief 不正な履歴オプションをエラーとして拒否することを検証する */
    void rejectsInvalidHistoryOptions()
    {
        // 準備 : 範囲外や未対応の操作種別を含むマップを入力する
        QFETCH(QVariantMap, map);
        HistoryOptions options;
        std::string error;
        // 実行 : 不正なマップを解析する
        const bool accepted = parseHistoryOptions(map, options, error);
        // 確認 : 拒否されエラーメッセージが設定されることを確認する
        QVERIFY(!accepted);
        QVERIFY(!error.empty());
    }

    /** @brief 有界コレクタが新しい窓だけを保持し、同時刻は出現順で安定させることを検証する */
    void keepsOnlyNewestWindowWithStableTiebreak()
    {
        // 準備 : offset+limit = 3の有界コレクタへファイル順に5件追加する
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

        // 実行 : 収集した履歴を取り出す
        const std::vector<HistoryEntry> taken = collector.take();

        // 確認 : 保持件数はoffset+limitに制限される
        QCOMPARE(collector.capacity(), std::size_t(3));
        QCOMPARE(collector.size(), std::size_t(3));
        // 新しい順 (同時刻はファイル出現順) の上位3件からoffset = 1件読み飛ばす
        QCOMPARE(taken.size(), std::size_t(2));
        QCOMPARE(taken[0].name, QStringLiteral("b"));
        QCOMPARE(taken[0].timestamp, qint64(300));
        QCOMPARE(taken[1].name, QStringLiteral("c"));
        QCOMPARE(taken[1].timestamp, qint64(300));
    }

    /** @brief 有界コレクタが保持件数を容量内に収めることを検証する */
    void boundsRetainedEntriesToCapacity()
    {
        // 準備 : offset = 0 かつ limit = 2の容量2のコレクタへ100件追加する
        BoundedHistoryCollector collector(0, 2);
        for (qint64 timestamp = 1; timestamp <= 100; ++timestamp) {
            HistoryEntry entry;
            entry.timestamp = timestamp;
            entry.name = QString::number(timestamp);
            collector.add(entry);
        }

        // 実行 : 収集した履歴を取り出す
        const std::vector<HistoryEntry> taken = collector.take();

        // 確認 : 保持は容量内に収まり、最新2件のみが新しい順で返る
        QCOMPARE(collector.size(), std::size_t(2));
        QCOMPARE(taken.size(), std::size_t(2));
        QCOMPARE(taken[0].timestamp, qint64(100));
        QCOMPARE(taken[1].timestamp, qint64(99));
    }

    /** @brief root権限なしでサンプル履歴ファイルを解析できることを検証する */
    void readsSampleHistoryWithoutRoot()
    {
        // 準備 : サンプル履歴を読み取るコールバックを用意する
        std::vector<zypp::HistoryLogData::Ptr> entries;
        zypp::parser::HistoryLogReader reader(
            QZYPPER_HISTORY_SAMPLE,
            zypp::parser::HistoryLogReader::Options(zypp::parser::HistoryLogReader::IGNORE_INVALID_ITEMS),
            [&entries](const zypp::HistoryLogData::Ptr &entry) {
                entries.push_back(entry);
                return true;
            });
        // 実行 : 履歴ファイルを読み込む
        reader.readAll();
        // 確認 : installとrepo-addのエントリを解析できることを確認する
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

    /** @brief 件数制限内で新しい履歴エントリから読み出すことを検証する */
    void readsNewestEntriesWithinLimits()
    {
        // 準備 : 5件の履歴ファイルとlimit = 2のオプションを用意する
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const std::string path = writeHistory(dir, 5);
        HistoryOptions options;
        options.limit = 2;
        std::vector<HistoryEntry> entries;
        std::string error;

        // 実行 : 履歴ファイルを読み込む
        const bool ok = readHistoryFile(path, options, m_noCancel, {}, entries, error);

        // 確認 : 新しい2件が返ることを確認する
        QVERIFY2(ok, error.c_str());
        QCOMPARE(entries.size(), std::size_t(2));
        QCOMPARE(entries[0].name, QStringLiteral("pkg4"));
        QCOMPARE(entries[1].name, QStringLiteral("pkg3"));
    }

    /** @brief 走査上限を超える履歴は古い分を返さずエラーにすることを検証する */
    void rejectsHistoryOverEntryLimitInsteadOfReturningOldEntries()
    {
        // 準備 : 最新のエントリが走査上限の直後に位置する状態にする
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const std::string path = writeHistory(dir, 4);
        HistoryOptions options;
        options.limit = 1;
        HistoryReadLimits limits;
        limits.maxEntries = 3;
        std::vector<HistoryEntry> entries;
        std::string error;

        // 実行 : 走査上限付きで履歴ファイルを読み込む
        const bool ok = readHistoryFile(path, options, m_noCancel, {}, entries, error, limits);

        // 確認 : 拒否されエントリが残らず件数超過のエラーになることを確認する
        QVERIFY(!ok);
        QVERIFY(entries.empty());
        QVERIFY(QString::fromStdString(error).contains(QStringLiteral("too many entries")));
    }

    /** @brief フィルタ一致がなくても容量超過の履歴ファイルを拒否することを検証する */
    void rejectsOversizedHistoryEvenWhenFilterMatchesNothing()
    {
        // 準備 : フィルタに一致しない操作種別と小さな容量上限を用意する
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const std::string path = writeHistory(dir, 50);
        HistoryOptions options;
        options.actions = {QStringLiteral("radd")};
        HistoryReadLimits limits;
        limits.maxBytes = 256;
        std::vector<HistoryEntry> entries;
        std::string error;

        // 実行 : 容量上限付きで履歴ファイルを読み込む
        const bool ok = readHistoryFile(path, options, m_noCancel, {}, entries, error, limits);

        // 確認 : 一致がなくても拒否され容量超過のエラーになることを確認する
        QVERIFY(!ok);
        QVERIFY(entries.empty());
        QVERIFY(QString::fromStdString(error).contains(QStringLiteral("too large")));
    }

    /** @brief 存在しない履歴ファイルを空として扱うことを検証する */
    void treatsMissingHistoryAsEmpty()
    {
        // 準備 : 存在しないパスを用意する
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const std::string path = dir.filePath(QStringLiteral("missing")).toStdString();
        std::vector<HistoryEntry> entries;
        std::string error;

        // 実行 : 存在しない履歴ファイルを読み込む
        const bool ok = readHistoryFile(path, HistoryOptions(), m_noCancel, {}, entries, error);

        // 確認 : 成功扱いで空になることを確認する
        QVERIFY2(ok, error.c_str());
        QVERIFY(entries.empty());
    }

    /** @brief 取消しフラグで履歴読み込みを中断することを検証する */
    void stopsReadingWhenCancelled()
    {
        // 準備 : 取消し済みのフラグを用意する
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const std::string path = writeHistory(dir, 5);
        const std::atomic<bool> cancel{true};
        std::vector<HistoryEntry> entries;
        std::string error;

        // 実行 : 取消しフラグ付きで履歴ファイルを読み込む
        const bool ok = readHistoryFile(path, HistoryOptions(), cancel, {}, entries, error);

        // 確認 : 中断され取消しのエラーになることを確認する
        QVERIFY(!ok);
        QVERIFY(entries.empty());
        QCOMPARE(QString::fromStdString(error), QStringLiteral("Operation cancelled"));
    }

    /** @brief HistoryEntryをVariantMapへ変換することを検証する */
    void serializesHistoryEntryToVariantMap()
    {
        // 準備 : 全項目を設定した履歴エントリを用意する
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
        // 実行 : VariantMapへ変換する
        const QVariantMap map = entry.toVariantMap();
        // 確認 : 各キーへ値が設定されることを確認する
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

    /** @brief 空の任意項目はVariantMapに含めないことを検証する */
    void omitsEmptyOptionalHistoryKeys()
    {
        // 準備 : 必須項目だけを設定した履歴エントリを用意する
        HistoryEntry entry;
        entry.timestamp = 1;
        entry.action = QStringLiteral("install");
        entry.name = QStringLiteral("bash");
        // 実行 : VariantMapへ変換する
        const QVariantMap map = entry.toVariantMap();
        // 確認 : 空の任意項目のキーが含まれないことを確認する
        QVERIFY(!map.contains(QStringLiteral("requestedBy")));
        QVERIFY(!map.contains(QStringLiteral("userdata")));
        QVERIFY(!map.contains(QStringLiteral("command")));
    }
};

QTEST_GUILESS_MAIN(HistoryTest)
#include "tst_History.moc"
