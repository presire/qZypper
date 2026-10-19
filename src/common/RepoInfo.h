#ifndef QZYPPER_COMMON_REPOINFO_H
#define QZYPPER_COMMON_REPOINFO_H

#include <optional>
#include <QString>
#include <QVariantMap>
#include <QMetaType>
#include "PartialUpdate.h"

namespace qZypper {

/**
 * @brief D-Bus経由で転送するリポジトリ情報
 */
struct RepoInfo {
    QString alias;                                      // リポジトリエイリアス
    QString name;                                       // 表示名
    QString url;                                        // ベースURL (変数展開済み)
    QString rawUrl;                                     // 生ベースURL (${releasever}等を含む)
    bool enabled = true;                                // 有効フラグ
    bool autoRefresh = true;                            // 自動リフレッシュ
    int priority = 99;                                  // 優先度 (1-99, 小さいほど高)
    QString type;                                       // リポタイプ ("rpm-md", "yast2"等)
    bool keepPackages = false;                          // パッケージキャッシュ保持
    QString service;                                    // 所属サービスエイリアス
    qint64 lastRefresh = 0;                             // 最終メタデータ更新日時 (エポック秒、0は不明)

    /**
     * @brief D-Bus転送用のQVariantMapへ変換する
     * @return リポジトリ属性を持つマップ
     */
    QVariantMap toVariantMap() const {
        return {
            {"alias",        alias},
            {"name",         name},
            {"url",          url},
            {"rawUrl",       rawUrl},
            {"enabled",      enabled},
            {"autoRefresh",  autoRefresh},
            {"priority",     priority},
            {"type",         type},
            {"keepPackages", keepPackages},
            {"service",      service},
            {"lastRefresh",  lastRefresh}
        };
    }

    /**
     * @brief D-Busで受信したQVariantMapから生成する
     *
     * 渡されなかったキーはメンバの既定値を維持する (false / 0に倒さない)
     * @param map リポジトリ属性を持つマップ
     * @return 復元したリポジトリ情報
     */
    static RepoInfo fromVariantMap(const QVariantMap &map) {
        RepoInfo info;
        info.alias        = map.value("alias").toString();
        info.name         = map.value("name").toString();
        info.url          = map.value("url").toString();
        info.rawUrl       = map.value("rawUrl").toString();
        info.enabled      = map.value("enabled", info.enabled).toBool();
        info.autoRefresh  = map.value("autoRefresh", info.autoRefresh).toBool();
        info.priority     = map.value("priority", info.priority).toInt();
        info.type         = map.value("type").toString();
        info.keepPackages = map.value("keepPackages", info.keepPackages).toBool();
        info.service      = map.value("service").toString();
        info.lastRefresh  = map.value("lastRefresh", info.lastRefresh).toLongLong();
        return info;
    }
};

/**
 * @brief リポジトリの部分更新内容 (ModifyRepo用)
 *
 * 値を持つメンバだけを適用する
 * 渡されなかったキーは現状維持となる
 */
struct RepoChanges {
    std::optional<QString> name;                        // 表示名 (空文字は現状維持)
    std::optional<QString> url;                         // ベースURL (空文字は現状維持)
    std::optional<bool> enabled;                        // 有効フラグ
    std::optional<bool> autoRefresh;                    // 自動リフレッシュ
    std::optional<qlonglong> priority;                  // 優先度 (範囲検証はZyppManager側)
    std::optional<bool> keepPackages;                   // パッケージキャッシュ保持

    /**
     * @brief QVariantMapから部分更新内容を生成する
     * @param map 入力マップ (未知のキーは無視する)
     * @param out 生成先
     * @param err 型不一致時のエラー
     * @return 全キーの型が正しい場合true
     */
    static bool fromVariantMap(const QVariantMap &map, RepoChanges &out, QString &err) {
        return PartialUpdate::takeString(map, "name", out.name, err)
            && PartialUpdate::takeString(map, "url", out.url, err)
            && PartialUpdate::takeBool(map, "enabled", out.enabled, err)
            && PartialUpdate::takeBool(map, "autoRefresh", out.autoRefresh, err)
            && PartialUpdate::takeInt(map, "priority", out.priority, err)
            && PartialUpdate::takeBool(map, "keepPackages", out.keepPackages, err);
    }
};

} // namespace qZypper

Q_DECLARE_METATYPE(qZypper::RepoInfo)

#endif // QZYPPER_COMMON_REPOINFO_H
