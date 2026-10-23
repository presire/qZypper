/**
 * @file LicenseConfirmation.h
 * @brief ライセンス承認を候補と本文へ束縛する補助関数
 *
 * ZyppManagerから呼び出され、候補の同一性を表す承認キーと拒否時の選択状態を決定する
 *
 * 入力 : パッケージ候補の名前、版、arch、リポジトリ、ライセンス本文
 * 出力 : 承認キーまたは拒否後のlibzypp選択状態
 * 依存 : libzyppのStatus
 */
#ifndef QZYPPER_BACKEND_LICENSECONFIRMATION_H
#define QZYPPER_BACKEND_LICENSECONFIRMATION_H

#include <zypp/ui/Status.h>
#include <optional>
#include <string>

namespace qZypper {

/**
 * @brief 実際の候補とライセンス全文に束縛した承認キーを生成する
 * @param name パッケージ名
 * @param edition パッケージ版
 * @param arch パッケージアーキテクチャ
 * @param repoAlias 提供リポジトリの別名
 * @param licenseText 確認対象のライセンス本文
 * @return 区切り文字による衝突を避けた承認キー
 */
std::string licenseApprovalKey(const std::string &name, const std::string &edition,
                               const std::string &arch, const std::string &repoAlias,
                               const std::string &licenseText);

/**
 * @brief 拒否時のTABOOまたはPROTECTEDへの状態遷移を返す
 * @param status 現在のlibzypp選択状態
 * @return 導入候補ではTABOO、更新候補ではPROTECTED、それ以外では空
 */
std::optional<zypp::ui::Status> licenseRejectedStatus(zypp::ui::Status status);

} // namespace qZypper

#endif // QZYPPER_BACKEND_LICENSECONFIRMATION_H
