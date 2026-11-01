/**
 * @file PackageListDecode.h
 * @brief パッケージ一覧のD-Bus応答を復号する関数を宣言する
 *
 * DBusClientが新旧の一覧応答を共通の行データへ変換するために利用する
 * 入力 : D-Bus応答の引数
 * 出力 : PackageRowsと復号の成否
 * 依存 : Qt Core、PackageRow
 */
#ifndef QZYPPER_GUI_PACKAGELISTDECODE_H
#define QZYPPER_GUI_PACKAGELISTDECODE_H

#include <QVariant>

#include "PackageRow.h"

namespace qZypper {

/**
 * @brief 一覧取得メソッドの応答引数をPackageRowsへ変換する
 *
 * 受理する形式は次の3通り
 * - 新形式 (a(ss)a(sssssitib)) : PackageListReply
 * - 旧形式 av / aa{sv} : マップの配列
 * - 展開済みのQVariantList (全要素がQVariantMapまたはそのQDBusVariant) : 旧形式
 *
 * @param arg D-Busの応答引数
 * @param out 変換結果の格納先
 * @return 受理した場合はtrue、形式が不正な場合はfalse
 * @note 成功時にoutを更新する
 */
bool decodePackageList(const QVariant &arg, PackageRows *out);

} // namespace qZypper

#endif // QZYPPER_GUI_PACKAGELISTDECODE_H
