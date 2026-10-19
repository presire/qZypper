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
 * @brief snapperが作成した1スナップショット分の情報
 *
 * /.snapshots/<num>/info.xmlから読み取った最小限の属性を保持する
 * 本アプリケーションは自身ではスナップショットを作成せず、コミット前後で差分を観測するのみ
 */
struct SnapshotEntry {
    quint64 num = 0;        // スナップショット番号 (<num>)
    QString type;           // 種類 (<type>) : "pre" / "post" / "single"等
    QString description;    // 説明 (<description>)
    QString date;           // 作成日時 (<date>) : UNIX秒 (文字列のまま保持)
};

/**
 * @brief 件数・サイズの上限 (fail-closed / D-Bus転送量制限)
 */
namespace SnapshotLimits {
constexpr int kMaxSnapshots = 10000;            // 読み取るスナップショット数の上限
constexpr qint64 kMaxInfoXmlBytes = 64 * 1024;  // info.xml 1件の読み取り上限
}

/**
 * @brief snapperのinfo.xmlを解析する
 * @param xml info.xmlの内容
 * @return 解析成功時はSnapshotEntry、<num>欠落や不正時はstd::nullopt
 */
std::optional<SnapshotEntry> parseSnapshotInfoXml(const QByteArray &xml);

/**
 * @brief スナップショット番号の最大値を求める
 * @param snapshots 対象一覧
 * @return 最大番号、空の場合は0
 */
quint64 maxSnapshotNumber(const std::vector<SnapshotEntry> &snapshots);

/**
 * @brief 指定ルート配下のスナップショットを列挙する
 *
 * ディレクトリを走査し各info.xmlを解析する
 * 読めない/壊れた項目は無視する
 * @param snapshotsRoot スナップショットルート (通常"/.snapshots")
 * @return 読み取れたスナップショット一覧 (番号順)
 */
std::vector<SnapshotEntry> readSnapshots(const QString &snapshotsRoot);

/**
 * @brief afterのうちbeforeに存在しないスナップショットを返す
 * @param before コミット前に観測した一覧
 * @param after コミット後に観測した一覧
 * @return 新規スナップショット (番号昇順)
 */
std::vector<SnapshotEntry> newSnapshots(const std::vector<SnapshotEntry> &before,
                                        const std::vector<SnapshotEntry> &after);

/**
 * @brief SnapshotEntryをD-Bus転送用のマップへ変換する
 * @param snapshot 変換対象
 * @return {num: qulonglong, type: string, description: string, date: string}
 */
QVariantMap snapshotToVariantMap(const SnapshotEntry &snapshot);

/**
 * @brief スナップショット一覧をD-Bus転送用のリストへ変換する
 * @param snapshots 変換対象の一覧
 * @return 各要素がsnapshotToVariantMapの結果であるリスト
 */
QVariantList snapshotsToVariantList(const std::vector<SnapshotEntry> &snapshots);

} // namespace qZypper

#endif // QZYPPER_BACKEND_SNAPSHOTINFO_H
