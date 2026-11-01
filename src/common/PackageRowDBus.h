/**
 * @file PackageRowDBus.h
 * @brief パッケージ一覧のD-Bus転送形式と変換を定義する
 *
 * バックエンドとGUIがリポジトリ表を共有する一覧応答を送受信する
 * 入力 : PackageRowsまたはD-Bus応答
 * 出力 : PackageListReplyまたはD-Bus構造体
 * 依存 : PackageRow、Qt D-Bus
 */
#ifndef QZYPPER_COMMON_PACKAGEROWDBUS_H
#define QZYPPER_COMMON_PACKAGEROWDBUS_H

#include <QDBusArgument>
#include <QDBusMetaType>
#include <QHash>
#include <QString>
#include <QVector>
#include "PackageRow.h"

namespace qZypper {

namespace detail {

/**
 * @brief ワイヤ形式のリポジトリ表の1要素 (alias, name)
 */
struct PackageRepoRef {
    QString alias;  // リポジトリエイリアス
    QString name;   // リポジトリ名
};

/**
 * @brief ワイヤ形式のパッケージ行 (リポジトリは表への索引で持つ)
 */
struct PackageRowWire {
    QString name;              // パッケージ名
    QString summary;           // 概要
    QString version;           // 候補バージョン
    QString installedVersion;  // インストール済みバージョン
    QString arch;              // アーキテクチャ
    int repoIndex = -1;        // リポジトリ表への索引 (-1はリポジトリなし)
    quint64 size = 0;          // インストールサイズ (バイト)
    int status = 0;            // パッケージ状態
    bool updatable = false;    // より新しい更新候補があるか
};

/**
 * @brief リポジトリ表の1要素をD-Bus構造体へ書き込む
 * @param argument 書き込み先
 * @param repo リポジトリ情報
 * @return 書き込み先の引数
 */
inline QDBusArgument &operator<<(QDBusArgument &argument, const PackageRepoRef &repo)
{
    argument.beginStructure();
    argument << repo.alias << repo.name;
    argument.endStructure();
    return argument;
}

/**
 * @brief リポジトリ表の1要素をD-Bus構造体から読む
 * @param argument 読み取り元
 * @param repo 読み取った情報の格納先
 * @return 読み取り元の引数
 */
inline const QDBusArgument &operator>>(const QDBusArgument &argument, PackageRepoRef &repo)
{
    argument.beginStructure();
    argument >> repo.alias >> repo.name;
    argument.endStructure();
    return argument;
}

/**
 * @brief ワイヤ形式の行をD-Bus構造体へ書き込む
 * @param argument 書き込み先
 * @param row 書き込む行
 * @return 書き込み先の引数
 */
inline QDBusArgument &operator<<(QDBusArgument &argument, const PackageRowWire &row)
{
    argument.beginStructure();
    argument << row.name << row.summary << row.version << row.installedVersion << row.arch
             << row.repoIndex << row.size << row.status << row.updatable;
    argument.endStructure();
    return argument;
}

/**
 * @brief ワイヤ形式の行をD-Bus構造体から読む
 * @param argument 読み取り元
 * @param row 読み取った行の格納先
 * @return 読み取り元の引数
 */
inline const QDBusArgument &operator>>(const QDBusArgument &argument, PackageRowWire &row)
{
    argument.beginStructure();
    argument >> row.name >> row.summary >> row.version >> row.installedVersion >> row.arch
             >> row.repoIndex >> row.size >> row.status >> row.updatable;
    argument.endStructure();
    return argument;
}

} // namespace detail

/**
 * @brief 一覧取得メソッドの応答 (リポジトリ表 + 行)
 */
struct PackageListReply {
    PackageRows rows;  // パッケージ行の一覧
};

/**
 * @brief 応答をワイヤ形式(a(ss)a(sssssitib))へ変換する
 *
 * リポジトリ表はaliasの初出順で重複を除いて構築し、空の行は索引-1とする
 * @param argument 書き込み先
 * @param reply 送信する一覧応答
 * @return 書き込み先の引数
 */
inline QDBusArgument &operator<<(QDBusArgument &argument, const PackageListReply &reply)
{
    QVector<detail::PackageRepoRef> repos;
    QHash<QString, int> indexByAlias;
    for (const PackageRow &row : reply.rows) {
        // リポジトリなしの行と登録済みのaliasは表に追加しない
        if (row.repoAlias.isEmpty() || indexByAlias.contains(row.repoAlias))
            continue;
        indexByAlias.insert(row.repoAlias, repos.size());
        repos.append(detail::PackageRepoRef{row.repoAlias, row.repoName});
    }

    QVector<detail::PackageRowWire> rows;
    rows.reserve(reply.rows.size());
    for (const PackageRow &row : reply.rows) {
        detail::PackageRowWire wire;
        wire.name             = row.name;
        wire.summary          = row.summary;
        wire.version          = row.version;
        wire.installedVersion = row.installedVersion;
        wire.arch             = row.arch;
        wire.repoIndex        = indexByAlias.value(row.repoAlias, -1);
        wire.size             = row.size;
        wire.status           = row.status;
        wire.updatable        = row.updatable;
        rows.append(wire);
    }

    argument.beginStructure();
    argument << repos;
    argument << rows;
    argument.endStructure();
    return argument;
}

/**
 * @brief ワイヤ形式(a(ss)a(sssssitib))を応答へ変換する
 *
 * 範囲外のリポジトリ索引を持つ行はaliasとnameを空文字にする
 * @param argument 読み取り元
 * @param reply 読み取った一覧応答の格納先 (既存の行は置き換える)
 * @return 読み取り元の引数
 */
inline const QDBusArgument &operator>>(const QDBusArgument &argument, PackageListReply &reply)
{
    QVector<detail::PackageRepoRef> repos;
    QVector<detail::PackageRowWire> rows;
    argument.beginStructure();
    argument >> repos;
    argument >> rows;
    argument.endStructure();

    reply.rows.clear();
    reply.rows.reserve(rows.size());
    for (const detail::PackageRowWire &wire : rows) {
        PackageRow row;
        row.name             = wire.name;
        row.summary          = wire.summary;
        row.version          = wire.version;
        row.installedVersion = wire.installedVersion;
        row.arch             = wire.arch;
        // 無効な索引では表を参照せず、リポジトリ情報を空のままにする
        if (wire.repoIndex >= 0 && wire.repoIndex < repos.size()) {
            row.repoAlias = repos.at(wire.repoIndex).alias;
            row.repoName  = repos.at(wire.repoIndex).name;
        }
        row.size      = wire.size;
        row.status    = wire.status;
        row.updatable = wire.updatable;
        reply.rows.append(row);
    }
    return argument;
}

/**
 * @brief PackageListReplyとその構成型をQt D-Busに登録する
 * @note 一覧応答を送受信する前に呼び出す
 */
inline void registerPackageListReplyType()
{
    qDBusRegisterMetaType<detail::PackageRepoRef>();
    qDBusRegisterMetaType<detail::PackageRowWire>();
    qDBusRegisterMetaType<PackageListReply>();
}

} // namespace qZypper

Q_DECLARE_METATYPE(qZypper::PackageListReply)

#endif // QZYPPER_COMMON_PACKAGEROWDBUS_H
