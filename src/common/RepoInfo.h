#ifndef QZYPPER_COMMON_REPOINFO_H
#define QZYPPER_COMMON_REPOINFO_H

#include <cmath>
#include <limits>
#include <optional>
#include <QString>
#include <QVariantMap>
#include <QMetaType>

namespace qZypper {

/**
 * @brief D-Bus経由で転送するリポジトリ情報。
 */
struct RepoInfo {
    QString alias;                                      // リポジトリエイリアス
    QString name;                                       // 表示名
    QString url;                                        // ベースURL (変数展開済み)
    QString rawUrl;                                     // 生ベースURL (${releasever}等を含む)
    bool enabled = true;                                // 有効フラグ
    bool autoRefresh = true;                            // 自動リフレッシュ
    int priority = 99;                                  // 優先度 (1-99, 小さいほど高)
    QString type;                                       // リポタイプ ("rpm-md", "yast2" 等)
    bool keepPackages = false;                          // パッケージキャッシュ保持
    QString service;                                    // 所属サービスエイリアス

    /** @brief QVariantMap に変換する (D-Bus転送用)。 */
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
            {"service",      service}
        };
    }

    /**
     * @brief QVariantMap から RepoInfo を生成する。
     *
     * 渡されなかったキーはメンバの既定値を維持する (false / 0 に倒さない)。
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
        return info;
    }
};

/**
 * @brief 部分更新用ヘルパー (D-Bus a{sv} の型検証付き取り出し)。
 */
namespace PartialUpdate {

/**
 * @brief キーが存在すれば bool として取り出す。
 * @param map 入力マップ
 * @param key キー
 * @param out 取り出し先 (キーが無ければ変更しない)
 * @param err 型不一致時のエラー
 * @return 型が正しい (またはキー無し) 場合 true
 */
inline bool takeBool(const QVariantMap &map, const char *key,
                     std::optional<bool> &out, QString &err)
{
    const auto it = map.constFind(QLatin1String(key));
    if (it == map.constEnd())
        return true;
    if (it->typeId() != QMetaType::Bool) {
        err = QStringLiteral("Property '%1' must be a boolean").arg(QLatin1String(key));
        return false;
    }
    out = it->toBool();
    return true;
}

/**
 * @brief キーが存在すれば整数として取り出す (浮動小数・文字列は拒否)。
 * @param map 入力マップ
 * @param key キー
 * @param out 取り出し先 (キーが無ければ変更しない)
 * @param err 型不一致時のエラー
 * @return 型が正しい (またはキー無し) 場合 true
 */
inline bool takeInt(const QVariantMap &map, const char *key,
                    std::optional<qlonglong> &out, QString &err)
{
    const auto it = map.constFind(QLatin1String(key));
    if (it == map.constEnd())
        return true;
    switch (it->typeId()) {
    case QMetaType::Int:
    case QMetaType::UInt:
    case QMetaType::LongLong:
    case QMetaType::Short:
    case QMetaType::UShort:
    case QMetaType::UChar:
        out = it->toLongLong();
        return true;
    case QMetaType::ULongLong:
        if (it->toULongLong() > static_cast<qulonglong>(std::numeric_limits<qlonglong>::max()))
            break;
        out = it->toLongLong();
        return true;
    case QMetaType::Double: {
        // QML の数値は double で届くことがあるため、整数値で表せる範囲のみ受け付ける
        const double value = it->toDouble();
        if (!std::isfinite(value) || std::trunc(value) != value
            || value < static_cast<double>(std::numeric_limits<int>::min())
            || value > static_cast<double>(std::numeric_limits<int>::max()))
            break;
        out = static_cast<qlonglong>(value);
        return true;
    }
    default:
        break;
    }
    err = QStringLiteral("Property '%1' must be an integer").arg(QLatin1String(key));
    return false;
}

/**
 * @brief キーが存在すれば文字列として取り出す。
 * @param map 入力マップ
 * @param key キー
 * @param out 取り出し先 (キーが無ければ変更しない)
 * @param err 型不一致時のエラー
 * @return 型が正しい (またはキー無し) 場合 true
 */
inline bool takeString(const QVariantMap &map, const char *key,
                       std::optional<QString> &out, QString &err)
{
    const auto it = map.constFind(QLatin1String(key));
    if (it == map.constEnd())
        return true;
    if (it->typeId() != QMetaType::QString) {
        err = QStringLiteral("Property '%1' must be a string").arg(QLatin1String(key));
        return false;
    }
    out = it->toString();
    return true;
}

} // namespace PartialUpdate

/**
 * @brief リポジトリの部分更新内容 (ModifyRepo 用)。
 *
 * 値を持つメンバだけを適用する。渡されなかったキーは現状維持となる。
 */
struct RepoChanges {
    std::optional<QString> name;                        // 表示名 (空文字は現状維持)
    std::optional<QString> url;                         // ベースURL (空文字は現状維持)
    std::optional<bool> enabled;                        // 有効フラグ
    std::optional<bool> autoRefresh;                    // 自動リフレッシュ
    std::optional<qlonglong> priority;                  // 優先度 (範囲検証は ZyppManager 側)
    std::optional<bool> keepPackages;                   // パッケージキャッシュ保持

