/**
 * @file LicenseConfirmation.cpp
 * @brief ライセンス承認キーと拒否状態を実装する
 *
 * ZyppManagerのライセンス確認処理から呼び出され、承認を候補とライセンス本文へ固定する
 *
 * 入力 : 候補を識別する各値とライセンス本文、現在の選択状態
 * 出力 : 承認キーまたは拒否後のlibzypp選択状態
 * 依存 : LicenseConfirmation、QtのQCryptographicHash、libzypp
 */
#include "LicenseConfirmation.h"
#include <QCryptographicHash>

namespace qZypper {

/**
 * @brief 区切り文字を含む値でも衝突しない長さ付きキーを生成する
 * @param name パッケージ名
 * @param edition パッケージ版
 * @param arch パッケージアーキテクチャ
 * @param repoAlias 提供リポジトリの別名
 * @param licenseText 確認対象のライセンス本文
 * @return 各候補値の長さと本文SHA-256を連結した承認キー
 */
std::string licenseApprovalKey(const std::string &name, const std::string &edition,
                               const std::string &arch, const std::string &repoAlias,
                               const std::string &licenseText)
{
    std::string key;
    for (const auto &field : {name, edition, arch, repoAlias}) {
        key += std::to_string(field.size()) + ":" + field;
    }
    key += QCryptographicHash::hash(QByteArray::fromStdString(licenseText),
                                   QCryptographicHash::Sha256).toHex().toStdString();
    return key;
}

/**
 * @brief Myrlynと同様、導入候補はTABOO、更新候補はPROTECTEDにする
 * @param status 現在のlibzypp選択状態
 * @return 導入候補ではTABOO、更新候補ではPROTECTED、それ以外では空
 */
std::optional<zypp::ui::Status> licenseRejectedStatus(zypp::ui::Status status)
{
    switch (status) {
    case zypp::ui::S_Install:
    case zypp::ui::S_AutoInstall:
        return zypp::ui::S_Taboo;
    case zypp::ui::S_Update:
    case zypp::ui::S_AutoUpdate:
        return zypp::ui::S_Protected;
    default:
        return std::nullopt;
    }
}

} // namespace qZypper
