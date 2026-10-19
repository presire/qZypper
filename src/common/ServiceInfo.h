#ifndef QZYPPER_COMMON_SERVICEINFO_H
#define QZYPPER_COMMON_SERVICEINFO_H

#include <optional>
#include <QString>
#include <QVariantMap>
#include <QMetaType>
#include "PartialUpdate.h"

namespace qZypper {

/**
 * @brief D-Bus経由で転送するサービス情報
 */
struct ServiceInfo {
    QString alias;                                      // サービスエイリアス
    QString name;                                       // 表示名
    QString url;                                        // サービスURL
    bool enabled = true;                                // 有効フラグ
    bool autoRefresh = true;                            // 自動リフレッシュ
    QString type;                                       // サービスタイプ ("ris", "plugin"等)

    /**
     * @brief D-Bus転送用のQVariantMapへ変換する
     * @return サービス属性を持つマップ
     */
    QVariantMap toVariantMap() const {
        return {
            {"alias",       alias},
            {"name",        name},
            {"url",         url},
            {"enabled",     enabled},
            {"autoRefresh", autoRefresh},
            {"type",        type}
        };
    }

    /**
     * @brief D-Busで受信したQVariantMapから生成する
     *
     * 渡されなかったキーはメンバの既定値を維持する (falseに倒さない)
     *
     * @param map サービス属性を持つマップ
     * @return 復元したサービス情報
     */
    static ServiceInfo fromVariantMap(const QVariantMap &map) {
        ServiceInfo info;
        info.alias       = map.value("alias").toString();
        info.name        = map.value("name").toString();
        info.url         = map.value("url").toString();
        info.enabled     = map.value("enabled", info.enabled).toBool();
        info.autoRefresh = map.value("autoRefresh", info.autoRefresh).toBool();
        info.type        = map.value("type").toString();
        return info;
    }
};

/**
 * @brief サービスの部分更新内容 (ModifyService用)
 *
 * 値を持つメンバだけを適用する
 * 渡されなかったキーは現状維持となる
 */
struct ServiceChanges {
    std::optional<QString> name;                        // 表示名 (空文字は現状維持)
    std::optional<QString> url;                         // サービスURL (空文字は現状維持)
    std::optional<bool> enabled;                        // 有効フラグ
    std::optional<bool> autoRefresh;                    // 自動リフレッシュ

    /**
     * @brief QVariantMapから部分更新内容を生成する
     * @param map 入力マップ (未知のキーは無視する)
     * @param out 生成先
     * @param err 型不一致時のエラー
     * @return 全キーの型が正しい場合はtrue
     */
    static bool fromVariantMap(const QVariantMap &map, ServiceChanges &out, QString &err) {
        return PartialUpdate::takeString(map, "name", out.name, err)
            && PartialUpdate::takeString(map, "url", out.url, err)
            && PartialUpdate::takeBool(map, "enabled", out.enabled, err)
            && PartialUpdate::takeBool(map, "autoRefresh", out.autoRefresh, err);
    }
};

} // namespace qZypper

Q_DECLARE_METATYPE(qZypper::ServiceInfo)

#endif // QZYPPER_COMMON_SERVICEINFO_H
