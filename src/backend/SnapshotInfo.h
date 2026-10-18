#ifndef QZYPPER_BACKEND_SNAPSHOTINFO_H
#define QZYPPER_BACKEND_SNAPSHOTINFO_H

#include <QByteArray>
#include <QString>
#include <QVariantList>
#include <QVariantMap>
#include <optional>
#include <vector>

namespace qZypper {

/**
 * @brief snapper が作成した 1 スナップショット分の情報。
 *
 * /.snapshots/<num>/info.xml から読み取った最小限の属性を保持する。
 * 本アプリは自分ではスナップショットを作成せず、コミット前後で差分を観測するだけ。
 */
struct SnapshotEntry {
    quint64 num = 0;        // スナップショット番号 (<num>)
    QString type;           // 種類 (<type>): "pre" / "post" / "single" 等
    QString description;    // 説明 (<description>)
    QString date;           // 作成日時 (<date>): UNIX 秒 (文字列のまま保持)
};

/**
 * @brief 件数・サイズの上限 (fail-closed / D-Bus 転送量制限)。
 */
namespace SnapshotLimits {
constexpr int kMaxSnapshots = 10000;        // 読み取るスナップショット数の上限
constexpr qint64 kMaxInfoXmlBytes = 64 * 1024;  // info.xml 1 件の読み取り上限
}

/**
 * @brief snapper の info.xml を解析する。
 * @param xml info.xml の内容
 * @return 解析成功時は SnapshotEntry、<num> 欠落や不正時は std::nullopt。
 */
std::optional<SnapshotEntry> parseSnapshotInfoXml(const QByteArray &xml);

/**
 * @brief スナップショット番号の最大値を求める。
 * @param snapshots 対象一覧
 * @return 最大番号。空の場合は 0。
 */
quint64 maxSnapshotNumber(const std::vector<SnapshotEntry> &snapshots);

/**
 * @brief 指定ルート配下のスナップショットを列挙する。
 *
 * ディレクトリを走査し各 info.xml を解析する。読めない/壊れた項目は無視する。
 * @param snapshotsRoot スナップショットルート (通常 "/.snapshots")
 * @return 読み取れたスナップショット一覧 (番号順)
 */
std::vector<SnapshotEntry> readSnapshots(const QString &snapshotsRoot);

/**
 * @brief after のうち before に存在しないスナップショットを返す。
 * @param before コミット前に観測した一覧
 * @param after コミット後に観測した一覧
 * @return 新規スナップショット (番号昇順)
 */
std::vector<SnapshotEntry> newSnapshots(const std::vector<SnapshotEntry> &before,
                                        const std::vector<SnapshotEntry> &after);

/**
 * @brief SnapshotEntry を D-Bus 転送用のマップへ変換する。
 * @param snapshot 変換対象
 * @return {num: qulonglong, type: string, description: string, date: string}
 */
QVariantMap snapshotToVariantMap(const SnapshotEntry &snapshot);

/**
 * @brief スナップショット一覧を D-Bus 転送用のリストへ変換する。
 * @param snapshots 変換対象の一覧
 * @return 各要素が snapshotToVariantMap の結果であるリスト
 */
QVariantList snapshotsToVariantList(const std::vector<SnapshotEntry> &snapshots);

} // namespace qZypper

#endif // QZYPPER_BACKEND_SNAPSHOTINFO_H
