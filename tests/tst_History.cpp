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
 * @brief Zypp履歴の読み込み、オプション解析、シリアライズを検証する
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
        // 確認 : 既定の件数・範囲・操作種別になる
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
        const QVariantMap map{{"since", 100000}, {"until", 200000}, {"limit", 50},
                              {"offset", 10}, {"actions", QStringList{"install", "remove"}}};
        HistoryOptions options;
        std::string error;
        // 実行 : マップを解析する
        const bool accepted = parseHistoryOptions(map, options, error);
        // 確認 : 指定した値がそのまま反映される
        QVERIFY(accepted);
        // since/untilはD-Bus上ではミリ秒、HistoryOptionsでは秒
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
        QTest::newRow("since-after-until") << QVariantMap{{"since", 200000}, {"until", 100000}};
        QTest::newRow("since-after-until-by-one-second") << QVariantMap{{"since", 200000}, {"until", 199000}};
        QTest::newRow("offset-wraps-quint32") << QVariantMap{{"offset", qlonglong(1) << 32}};
        QTest::newRow("limit-wraps-quint32") << QVariantMap{{"limit", (qlonglong(1) << 32) + 1}};
        QTest::newRow("negative-offset") << QVariantMap{{"offset", -1}};
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
        // 確認 : 拒否されエラーメッセージが設定される
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
        // 確認 : installとrepo-addのエントリを解析できる
        QCOMPARE(entries.size(), std::size_t(10));
        QCOMPARE(entries[2]->action(), zypp::HistoryActionID::INSTALL);
        const auto install = zypp::dynamic_pointer_cast<zypp::HistoryLogDataInstall>(entries[2]);
        QVERIFY(install);
        QCOMPARE(QString::fromStdString(install->name()), QStringLiteral("bash"));
        QCOMPARE(entries[6]->action(), zypp::HistoryActionID::REPO_ADD);
        const auto repoAdd = zypp::dynamic_pointer_cast<zypp::HistoryLogDataRepoAdd>(entries[6]);
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

        // 確認 : 新しい2件が返る
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

        // 確認 : 拒否されエントリが残らず件数超過のエラーになる
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

        // 確認 : 一致がなくても拒否され容量超過のエラーになる
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

        // 確認 : 成功扱いで空になる
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

        // 確認 : 中断され取消しのエラーになる
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
        // 確認 : 各キーへ値が設定される
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
        // 確認 : 空の任意項目のキーが含まれない
        QVERIFY(!map.contains(QStringLiteral("requestedBy")));
        QVERIFY(!map.contains(QStringLiteral("userdata")));
        QVERIFY(!map.contains(QStringLiteral("command")));
    }
    /** @brief since/untilのミリ秒指定が秒へ変換され、履歴取得の絞り込みに反映されることを検証する */
    void convertsMillisecondRangeToSecondsWhenReading()
    {
        // 準備 : 2024-01-16の0時 (ローカル時刻) 以降だけを求める範囲をミリ秒で指定する
        const qint64 sinceMs = QDateTime(QDate(2024, 1, 16), QTime(0, 0)).toMSecsSinceEpoch();
        HistoryOptions options;
        std::string error;
        QVERIFY(parseHistoryOptions(QVariantMap{{"since", sinceMs}}, options, error));
        QCOMPARE(options.since, sinceMs / 1000);
        std::vector<HistoryEntry> entries;
        // 実行 : サンプル履歴を読み込む
        QVERIFY(readHistoryFile(QZYPPER_HISTORY_SAMPLE, options, m_noCancel, {}, entries, error));
        // 確認 : 2024-01-15以前のinstallは除外され、残りは新しい順になる
        QVERIFY(!entries.empty());
        for (const HistoryEntry &entry : entries) {
            QVERIFY2(entry.timestamp >= options.since, "entry older than since");
        }

        QCOMPARE(entries.front().action, QStringLiteral("command"));
    }

    /** @brief 最初のcommandより前のイベントが仮想グループになり、空グループが除かれ、新しい順に並ぶことを検証する */
    void groupsInFileOrderWithArtificialLeadingGroup()
    {
        // 準備 : 絞り込みなしのグループ取得オプションを用意する
        HistoryGroupOptions options;
        std::string error;
        QVERIFY(parseHistoryGroupOptions({}, options, error));
        HistoryGroupResult result;
        // 実行 : サンプル履歴をグループ化する
        QVERIFY2(readHistoryGroups(QZYPPER_HISTORY_SAMPLE, options, m_noCancel, {}, result, error), error.c_str());
        // 確認 : 末尾のrefreshグループ (イベントなし) は除かれ、新しい順に4グループ
        QCOMPARE(result.groups.size(), std::size_t(4));
        QCOMPARE(result.totalGroups, std::size_t(4));
        QVERIFY(!result.hasMore);
        QVERIFY(!result.truncated);
        QCOMPARE(result.groups[0].command, QStringLiteral("zypper addrepo my-repo"));
        QCOMPARE(result.groups[1].command, QStringLiteral("zypper remove nano"));
        QCOMPARE(result.groups[2].command, QStringLiteral("zypper install bash"));
        QCOMPARE(result.groups[3].command, QStringLiteral("<unknown>"));
        QVERIFY(result.groups[3].artificial);
        QVERIFY(!result.groups[0].artificial);
        QCOMPARE(result.groups[0].requestedBy, QStringLiteral("root@host"));
        QCOMPARE(result.groups[0].userdata, QStringLiteral("trans3"));
        QCOMPARE(result.groups[0].eventCount, 3u);
        QCOMPARE(result.groups[1].removeCount, 1u);
        QCOMPARE(result.groups[2].installCount, 1u);
        QCOMPARE(result.groups[3].installCount, 1u);
        QCOMPARE(result.groups[2].events.front().name, QStringLiteral("bash"));
        // 確認 : リポジトリ変更イベントにURL・旧URL・旧エイリアスが補われる
        const std::vector<HistoryEntry> &repoEvents = result.groups[0].events;
        QCOMPARE(repoEvents[0].action, QStringLiteral("radd"));
        QCOMPARE(repoEvents[0].url, QStringLiteral("https://example.com/repo"));
        QCOMPARE(repoEvents[1].action, QStringLiteral("url"));
        QCOMPARE(repoEvents[1].url, QStringLiteral("https://example.org/repo"));
        QCOMPARE(repoEvents[1].oldUrl, QStringLiteral("https://example.com/repo"));
        QCOMPARE(repoEvents[2].action, QStringLiteral("rchange"));
        QCOMPARE(repoEvents[2].oldAlias, QStringLiteral("my-repo"));
        QCOMPARE(repoEvents[2].name, QStringLiteral("my-repo2"));
        // 確認 : D-Bus用マップが入れ子のeventsとid・フラグを持つ
        const QVariantMap map = result.groups[3].toVariantMap();
        QVERIFY(map.value(QStringLiteral("artificial")).toBool());
        QCOMPARE(map.value(QStringLiteral("events")).toList().size(), 1);
        QVERIFY(!map.value(QStringLiteral("id")).toString().isEmpty());
    }

    /** @brief 絞り込み後に子イベントが無くなったグループが除かれることを検証する */
    void dropsGroupsEmptiedByFilters()
    {
        // 準備 : removeだけ、および存在しない文字列の検索を指定する
        std::string error;
        HistoryGroupOptions removeOnly;
        QVERIFY(parseHistoryGroupOptions(QVariantMap{{"actions", QStringList{"remove"}}}, removeOnly, error));
        HistoryGroupOptions textOnly;
        QVERIFY(parseHistoryGroupOptions(QVariantMap{{"text", "BASH"}}, textOnly, error));
        HistoryGroupOptions nothing;
        QVERIFY(parseHistoryGroupOptions(QVariantMap{{"text", "no-such-package"}}, nothing, error));
        HistoryGroupOptions commandOnly;
        QVERIFY(parseHistoryGroupOptions(QVariantMap{{"actions", QStringList{"command"}}}, commandOnly, error));
        HistoryGroupResult a;
        HistoryGroupResult b;
        HistoryGroupResult c;
        HistoryGroupResult d;
        // 実行 : それぞれのオプションでグループ化する
        QVERIFY(readHistoryGroups(QZYPPER_HISTORY_SAMPLE, removeOnly, m_noCancel, {}, a, error));
        QVERIFY(readHistoryGroups(QZYPPER_HISTORY_SAMPLE, textOnly, m_noCancel, {}, b, error));
        QVERIFY(readHistoryGroups(QZYPPER_HISTORY_SAMPLE, nothing, m_noCancel, {}, c, error));
        QVERIFY(readHistoryGroups(QZYPPER_HISTORY_SAMPLE, commandOnly, m_noCancel, {}, d, error));
        // 確認 : 一致する子イベントを持つグループだけが残る (command行は境界であり子イベントではない)
        QCOMPARE(a.groups.size(), std::size_t(1));
        QCOMPARE(a.groups[0].command, QStringLiteral("zypper remove nano"));
        QCOMPARE(a.groups[0].events.size(), std::size_t(1));
        QCOMPARE(b.groups.size(), std::size_t(1));
        QCOMPARE(b.groups[0].events.front().name, QStringLiteral("bash"));
        QVERIFY(c.groups.empty());
        QCOMPARE(c.totalGroups, std::size_t(0));
        QVERIFY(d.groups.empty());
    }

    /** @brief 日付範囲の絞り込みを子イベントへ適用し、期間外のグループを除くことを検証する */
    void appliesTimeRangeToChildEvents()
    {
        // 準備 : 2024-01-16の1日だけをミリ秒で指定する
        const qint64 sinceMs = QDateTime(QDate(2024, 1, 16), QTime(0, 0)).toMSecsSinceEpoch();
        const qint64 untilMs = QDateTime(QDate(2024, 1, 16), QTime(23, 59, 59)).toMSecsSinceEpoch();
        HistoryGroupOptions options;
        std::string error;
        QVERIFY(parseHistoryGroupOptions(QVariantMap{{"since", sinceMs}, {"until", untilMs}}, options, error));
        HistoryGroupResult result;
        // 実行 : グループ化する
        QVERIFY(readHistoryGroups(QZYPPER_HISTORY_SAMPLE, options, m_noCancel, {}, result, error));
        // 確認 : その日のremoveだけが残る
        QCOMPARE(result.groups.size(), std::size_t(1));
        QCOMPARE(result.groups[0].events.front().name, QStringLiteral("nano"));
    }

    /** @brief グループ単位のoffset/limitで新しい順にページングできることを検証する */
    void pagesGroupsNewestFirst()
    {
        // 準備 : 先頭2グループ、続く2グループ、範囲外のoffsetを用意する
        std::string error;
        HistoryGroupOptions first;
        HistoryGroupOptions second;
        HistoryGroupOptions beyond;
        QVERIFY(parseHistoryGroupOptions(QVariantMap{{"limit", 2}}, first, error));
        QVERIFY(parseHistoryGroupOptions(QVariantMap{{"limit", 2}, {"offset", 2}}, second, error));
        QVERIFY(parseHistoryGroupOptions(QVariantMap{{"limit", 2}, {"offset", 50}}, beyond, error));
        HistoryGroupResult a;
        HistoryGroupResult b;
        HistoryGroupResult c;
        // 実行 : 各ページを読み込む
        QVERIFY(readHistoryGroups(QZYPPER_HISTORY_SAMPLE, first, m_noCancel, {}, a, error));
        QVERIFY(readHistoryGroups(QZYPPER_HISTORY_SAMPLE, second, m_noCancel, {}, b, error));
        QVERIFY(readHistoryGroups(QZYPPER_HISTORY_SAMPLE, beyond, m_noCancel, {}, c, error));
        // 確認 : グループ単位で新しい順に分かれ、totalGroupsは全体の件数、hasMoreは続きの有無を示す
        QCOMPARE(a.groups.size(), std::size_t(2));
        QCOMPARE(a.groups[0].command, QStringLiteral("zypper addrepo my-repo"));
        QCOMPARE(a.groups[1].command, QStringLiteral("zypper remove nano"));
        QCOMPARE(a.totalGroups, std::size_t(4));
        QVERIFY(a.hasMore);
        QCOMPARE(b.groups.size(), std::size_t(2));
        QCOMPARE(b.groups[0].command, QStringLiteral("zypper install bash"));
        QCOMPARE(b.groups[1].command, QStringLiteral("<unknown>"));
        QVERIFY(!b.hasMore);
        QVERIFY(c.groups.empty());
        QVERIFY(!c.hasMore);
        QCOMPARE(c.totalGroups, std::size_t(4));
    }

    /** @brief 同時刻のグループがファイル順番号で区別され、後ろのグループが新しい扱いになることを検証する */
    void distinguishesGroupsWithSameTimestamp()
    {
        // 準備 : 同一時刻のcommandが2つ連続する履歴を作る
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = dir.filePath(QStringLiteral("history"));
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("2024-02-01 10:00:00|command|root@host|first|u1\n"
                   "2024-02-01 10:00:00|install|a|1|x86_64|root@host|r|c|u1\n"
                   "2024-02-01 10:00:00|command|root@host|second|u2\n"
                   "2024-02-01 10:00:00|install|b|1|x86_64|root@host|r|c|u2\n");
        file.close();
        HistoryGroupOptions options;
        std::string error;
        QVERIFY(parseHistoryGroupOptions({}, options, error));
        HistoryGroupResult result;
        // 実行 : グループ化する
        QVERIFY(readHistoryGroups(path.toStdString(), options, m_noCancel, {}, result, error));
        // 確認 : 2グループは別のid・seqを持ち、後ろのグループが先に並ぶ
        QCOMPARE(result.groups.size(), std::size_t(2));
        QCOMPARE(result.groups[0].command, QStringLiteral("second"));
        QCOMPARE(result.groups[1].command, QStringLiteral("first"));
        QCOMPARE(result.groups[0].events.front().name, QStringLiteral("b"));
        QCOMPARE(result.groups[1].events.front().name, QStringLiteral("a"));
        QVERIFY(result.groups[0].seq != result.groups[1].seq);
        QVERIFY(result.groups[0].toVariantMap().value(QStringLiteral("id")) !=
                result.groups[1].toVariantMap().value(QStringLiteral("id")));
    }

    /** @brief グループ内・応答全体のイベント上限を超えた分が黙って落ちず、件数とフラグで示されることを検証する */
    void flagsTruncationWhenEventLimitsAreExceeded()
    {
        // 準備 : 5件のinstallを持つ新しいグループと、3件のinstallを持つ古いグループを作る
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = dir.filePath(QStringLiteral("history"));
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("2024-02-01 10:00:00|command|root@host|old|u1\n");
        for (int i = 0; i < 3; ++i) {
            file.write(QStringLiteral("2024-02-01 10:00:0%1|install|o%1|1|x86_64|root@host|r|c|u1\n").arg(i + 1).toUtf8());
        }


        file.write("2024-02-02 10:00:00|command|root@host|new|u2\n");
        for (int i = 0; i < 5; ++i) {
            file.write(QStringLiteral("2024-02-02 10:00:0%1|install|n%1|1|x86_64|root@host|r|c|u2\n").arg(i + 1).toUtf8());
        }

        file.close();
        HistoryGroupOptions options;
        std::string error;
        QVERIFY(parseHistoryGroupOptions({}, options, error));
        HistoryGroupLimits limits;
        limits.maxEventsPerGroup = 3;
        limits.maxTotalEvents = 5;
        HistoryGroupResult result;
        // 実行 : 上限付きでグループ化する
        QVERIFY(readHistoryGroups(path.toStdString(), options, m_noCancel, {}, result, error, limits));
        // 確認 : 新しいグループは3件、古いグループは残り予算の2件だけを返し、省略分はカウントとフラグに残る
        QCOMPARE(result.groups.size(), std::size_t(2));
        QCOMPARE(result.groups[0].events.size(), std::size_t(3));
        QCOMPARE(result.groups[0].eventCount, 5u);
        QCOMPARE(result.groups[0].hiddenCount(), 2u);
        QCOMPARE(result.groups[1].events.size(), std::size_t(2));
        QCOMPARE(result.groups[1].hiddenCount(), 1u);
        QCOMPARE(result.hiddenEvents, quint64(3));
        QVERIFY(result.truncated);
        const QVariantMap map = result.groups[0].toVariantMap();
        QVERIFY(map.value(QStringLiteral("truncated")).toBool());
        QCOMPARE(map.value(QStringLiteral("hiddenCount")).toUInt(), 2u);
        QCOMPARE(map.value(QStringLiteral("eventCount")).toUInt(), 5u);
    }

    /** @brief 履歴の走査件数・容量・取消しの上限がグループ取得でもエラーになることを検証する */
    void rejectsGroupReadOverScanLimitsAndCancellation()
    {
        // 準備 : 5件のinstall行を持つ履歴と、走査上限2件のオプションを用意する
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const std::string path = writeHistory(dir, 5);
        QVERIFY(!path.empty());
        HistoryGroupOptions options;
        std::string error;
        QVERIFY(parseHistoryGroupOptions({}, options, error));
        HistoryGroupLimits entryLimit;
        entryLimit.maxEntries = 2;
        HistoryGroupLimits byteLimit;
        byteLimit.maxBytes = 10;
        const std::atomic<bool> cancelled{true};
        HistoryGroupResult result;
        // 実行 / 確認 : 件数超過・容量超過・取消しはいずれも失敗として返る
        QVERIFY(!readHistoryGroups(path, options, m_noCancel, {}, result, error, entryLimit));
        QVERIFY(error.find("too many entries") != std::string::npos);
        QVERIFY(!readHistoryGroups(path, options, m_noCancel, {}, result, error, byteLimit));
        QVERIFY(error.find("too large") != std::string::npos);
        QVERIFY(!readHistoryGroups(path, options, cancelled, {}, result, error));
        QCOMPARE(error, std::string("Operation cancelled"));
    }

    /** @brief 存在しない履歴ファイルは空の結果になることを検証する */
    void treatsMissingHistoryGroupsAsEmpty()
    {
        // 準備 : 存在しないパスを指定する
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        HistoryGroupOptions options;
        std::string error;
        QVERIFY(parseHistoryGroupOptions({}, options, error));
        HistoryGroupResult result;
        // 実行 : グループ取得する
        const bool ok = readHistoryGroups(dir.filePath(QStringLiteral("none")).toStdString(), options, m_noCancel, {}, result, error);
        // 確認 : 成功し空になる
        QVERIFY(ok);
        QVERIFY(result.groups.empty());
    }

    /** @brief rejectsInvalidHistoryGroupOptionsの入力データを提供する */
    void rejectsInvalidHistoryGroupOptions_data()
    {
        QTest::addColumn<QVariantMap>("map");
        QTest::newRow("limit-zero") << QVariantMap{{"limit", 0}};
        QTest::newRow("limit-over-group-max") << QVariantMap{{"limit", static_cast<qlonglong>(kMaxHistoryGroupLimit) + 1}};
        QTest::newRow("limit-wraps-quint32") << QVariantMap{{"limit", (qlonglong(1) << 32) + 5}};
        QTest::newRow("offset-wraps-quint32") << QVariantMap{{"offset", (qlonglong(1) << 32) + 5}};
        QTest::newRow("offset-negative") << QVariantMap{{"offset", -1}};
        QTest::newRow("offset-over-window") << QVariantMap{{"offset", static_cast<qlonglong>(kMaxHistoryWindow) + 1}};
        QTest::newRow("limit-not-number") << QVariantMap{{"limit", "ten"}};
        QTest::newRow("text-not-string") << QVariantMap{{"text", 5}};
        QTest::newRow("text-too-long") << QVariantMap{{"text", QString(kMaxHistoryTextBytes + 1, QLatin1Char('x'))}};
        QTest::newRow("negative-since") << QVariantMap{{"since", -1}};
        QTest::newRow("since-after-until") << QVariantMap{{"since", 200000}, {"until", 100000}};
        QTest::newRow("unknown-action") << QVariantMap{{"actions", QStringList{"bogus"}}};
    }

    /** @brief 不正なグループ取得オプションを拒否することを検証する */
    void rejectsInvalidHistoryGroupOptions()
    {
        // 準備 : 範囲外・型違いの値を含むマップを入力する
        QFETCH(QVariantMap, map);
        HistoryGroupOptions options;
        std::string error;
        // 実行 : 解析する
        const bool accepted = parseHistoryGroupOptions(map, options, error);
        // 確認 : 拒否されエラーメッセージが設定される
        QVERIFY(!accepted);
        QVERIFY(!error.empty());
    }

    /** @brief 妥当なグループ取得オプションを反映し、既定値を持つことを検証する */
    void parsesHistoryGroupOptions()
    {
        // 準備 : 全項目を指定したマップと空のマップを用意する
        std::string error;
        HistoryGroupOptions defaults;
        HistoryGroupOptions parsed;
        QVERIFY(parseHistoryGroupOptions({}, defaults, error));
        // 実行 : 解析する
        QVERIFY(parseHistoryGroupOptions(QVariantMap{{"limit", 20}, {"offset", 3}, {"text", "  vim "},
                                                     {"since", 100000}, {"until", 200000},
                                                     {"actions", QStringList{"install", "alias"}}}, parsed, error));
        // 確認 : 既定値・trim・ミリ秒から秒への変換が反映される
        QCOMPARE(defaults.limit, kDefaultHistoryGroupLimit);
        QCOMPARE(defaults.offset, 0u);
        QVERIFY(defaults.text.isEmpty());
        QCOMPARE(parsed.limit, 20u);
        QCOMPARE(parsed.offset, 3u);
        QCOMPARE(parsed.text, QStringLiteral("vim"));
        QCOMPARE(parsed.filter.since, 100);
        QCOMPARE(parsed.filter.until, 200);
        QCOMPARE(parsed.filter.actions.size(), 2);
    }

    /** @brief パスワード付きURLの履歴からパスワードを除くことを検証する */
    void masksPasswordsInRepositoryUrls()
    {
        // 準備 : パスワード付きURLでリポジトリを追加した履歴を作る
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = dir.filePath(QStringLiteral("history"));
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("2024-02-01 10:00:00|command|root@host|zypper ar|u1\n"
                   "2024-02-01 10:00:01|radd|priv|https://user:secret@example.com/repo|u1\n");
        file.close();
        HistoryGroupOptions options;
        std::string error;
        QVERIFY(parseHistoryGroupOptions({}, options, error));
        HistoryGroupResult result;
        // 実行 : グループ化する
        QVERIFY(readHistoryGroups(path.toStdString(), options, m_noCancel, {}, result, error));
        // 確認 : 返却URLにパスワードが含まれない
        QCOMPARE(result.groups.size(), std::size_t(1));
        QVERIFY(!result.groups[0].events.front().url.contains(QLatin1String("secret")));
        QVERIFY(result.groups[0].events.front().url.contains(QLatin1String("example.com")));
    }
};

QTEST_GUILESS_MAIN(HistoryTest)
#include "tst_History.moc"
