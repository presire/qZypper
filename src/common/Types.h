#ifndef QZYPPER_COMMON_TYPES_H
#define QZYPPER_COMMON_TYPES_H

#include <QObject>

namespace qZypper {

Q_NAMESPACE

/**
 * @brief パッケージの状態 (libzypp zypp::ui::Statusに対応)
 *
 * qZypper独自の順序で定義されており、
 * ZyppManager::fromZyppStatus() / toZyppStatus()でlibzyppのzypp::ui::Statusと相互変換する
 */
enum class PackageStatus {
    NoInst = 0,         // 未インストール、選択なし
    Install,            // 手動でインストール選択
    AutoInstall,        // ソルバによる自動インストール
    KeepInstalled,      // インストール済み、変更なし
    Update,             // 手動で更新選択
    AutoUpdate,         // ソルバによる自動更新
    Del,                // 手動で削除選択
    AutoDel,            // ソルバによる自動削除
    Taboo,              // インストール禁止
    Protected           // 削除禁止
};
Q_ENUM_NS(PackageStatus)

/**
 * @brief パッケージ検索のフラグ (ビットマスク)
 */
enum class SearchFlag {
    Name        = 1 << 0,  // パッケージ名を対象にする
    Keywords    = 1 << 1,  // キーワードを対象にする
    Summary     = 1 << 2,  // 概要を対象にする
    Description = 1 << 3,  // 説明文を対象にする
    Requires    = 1 << 4,  // 必要ケーパビリティを対象にする
    Provides    = 1 << 5,  // 提供ケーパビリティを対象にする
    FileList    = 1 << 6,  // インストールファイルを対象にする
    Recommends  = 1 << 7   // 推奨ケーパビリティを対象にする
};
Q_ENUM_NS(SearchFlag)
Q_DECLARE_FLAGS(SearchFlags, SearchFlag)
Q_DECLARE_OPERATORS_FOR_FLAGS(SearchFlags)

/**
 * @brief パッチカテゴリの分類
 */
enum class PatchCategory {
    Security = 0,  // セキュリティ更新
    Recommended,   // 推奨更新
    Optional,      // 任意更新
    Feature,       // 機能更新
    All            // 全カテゴリ
};
Q_ENUM_NS(PatchCategory)

} // namespace qZypper

#endif // QZYPPER_COMMON_TYPES_H
