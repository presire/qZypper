#ifndef QZYPPER_BACKEND_REPOFILEPARSER_H
#define QZYPPER_BACKEND_REPOFILEPARSER_H

#include <QString>
#include <QVariantMap>
#include <cstddef>
#include <functional>
#include <optional>
#include <utility>
#include <vector>

namespace qZypper {

/**
 * @brief .repo ファイルの 1 セクションを表す解析結果。
 *
 * クライアント供給のパスは一切保持しない。値はすべて文字列として保持し、
 * 検証は注入されたバリデータ (validateAlias / validateUrl) で行う。
 */
struct RepoFileSection {
    QString alias;                       // セクション名 (リポジトリエイリアス)
    QString name;                        // 表示名 (未指定時は alias を補完)
    QString url;                         // baseurl
    bool enabled = true;                 // enabled=
    bool autoRefresh = false;            // autorefresh=
    long long priority = 99;             // priority= (1-200)
    bool keepPackages = false;           // keeppackages=
    QString type;                        // type= (rpmmd/plaindir/yast2)
    QString path;                        // path=
    std::optional<bool> gpgCheck;        // gpgcheck=
    std::optional<bool> repoGpgCheck;    // repo_gpgcheck=
    std::optional<bool> pkgGpgCheck;     // pkg_gpgcheck=
    QString gpgKey;                      // gpgkey=
    QString invalidReason;               // 空でなければセクションは破棄される
};

/**
 * @brief .repo 解析の全体結果。
 */
struct RepoFileParseResult {
    bool ok = false;                                  // ファイル全体の構文が正しいか
    QString error;                                    // ok=false の理由
    std::vector<RepoFileSection> sections;            // 受け入れたセクション
    std::vector<std::pair<QString, QString>> skipped; // (alias, 破棄理由)
};

/**
 * @brief .repo (INI) テキストを純粋関数として厳密に解析する。
 *
 * ファイルパスを一切扱わないため、クライアント供給のパスが libzypp へ
 * 流入することはない。構文エラー (ヘッダ不正・セクション外のキー・'=' 欠落)
 * はファイル全体のエラーとし、値の不正 (未知キー・不正 URL・重複) は
 * 該当セクションのみを skipped へ回す。
 * @param content .repo の内容
 * @param maxBytes 受け付ける最大バイト数 (超過はエラー)
 * @param validateAlias エイリアス検証関数 (失敗時は理由を第 2 引数へ)
 * @param validateUrl URL 検証関数 (失敗時は理由を第 2 引数へ)
 * @param out 解析結果の出力先
 * @return ファイル全体の構文が正しい場合 true
 */
bool parseRepoFile(const QString &content,
                   std::size_t maxBytes,
                   const std::function<bool(const QString &, QString &)> &validateAlias,
                   const std::function<bool(const QString &, QString &)> &validateUrl,
                   RepoFileParseResult &out);

/**
 * @brief セクションを GUI プレビュー / RepoInfo 用の QVariantMap へ変換する。
 * @param section 変換対象セクション
 * @return 正規化キーを持つマップ
 */
QVariantMap repoFileSectionToMap(const RepoFileSection &section);

} // namespace qZypper

#endif // QZYPPER_BACKEND_REPOFILEPARSER_H
