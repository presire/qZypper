#ifndef QZYPPER_COMMON_REPOINFO_H
#define QZYPPER_COMMON_REPOINFO_H

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

    /** @brief QVariantMap から RepoInfo を生成する。 */
    static RepoInfo fromVariantMap(const QVariantMap &map) {
        RepoInfo info;
        info.alias        = map.value("alias").toString();
        info.name         = map.value("name").toString();
        info.url          = map.value("url").toString();
        info.rawUrl       = map.value("rawUrl").toString();
        info.enabled      = map.value("enabled").toBool();
        info.autoRefresh  = map.value("autoRefresh").toBool();
        info.priority     = map.value("priority").toInt();
        info.type         = map.value("type").toString();
        info.keepPackages = map.value("keepPackages").toBool();
        info.service      = map.value("service").toString();
        return info;
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

    /** @brief QVariantMap から ServiceInfo を生成する。 */
    static ServiceInfo fromVariantMap(const QVariantMap &map) {
        ServiceInfo info;
        info.alias       = map.value("alias").toString();
        info.name        = map.value("name").toString();
        info.url         = map.value("url").toString();
        info.enabled     = map.value("enabled").toBool();
        info.autoRefresh = map.value("autoRefresh").toBool();
        info.type        = map.value("type").toString();
        return info;
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
