#ifndef QZYPPER_BACKEND_SEARCHOPTIONS_H
#define QZYPPER_BACKEND_SEARCHOPTIONS_H

#include <QString>
#include <QVariantMap>

namespace qZypper {

/**
 * @brief 検索クエリの照合方式。
 *
 * PoolQuery の照合モードに対応する。
 */
enum class SearchMatchMode {
    Substring, ///< 部分一致 (既定)
    Exact,     ///< 完全一致
    Glob,      ///< グロブ (ワイルドカード) 一致
    Regex      ///< 正規表現一致
};

/**
 * @brief SearchPackagesEx のオプション。
 *
 * 破壊的な検証を通過した後にのみ生成される。
 */
struct SearchOptions {
    QString query;                       ///< 検索文字列
    int flags = 0;                       ///< SearchFlag のビット和
    SearchMatchMode match = SearchMatchMode::Substring; ///< 照合方式
    unsigned int offset = 0;             ///< 結果の読み飛ばし件数
    unsigned int limit = 0;              ///< 最大件数 (0 は無制限)
    bool installedOnly = false;          ///< インストール済みのみ
    QString repoAlias;                   ///< 対象リポジトリ (空は全リポジトリ)
};

/**
 * @brief SearchPackagesEx のオプションを厳格に検証して解析する。
 *
 * 未知のキー、型不一致、範囲外の値、不正な照合方式、コンパイル不能な
 * 正規表現はすべて拒否する。入力の制御文字・過大長は
 * InputValidation::validateArguments で弾く。
 * @param options D-Bus 由来のオプションマップ
 * @param out 解析結果の格納先
 * @param error 失敗理由 (日本語メッセージ)
 * @return 検証成功なら true
 */
bool parseSearchOptions(const QVariantMap &options, SearchOptions &out, QString &error);

} // namespace qZypper

#endif // QZYPPER_BACKEND_SEARCHOPTIONS_H
