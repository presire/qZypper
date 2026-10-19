#include <QtTest>
#include <QTemporaryDir>
#include <QDir>
#include <QFile>
#include "SnapshotInfo.h"

using namespace qZypper;

namespace {

/**
 * @brief テスト用のinfo.xmlを組み立てる
 * @param num スナップショット番号
 * @param type 種類
 * @param description 説明
 * @return XMLバイト列
 */
QByteArray makeInfoXml(quint64 num, const QString &type, const QString &description)
{
    return QStringLiteral(
               "<?xml version=\"1.0\"?>\n"
               "<snapshot>\n"
               "  <num>%1</num>\n"
               "  <type>%2</type>\n"
               "  <description>%3</description>\n"
               "  <date>2026-01-02 03:04:05</date>\n"
               "</snapshot>\n")
        .arg(num)
        .arg(type, description)
        .toUtf8();
}

/**
 * @brief 一時ルートにスナップショット1件を書き出す
 * @param root スナップショットルート
 * @param num 番号
 * @param description 説明
 */
void writeSnapshot(const QString &root, quint64 num, const QString &description)
{
    QDir rootDir(root);
    rootDir.mkpath(QString::number(num));
    QFile file(rootDir.filePath(QString::number(num) + QStringLiteral("/info.xml")));
    if (file.open(QIODevice::WriteOnly))
        file.write(makeInfoXml(num, QStringLiteral("single"), description));
}

} // namespace

/**
 * @class SnapshotInfoTest
 * @brief スナップショット情報の解析と収集・差分・変換を検証する
 *
 * info.xmlの解析とnum欠落・0・不正XMLの拒否、最大番号の算出、差分の抽出、
 * ルート走査時の番号順ソートと不在時の空結果、VariantMap/Listへの変換を対象にする
 */
class SnapshotInfoTest : public QObject {
    Q_OBJECT
private slots:
    /** @brief 妥当なinfo.xmlを解析して各項目を読み取ることを検証する */
    void parsesValidInfoXml()
    {
        const auto entry = parseSnapshotInfoXml(makeInfoXml(42, QStringLiteral("pre"), QStringLiteral("zypp")));
        QVERIFY(entry.has_value());
        QCOMPARE(entry->num, quint64(42));
        QCOMPARE(entry->type, QStringLiteral("pre"));
        QCOMPARE(entry->description, QStringLiteral("zypp"));
        QCOMPARE(entry->date, QStringLiteral("2026-01-02 03:04:05"));
    }

    /** @brief num要素のないXMLを拒否することを検証する */
    void rejectsMissingNum()
    {
        const QByteArray xml = "<snapshot><type>pre</type></snapshot>";
        QVERIFY(!parseSnapshotInfoXml(xml).has_value());
    }

    /** @brief 番号0のスナップショットを拒否することを検証する */
    void rejectsZeroNum()
    {
        QVERIFY(!parseSnapshotInfoXml(makeInfoXml(0, QStringLiteral("post"), QStringLiteral("x"))).has_value());
    }

    /** @brief 閉じていない不正なXMLを拒否することを検証する */
    void rejectsMalformedXml()
    {
        const QByteArray xml = "<snapshot><num>5</num>";
        QVERIFY(!parseSnapshotInfoXml(xml).has_value());
    }

    /** @brief 最大のスナップショット番号を求め空では0を返すことを検証する */
    void maxSnapshotNumberWorks()
    {
        std::vector<SnapshotEntry> entries;
        entries.push_back(SnapshotEntry{3, QString(), QString(), QString()});
        entries.push_back(SnapshotEntry{17, QString(), QString(), QString()});
        entries.push_back(SnapshotEntry{9, QString(), QString(), QString()});
        QCOMPARE(maxSnapshotNumber(entries), quint64(17));
        QCOMPARE(maxSnapshotNumber({}), quint64(0));
    }

    /** @brief 前後の一覧から新規スナップショットの差分を抽出することを検証する */
    void newSnapshotsReturnsDiff()
    {
        std::vector<SnapshotEntry> before;
        before.push_back(SnapshotEntry{1, QStringLiteral("single"), QStringLiteral("old"), QString()});
        before.push_back(SnapshotEntry{2, QStringLiteral("pre"), QStringLiteral("pre"), QString()});

        std::vector<SnapshotEntry> after = before;
        after.push_back(SnapshotEntry{3, QStringLiteral("post"), QStringLiteral("post"), QString()});
        after.push_back(SnapshotEntry{4, QStringLiteral("single"), QStringLiteral("new"), QString()});

        const auto diff = newSnapshots(before, after);
        QCOMPARE(diff.size(), std::size_t(2));
        QCOMPARE(diff.at(0).num, quint64(3));
        QCOMPARE(diff.at(1).num, quint64(4));
    }

    /** @brief ルートを走査して番号順に並べることを検証する */
    void readSnapshotsListsSorted()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        writeSnapshot(dir.path(), 5, QStringLiteral("five"));
        writeSnapshot(dir.path(), 2, QStringLiteral("two"));
        writeSnapshot(dir.path(), 9, QStringLiteral("nine"));

        const auto entries = readSnapshots(dir.path());
        QCOMPARE(entries.size(), std::size_t(3));
        QCOMPARE(entries.at(0).num, quint64(2));
        QCOMPARE(entries.at(1).num, quint64(5));
        QCOMPARE(entries.at(2).num, quint64(9));
    }

    /** @brief ルートが存在しない場合は空を返すことを検証する */
    void readSnapshotsMissingRootIsEmpty()
    {
        QVERIFY(readSnapshots(QStringLiteral("/nonexistent/qzypper/snapshots")).empty());
    }

    /** @brief スナップショットをVariantMapとVariantListへ変換することを検証する */
    void variantMapConversion()
    {
        const SnapshotEntry entry{11, QStringLiteral("pre"), QStringLiteral("desc"), QStringLiteral("date")};
        const QVariantMap map = snapshotToVariantMap(entry);
        QCOMPARE(map.value(QStringLiteral("num")).toULongLong(), qulonglong(11));
        QCOMPARE(map.value(QStringLiteral("type")).toString(), QStringLiteral("pre"));

        const QVariantList list = snapshotsToVariantList({entry});
        QCOMPARE(list.size(), 1);
        QCOMPARE(list.first().toMap().value(QStringLiteral("description")).toString(), QStringLiteral("desc"));
    }
};

QTEST_APPLESS_MAIN(SnapshotInfoTest)
#include "tst_SnapshotInfo.moc"
