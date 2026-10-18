#ifndef QZYPPER_BACKEND_ZYPPLOCKS_H
#define QZYPPER_BACKEND_ZYPPLOCKS_H

#include <QString>
#include <QVariantMap>
#include <string>

namespace qZypper {

/**
 * @brief 永続ロック (zypper al/rl 互換) の 1 件分の情報。
 *
 * /etc/zypp/locks に保存される zypp::Locks::LockList (PoolQuery) の
 * 表示・検証・生成に必要な項目を保持する。
 */
struct LockInfo {
    QString name;         ///< ロック対象名 (必須)
    QString kind;         ///< リソース種別 (package/pattern/patch/product 等)
    QString match;        ///< マッチ方式 (exact/substring/glob/regex/word)
    QString repoAlias;    ///< 限定リポジトリ別名 (空なら全リポジトリ)
    QString arch;         ///< アーキテクチャ (空なら全アーキテクチャ)
    QString version;      ///< エディション (空なら全バージョン)
    QString comment;      ///< コメント (保存時に付与)
    int matchCount = -1;  ///< 現在のプール上の一致件数 (-1 は未計算)

    /**
     * @brief QVariantMap へ変換する。
     * @return D-Bus 転送用マップ (name/kind/match/repoAlias/arch/version/comment/matchCount)
     */
    QVariantMap toVariantMap() const;
};

/**
 * @brief ロック指定マップを厳密に検証し正規化する。
 *
 * name は必須。kind は既定 "package" で既知の種別のみ許可。
 * match (match/glob) は既知のマッチ方式のみ許可。
 * 制御文字やパス区切りなどの不正値を拒否する。
 *
 * @param map D-Bus から受け取ったロック指定マップ
 * @param out 正規化したロック情報 (成功時のみ有効)
 * @param error 失敗時のエラーメッセージ (英語)
 * @return 有効な指定であれば true
 */
bool validateLockMap(const QVariantMap &map, LockInfo &out, std::string &error);

} // namespace qZypper

#endif // QZYPPER_BACKEND_ZYPPLOCKS_H
