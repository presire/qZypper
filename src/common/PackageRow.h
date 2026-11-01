/**
 * @file PackageRow.h
 * @brief パッケージ一覧の表示用データを定義する
 *
 * バックエンドとGUIの一覧処理でPackageInfoや旧形式のマップから行を作る
 * 入力 : PackageInfoまたは旧形式のQVariantMap
 * 出力 : PackageRowとその一覧、モデル通知用のQVariantMap
 * 依存 : Qt Core、PackageInfo、PackageStatus
 */
#ifndef QZYPPER_COMMON_PACKAGEROW_H
#define QZYPPER_COMMON_PACKAGEROW_H

#include <QString>
#include <QVariantMap>
#include <QVector>
#include <QMetaType>
#include <vector>
#include "PackageInfo.h"
#include "Types.h"

namespace qZypper {

/**
 * @brief 一覧表示に必要な属性だけを持つ固定レイアウトのパッケージ行
 *
 * 旧形式のQVariantMapもfromVariantMap()で受け取れる
 * D-BusマーシャリングはPackageRowDBus.hが担当する
 */
struct PackageRow {
    QString name;                                          // パッケージ名
    QString summary;                                       // 概要
    QString version;                                       // 候補バージョン
    QString installedVersion;                              // インストール済みバージョン
    QString arch;                                          // アーキテクチャ
    QString repoAlias;                                     // リポジトリエイリアス
    QString repoName;                                      // リポジトリ名
    quint64 size = 0;                                      // インストールサイズ (バイト)
    int status = static_cast<int>(PackageStatus::NoInst);  // パッケージ状態
    bool updatable = false;                                // より新しい更新候補があるか

    /**
     * @brief 旧形式のQVariantMapから行を生成する
     * @param map パッケージ属性を持つマップ
     * @return 復元した行 (updatableキーがなければfalse)
     */
    static PackageRow fromVariantMap(const QVariantMap &map)
    {
        PackageRow row;
        row.name             = map.value("name").toString();
        row.summary          = map.value("summary").toString();
        row.version          = map.value("version").toString();
        row.installedVersion = map.value("installedVersion").toString();
        row.arch             = map.value("arch").toString();
        row.repoAlias        = map.value("repoAlias").toString();
        row.repoName         = map.value("repoName").toString();
        row.size             = map.value("size").toULongLong();
        row.status           = map.value("status").toInt();
        row.updatable        = map.value("updatable").toBool();
        return row;
    }

    /**
     * @brief 全メンバが等しいかを返す
     * @param other 比較対象の行
     * @return すべて一致する場合true
     */
    bool operator==(const PackageRow &other) const
    {
        return name == other.name
            && summary == other.summary
            && version == other.version
            && installedVersion == other.installedVersion
            && arch == other.arch
            && repoAlias == other.repoAlias
            && repoName == other.repoName
            && size == other.size
            && status == other.status
            && updatable == other.updatable;
    }

    /**
     * @brief モデル通知用のQVariantMapへ変換する
     * @return 一覧表示用の10個のキーを持つマップ
     */
    QVariantMap toVariantMap() const
    {
        return {
            {"name",             name},
            {"summary",          summary},
            {"version",          version},
            {"installedVersion", installedVersion},
            {"arch",             arch},
            {"repoAlias",        repoAlias},
            {"repoName",         repoName},
            {"size",             QVariant::fromValue(size)},
            {"status",           status},
            {"updatable",        updatable}
        };
    }
};

/** @brief パッケージ行の一覧 */
using PackageRows = QVector<PackageRow>;

/**
 * @brief PackageInfoの一覧を行の一覧へ変換する
 * @param infos libzyppから取得したパッケージ情報
 * @return 入力順を保った行の一覧
 */
inline PackageRows toPackageRows(const std::vector<PackageInfo> &infos)
{
    PackageRows rows;
    rows.reserve(static_cast<int>(infos.size()));
    for (const PackageInfo &info : infos) {
        PackageRow row;
        row.name             = info.name;
        row.summary          = info.summary;
        row.version          = info.version;
        row.installedVersion = info.installedVersion;
        row.arch             = info.arch;
        row.repoAlias        = info.repoAlias;
        row.repoName         = info.repoName;
        row.size             = info.size;
        row.status           = info.status;
        row.updatable        = info.updatable;
        rows.append(row);
    }
    return rows;
}

} // namespace qZypper

Q_DECLARE_METATYPE(qZypper::PackageRows)

#endif // QZYPPER_COMMON_PACKAGEROW_H