    /**
     * @brief QVariantMap から部分更新内容を生成する。
     * @param map 入力マップ (未知のキーは無視する)
     * @param out 生成先
     * @param err 型不一致時のエラー
     * @return 全キーの型が正しい場合 true
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

/**
 * @brief D-Bus経由で転送するサービス情報。
 */
struct ServiceInfo {
    QString alias;                                      // サービスエイリアス
    QString name;                                       // 表示名
    QString url;                                        // サービスURL
    bool enabled = true;                                // 有効フラグ
    bool autoRefresh = true;                            // 自動リフレッシュ
    QString type;                                       // サービスタイプ ("ris", "plugin" 等)

    /** @brief QVariantMap に変換する (D-Bus転送用)。 */
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
     * @brief QVariantMap から ServiceInfo を生成する。
     *
     * 渡されなかったキーはメンバの既定値を維持する (false に倒さない)。
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
 * @brief サービスの部分更新内容 (ModifyService 用)。
 *
 * 値を持つメンバだけを適用する。渡されなかったキーは現状維持となる。
 */
struct ServiceChanges {
    std::optional<QString> name;                        // 表示名 (空文字は現状維持)
    std::optional<QString> url;                         // サービスURL (空文字は現状維持)
    std::optional<bool> enabled;                        // 有効フラグ
    std::optional<bool> autoRefresh;                    // 自動リフレッシュ

    /**
     * @brief QVariantMap から部分更新内容を生成する。
     * @param map 入力マップ (未知のキーは無視する)
     * @param out 生成先
     * @param err 型不一致時のエラー
     * @return 全キーの型が正しい場合 true
     */
    static bool fromVariantMap(const QVariantMap &map, ServiceChanges &out, QString &err) {
        return PartialUpdate::takeString(map, "name", out.name, err)
            && PartialUpdate::takeString(map, "url", out.url, err)
            && PartialUpdate::takeBool(map, "enabled", out.enabled, err)
            && PartialUpdate::takeBool(map, "autoRefresh", out.autoRefresh, err);
    }
};

/**
 * @brief パターン情報。
 */
struct PatternInfo {
    QString name;                                       // パターン名
    QString summary;                                    // 概要
    QString description;                                // 説明文
    QString category;                                   // カテゴリ
    QString icon;                                       // アイコンパス
    int status = 0;                                     // パッケージ状態
    int order = 0;                                      // 表示順序

    /** @brief QVariantMap に変換する (D-Bus転送用)。 */
    QVariantMap toVariantMap() const {
        return {
            {"name",        name},
            {"summary",     summary},
            {"description", description},
            {"category",    category},
            {"icon",        icon},
            {"status",      status},
            {"order",       order}
        };
    }
};

/**
 * @brief パッチ情報。
 */
struct PatchInfo {
    QString name;                                       // パッチ名
    QString summary;                                    // 概要
    QString category;                                   // カテゴリ (security, recommended 等)
    QString severity;                                   // 深刻度
    QString version;                                    // バージョン
    int status = 0;                                     // パッケージ状態
    bool interactive = false;                           // 対話操作が必要か

    /** @brief QVariantMap に変換する (D-Bus転送用)。 */
    QVariantMap toVariantMap() const {
        return {
            {"name",        name},
            {"summary",     summary},
            {"category",    category},
            {"severity",    severity},
            {"version",     version},
            {"status",      status},
            {"interactive", interactive}
        };
    }
};

/**
 * @brief 依存関係コンフリクト情報。
 */
struct ConflictInfo {
    QString description;                                // 問題の説明
    QString details;                                    // 詳細情報
    QList<QVariantMap> solutions;                        // 解決策リスト

    /** @brief QVariantMap に変換する (D-Bus転送用)。 */
    QVariantMap toVariantMap() const {
        QVariantList solList;
        for (const auto& sol : solutions)
            solList.append(sol);
        return {
            {"description", description},
            {"details",     details},
            {"solutions",   solList}
        };
    }
};

/**
 * @brief ディスク使用量情報。
 */
struct DiskUsageInfo {
    QString mountPoint;                                 // マウントポイント
    quint64 totalSize = 0;                              // 総容量 (bytes)
    quint64 usedSize = 0;                               // 使用量 (bytes)
    qint64  packageUsage = 0;                            // パッケージ変更による増減 (bytes, 負=削減)

    /** @brief QVariantMap に変換する (D-Bus転送用)。 */
    QVariantMap toVariantMap() const {
        return {
            {"mountPoint",   mountPoint},
            {"totalSize",    QVariant::fromValue(totalSize)},
            {"usedSize",     QVariant::fromValue(usedSize)},
            {"packageUsage", QVariant::fromValue(packageUsage)}
        };
    }
};

} // namespace qZypper

Q_DECLARE_METATYPE(qZypper::RepoInfo)
Q_DECLARE_METATYPE(qZypper::ServiceInfo)
Q_DECLARE_METATYPE(qZypper::PatternInfo)
Q_DECLARE_METATYPE(qZypper::PatchInfo)
Q_DECLARE_METATYPE(qZypper::ConflictInfo)
Q_DECLARE_METATYPE(qZypper::DiskUsageInfo)

#endif // QZYPPER_COMMON_REPOINFO_H
