#include <QDir>
#include <QFile>
#include <QXmlStreamReader>
#include <algorithm>
#include <set>
#include "SnapshotInfo.h"

namespace qZypper {

/**
 * @brief info.xmlの1項目を解析する
 * @param xml XML文字列
 * @return SnapshotEntry、解析失敗時はstd::nullopt
 */
std::optional<SnapshotEntry> parseSnapshotInfoXml(const QByteArray &xml)
{
    SnapshotEntry entry;
    bool haveNum = false;
    bool malformed = false;

    QXmlStreamReader reader(xml);
    while (!reader.atEnd()) {
        reader.readNext();
        if (reader.isStartElement()) {
            const QString name = reader.name().toString();
            if (name == QStringLiteral("num")) {
                entry.num = reader.readElementText().toULongLong(&haveNum);
            }
            else if (name == QStringLiteral("type")) {
                entry.type = reader.readElementText();
            }
            else if (name == QStringLiteral("description")) {
                entry.description = reader.readElementText();
            }
            else if (name == QStringLiteral("date")) {
                entry.date = reader.readElementText();
            }
        }
    }

    if (reader.hasError() || malformed)
        return std::nullopt;
    if (!haveNum || entry.num == 0)
        return std::nullopt;
    return entry;
}

/**
 * @brief スナップショット番号の最大値を求める
 * @param snapshots 対象一覧
 * @return 最大番号 (空の場合は0)
 */
quint64 maxSnapshotNumber(const std::vector<SnapshotEntry> &snapshots)
{
    quint64 maxNum = 0;
    for (const auto &snapshot : snapshots)
        maxNum = std::max(maxNum, snapshot.num);
    return maxNum;
}

/**
 * @brief 指定ルート配下のスナップショットを列挙する
 * @param snapshotsRoot スナップショットルート
 * @return 読み取れた一覧 (番号昇順)
 */
std::vector<SnapshotEntry> readSnapshots(const QString &snapshotsRoot)
{
    std::vector<SnapshotEntry> result;
    QDir root(snapshotsRoot);
    if (!root.exists())
        return result;

    const QStringList entries =
        root.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    for (const QString &name : entries) {
        if (result.size() >= SnapshotLimits::kMaxSnapshots)
            break;
        QFile info(root.filePath(name + QStringLiteral("/info.xml")));
        if (!info.open(QIODevice::ReadOnly))
            continue;
        const QByteArray xml = info.read(SnapshotLimits::kMaxInfoXmlBytes);
        info.close();
        if (auto parsed = parseSnapshotInfoXml(xml))
            result.push_back(*parsed);
    }

    std::sort(result.begin(), result.end(),
              [](const SnapshotEntry &lhs, const SnapshotEntry &rhs) {
                  return lhs.num < rhs.num;
              });
    return result;
}

/**
 * @brief afterのうちbeforeに存在しないスナップショットを返す
 * @param before コミット前の一覧
 * @param after コミット後の一覧
 * @return 新規スナップショット (番号昇順)
 */
std::vector<SnapshotEntry> newSnapshots(const std::vector<SnapshotEntry> &before,
                                        const std::vector<SnapshotEntry> &after)
{
    std::set<quint64> known;
    for (const auto &snapshot : before)
        known.insert(snapshot.num);

    std::vector<SnapshotEntry> result;
    for (const auto &snapshot : after) {
        if (known.find(snapshot.num) == known.end())
            result.push_back(snapshot);
    }
    return result;
}

/**
 * @brief SnapshotEntryをD-Bus転送用のマップへ変換する
 * @param snapshot 変換対象
 * @return {num, type, description, date}
 */
QVariantMap snapshotToVariantMap(const SnapshotEntry &snapshot)
{
    QVariantMap map;
    map.insert(QStringLiteral("num"), QVariant::fromValue<qulonglong>(snapshot.num));
    map.insert(QStringLiteral("type"), snapshot.type);
    map.insert(QStringLiteral("description"), snapshot.description);
    map.insert(QStringLiteral("date"), snapshot.date);
    return map;
}

/**
 * @brief スナップショット一覧をD-Bus転送用のリストへ変換する
 * @param snapshots 変換対象の一覧
 * @return 各要素がsnapshotToVariantMapの結果であるリスト
 */
QVariantList snapshotsToVariantList(const std::vector<SnapshotEntry> &snapshots)
{
    QVariantList list;
    list.reserve(static_cast<int>(snapshots.size()));
    for (const auto &snapshot : snapshots) list.append(snapshotToVariantMap(snapshot));
    return list;
}

} // namespace qZypper
