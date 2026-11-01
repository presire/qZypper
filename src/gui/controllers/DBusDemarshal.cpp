/**
 * @file DBusDemarshal.cpp
 * @brief D-Busの入れ子の値をQtの通常のコンテナへ変換する
 *
 * DBusClientとPackageListDecodeが利用する変換関数を実装する
 * 入力 : D-Busから受け取ったQVariant
 * 出力 : 正規化したQVariant、QVariantListまたはQVariantMap
 * 依存 : DBusDemarshal.h、Qt D-Bus
 */
#include "DBusDemarshal.h"

#include <QDBusArgument>
#include <QDBusVariant>
#include <QMetaType>

namespace qZypper {

QVariant normalizeDBusValue(const QVariant &value);

namespace {

/**
 * @brief QDBusArgumentの現在要素を再帰的にQVariantへ変換する
 *
 * Qt D-Busはvariant内の配列/辞書 (aa{sv}, a{sv}, av等) をQDBusArgumentのまま返すため、
 * 入れ子のsummary / problems / snapshots等をQMLから参照できるよう、QVariantMap / QVariantListへ展開する
 *
 * @param arg 読み出し元のQDBusArgument (現在位置の要素を1つ消費する)
 * @return 変換後の値
 */
QVariant readDBusArgument(const QDBusArgument &arg)
{
    switch (arg.currentType()) {
    case QDBusArgument::MapType: {
        QVariantMap map;
        arg.beginMap();
        while (!arg.atEnd()) {
            arg.beginMapEntry();
            const QVariant key = normalizeDBusValue(arg.asVariant());
            const QVariant value = readDBusArgument(arg);
            arg.endMapEntry();
            map.insert(key.toString(), value);
        }
        arg.endMap();
        return map;
    }
    case QDBusArgument::ArrayType: {
        QVariantList list;
        arg.beginArray();
        while (!arg.atEnd())
            list.append(readDBusArgument(arg));
        arg.endArray();
        return list;
    }
    case QDBusArgument::StructureType: {
        QVariantList fields;
        arg.beginStructure();
        while (!arg.atEnd())
            fields.append(readDBusArgument(arg));
        arg.endStructure();
        return fields;
    }
    default:
        return normalizeDBusValue(arg.asVariant());
    }
}

} // namespace

/**
 * @brief QDBusVariant / QDBusArgumentを含む値を、再帰的に通常のQVariantへ正規化する
 * @param value 変換対象
 * @return 正規化後の値
 */
QVariant normalizeDBusValue(const QVariant &value)
{
    const int type = value.userType();
    if (type == qMetaTypeId<QDBusVariant>())
        return normalizeDBusValue(value.value<QDBusVariant>().variant());
    if (type == qMetaTypeId<QDBusArgument>())
        return readDBusArgument(value.value<QDBusArgument>());
    if (type == QMetaType::QVariantList) {
        QVariantList list = value.toList();
        for (QVariant &item : list)
            item = normalizeDBusValue(item);
        return list;
    }
    if (type == QMetaType::QVariantMap) {
        QVariantMap map = value.toMap();
        for (auto it = map.begin(); it != map.end(); ++it)
            it.value() = normalizeDBusValue(it.value());
        return map;
    }
    return value;
}

/**
 * @brief 入れ子を展開して一覧に変換する
 * @param variant D-Busの配列
 * @return 正規化した一覧
 */
QVariantList demarshallArrayOfMaps(const QVariant &variant)
{
    return normalizeDBusValue(variant).toList();
}

/**
 * @brief 入れ子を展開してマップに変換する
 * @param variant D-Busのマップ
 * @return 正規化したマップ
 */
QVariantMap demarshallMap(const QVariant &variant)
{
    return normalizeDBusValue(variant).toMap();
}

} // namespace qZypper
