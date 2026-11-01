/**
 * @file DBusDemarshal.h
 * @brief D-Busの入れ子の値をQtの通常のコンテナへ変換する関数を宣言する
 *
 * DBusClientとPackageListDecodeが応答を扱う際に利用する
 * 入力 : D-Busから受け取ったQVariant
 * 出力 : 正規化したQVariant、QVariantListまたはQVariantMap
 * 依存 : Qt Core、Qt D-Bus
 */
#ifndef QZYPPER_GUI_DBUSDEMARSHAL_H
#define QZYPPER_GUI_DBUSDEMARSHAL_H

#include <QVariant>
#include <QVariantList>
#include <QVariantMap>

namespace qZypper {

/**
 * @brief QDBusVariant / QDBusArgumentを含む値を、再帰的に通常のQVariantへ正規化する
 * @param value 変換対象
 * @return 正規化後の値
 */
QVariant normalizeDBusValue(const QVariant &value);

/**
 * @brief av / aa{sv}形式の配列をQVariantListに変換する (入れ子も展開)
 * @param variant 変換対象の引数
 * @return QVariantMapのリスト
 */
QVariantList demarshallArrayOfMaps(const QVariant &variant);

/**
 * @brief a{sv}形式のマップをQVariantMapに変換する (入れ子も展開)
 * @param variant 変換対象の引数
 * @return 変換後のQVariantMap
 */
QVariantMap demarshallMap(const QVariant &variant);

} // namespace qZypper

#endif // QZYPPER_GUI_DBUSDEMARSHAL_H
