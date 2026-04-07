#ifndef QZYPPER_COMMON_TYPES_H
#define QZYPPER_COMMON_TYPES_H

#include <QObject>

namespace qZypper {

Q_NAMESPACE

/**
 * @brief パッケージの状態 (libzypp zypp::ui::Status に対応)。
 *
 * qZypper 独自の順序で定義されており、ZyppManager::fromZyppStatus() /
 * toZyppStatus() で libzypp の zypp::ui::Status と相互変換する。
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
 * @brief パッケージ検索のフラグ (ビットマスク)。
 */
enum class SearchFlag {
    Name        = 1 << 0,
    Keywords    = 1 << 1,
    Summary     = 1 << 2,
    Description = 1 << 3,
    Requires    = 1 << 4,
    Provides    = 1 << 5,
    FileList    = 1 << 6
};
Q_ENUM_NS(SearchFlag)
Q_DECLARE_FLAGS(SearchFlags, SearchFlag)
Q_DECLARE_OPERATORS_FOR_FLAGS(SearchFlags)

/**
 * @brief パッチカテゴリの分類。
 */
enum class PatchCategory {
    Security = 0,
    Recommended,
    Optional,
    Feature,
    All
};
Q_ENUM_NS(PatchCategory)

} // namespace qZypper

#endif // QZYPPER_COMMON_TYPES_H
