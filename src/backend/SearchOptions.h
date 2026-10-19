#ifndef QZYPPER_BACKEND_SEARCHOPTIONS_H
#define QZYPPER_BACKEND_SEARCHOPTIONS_H

#include <QString>
#include <QVariantMap>

namespace qZypper {

/**
 * @brief 検索クエリの照合方式
 *
 * PoolQueryの照合モードに対応する
 */
enum class SearchMatchMode {
    Substring, // 部分一致 (既定)
    Exact,     // 完全一致
    Glob,      // グロブ (ワイルドカード) 一致
    Regex      // 正規表現一致
};

/**
 * @brief SearchPackagesExのオプション
 *
 * 破壊的な検証を通過した後にのみ生成される
 */
struct SearchOptions {
    QString query;                                       // 検索文字列
    int flags = 0;                                       // SearchFlagのビット和
    SearchMatchMode match = SearchMatchMode::Substring;  // 照合方式
    unsigned int offset = 0;                             // 結果の読み飛ばし件数
    unsigned int limit = 0;                              // 最大件数 (0は無制限)
    bool installedOnly = false;                          // インストール済みのみ
    QString repoAlias;                                   // 対象リポジトリ (空は全リポジトリ)
};

/**
 * @brief SearchPackagesExのオプションを厳格に検証して解析する
 *
 * 未知のキー、型不一致、範囲外の値、不正な照合方式、POSIX EREとしてコンパイル不能な正規表現はすべて拒否する
 * 入力の制御文字・過大長はInputValidation::validateArgumentsで弾く
 *
 * @param options D-Bus由来のオプションマップ
 * @param out 解析結果の格納先
 * @param error 失敗理由 (日本語メッセージ)
 * @return 検証成功ならtrue
 */
bool parseSearchOptions(const QVariantMap &options, SearchOptions &out, QString &error);

} // namespace qZypper

#endif // QZYPPER_BACKEND_SEARCHOPTIONS_H
