/**
 * @file PackageListDecode.cpp
 * @brief 新旧のパッケージ一覧応答をPackageRowsへ復号する
 *
 * DBusClientの一覧取得処理から呼び出される
 * 入力 : D-Bus応答の引数
 * 出力 : PackageRowsと復号の成否
 * 依存 : PackageListDecode.h、DBusDemarshal、PackageRowDBus、Qt D-Bus
 */
#include "PackageListDecode.h"

#include <QDBusArgument>
#include <QDBusVariant>
#include <QMetaType>

#include "DBusDemarshal.h"
#include "PackageRowDBus.h"

namespace qZypper {

namespace {

const QString kPackageListSignature = QStringLiteral("(a(ss)a(sssssitib))");  // 新形式の署名
const QString kVariantsSignature = QStringLiteral("av");                       // 旧形式のvariant配列の署名
const QString kMapsSignature = QStringLiteral("aa{sv}");                       // 旧形式のマップ配列の署名

/**
 * @brief 要素を旧形式のマップとして読む
 * @param value 要素
 * @param out 変換結果の格納先
 * @return マップとして読めた場合はtrue
 * @note 成功時にoutを更新する
 */
bool readLegacyRow(const QVariant &value, PackageRow *out)
{
    QVariant current = value;
    while (current.userType() == qMetaTypeId<QDBusVariant>())
        current = current.value<QDBusVariant>().variant();
    // 不正な要素を空の行として扱わない
    if (current.userType() != QMetaType::QVariantMap)
        return false;
    *out = PackageRow::fromVariantMap(current.toMap());
    return true;
}

/**
 * @brief 旧形式のリストをPackageRowsへ変換する
 * @param list 旧形式のリスト
 * @param out 変換結果の格納先
 * @return 全要素がマップの場合はtrue
 * @note 全要素を変換できた場合だけoutを更新する
 */
bool decodeLegacyList(const QVariantList &list, PackageRows *out)
{
    PackageRows rows;
    rows.reserve(list.size());
    for (const QVariant &value : list) {
        PackageRow row;
        // 不正な要素があれば部分的な一覧を返さない
        if (!readLegacyRow(value, &row))
            return false;
        rows.append(row);
    }
    *out = rows;
    return true;
}

} // namespace

/**
 * @brief 新旧の一覧応答をPackageRowsへ変換する
 * @param arg D-Busの応答引数
 * @param out 変換結果の格納先
 * @return 対応する形式を復号できた場合はtrue
 * @note 成功時にoutを更新する
 */
bool decodePackageList(const QVariant &arg, PackageRows *out)
{
    if (arg.userType() == qMetaTypeId<QDBusArgument>()) {
        const QDBusArgument argument = arg.value<QDBusArgument>();
        if (argument.currentSignature() == kPackageListSignature) {
            *out = qdbus_cast<PackageListReply>(arg).rows;
            return true;
        }
        if (argument.currentSignature() == kVariantsSignature
            || argument.currentSignature() == kMapsSignature) {
            return decodeLegacyList(demarshallArrayOfMaps(arg), out);
        }
        // 未知の署名を旧形式として誤解釈しない
        return false;
    }

    if (arg.userType() == QMetaType::QVariantList)
        return decodeLegacyList(arg.toList(), out);

    // 対応しない型は不正な応答として呼び出し元へ返す
    return false;
}

} // namespace qZypper
