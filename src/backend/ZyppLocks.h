#ifndef QZYPPER_BACKEND_ZYPPLOCKS_H
#define QZYPPER_BACKEND_ZYPPLOCKS_H

#include <QString>
#include <QVariantMap>
#include <string>

namespace qZypper {

/**
 * @brief 永続ロック (zypper al/rl互換) の1件分の情報
 *
 * /etc/zypp/locksに保存されるzypp::Locks::LockList (PoolQuery) の表示・検証・生成に必要な項目を保持する
 */
struct LockInfo {
    QString name;         // ロック対象名 (必須)
    QString kind;         // リソース種別 (package/pattern/patch/product等)
    QString match;        // マッチ方式 (exact/substring/glob/regex/word)
    QString repoAlias;    // 限定リポジトリ別名 (空なら全リポジトリ)
    QString arch;         // アーキテクチャ (空なら全アーキテクチャ)
    QString version;      // エディション (空なら全バージョン)
    QString comment;      // コメント (保存時に付与)
    int matchCount = -1;  // 現在のプール上の一致件数 (-1は未計算)

    /**
     * @brief QVariantMapへ変換する
     * @return D-Bus転送用マップ (name/kind/match/repoAlias/arch/version/comment/matchCount)
     */
    QVariantMap toVariantMap() const;
};

/**
 * @brief ロック指定マップを厳密に検証し正規化する
 *
 * nameは必須
 * kindは既定"package"で既知の種別のみ許可
 * match (match/glob) は既知のマッチ方式のみ許可
 *
 * 制御文字やパス区切りなどの不正値は拒否
 *
 * @param map D-Busから受け取ったロック指定マップ
 * @param out 正規化したロック情報 (成功時のみ有効)
 * @param error 失敗時のエラーメッセージ (英語)
 * @return 有効な指定であればtrue
 */
bool validateLockMap(const QVariantMap &map, LockInfo &out, std::string &error);

} // namespace qZypper

#endif // QZYPPER_BACKEND_ZYPPLOCKS_H
