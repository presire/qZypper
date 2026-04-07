#ifndef QZYPPER_COMMON_PACKAGEINFO_H
#define QZYPPER_COMMON_PACKAGEINFO_H

#include <QString>
#include <QVariantMap>
#include <QMetaType>
#include "Types.h"

namespace qZypper {

/**
 * @brief D-Bus経由で転送するパッケージ情報。
 */
struct PackageInfo {
    QString name;                                       // パッケージ名
    QString summary;                                    // 概要
    QString version;                                    // 候補バージョン
    QString installedVersion;                           // インストール済みバージョン
    QString arch;                                       // アーキテクチャ
    QString repoAlias;                                  // リポジトリエイリアス
    QString repoName;                                   // リポジトリ名
    quint64 size = 0;                                   // インストールサイズ (bytes)
    int status = static_cast<int>(PackageStatus::NoInst); // パッケージ状態

    /** @brief QVariantMap に変換する (D-Bus転送用)。 */
    QVariantMap toVariantMap() const {
        return {
            {"name",             name},
            {"summary",          summary},
            {"version",          version},
            {"installedVersion", installedVersion},
            {"arch",             arch},
            {"repoAlias",        repoAlias},
            {"repoName",         repoName},
            {"size",             QVariant::fromValue(size)},
            {"status",           status}
        };
    }

    /** @brief QVariantMap から PackageInfo を生成する。 */
    static PackageInfo fromVariantMap(const QVariantMap &map) {
        PackageInfo info;
        info.name             = map.value("name").toString();
        info.summary          = map.value("summary").toString();
        info.version          = map.value("version").toString();
        info.installedVersion = map.value("installedVersion").toString();
        info.arch             = map.value("arch").toString();
        info.repoAlias        = map.value("repoAlias").toString();
        info.repoName         = map.value("repoName").toString();
        info.size             = map.value("size").toULongLong();
        info.status           = map.value("status").toInt();
        return info;
    }
};

/**
 * @brief パッケージ詳細情報 (単一パッケージの全情報)。
 */
struct PackageDetails {
    PackageInfo basic;                                  // 基本情報
    QString description;                                // 説明文
    QString license;                                    // ライセンス
    QString group;                                      // パッケージグループ
    QString vendor;                                     // ベンダー
    QString buildHost;                                  // ビルドホスト
    QString buildTime;                                  // ビルド日時
    QString sourceRpm;                                  // ソースRPM名
    QString url;                                        // アップストリームURL
    QStringList provides;                               // 提供ケーパビリティ
    QStringList requires;                               // 依存ケーパビリティ
    QStringList conflicts;                              // 衝突ケーパビリティ
    QStringList obsoletes;                              // 廃止ケーパビリティ
    QStringList recommends;                             // 推奨ケーパビリティ
    QStringList suggests;                               // 提案ケーパビリティ
    QStringList fileList;                               // インストールファイル一覧
    QStringList changeLog;                              // 変更履歴
    QList<QVariantMap> availableVersions;                // 利用可能バージョン

    /** @brief QVariantMap に変換する (D-Bus転送用)。 */
    QVariantMap toVariantMap() const {
        QVariantMap map = basic.toVariantMap();
        map["description"]       = description;
        map["license"]           = license;
        map["group"]             = group;
        map["vendor"]            = vendor;
        map["buildHost"]         = buildHost;
        map["buildTime"]         = buildTime;
        map["sourceRpm"]         = sourceRpm;
        map["url"]               = url;
        map["provides"]          = provides;
        map["requires"]          = requires;
        map["conflicts"]         = conflicts;
        map["obsoletes"]         = obsoletes;
        map["recommends"]        = recommends;
        map["suggests"]          = suggests;
        map["fileList"]          = fileList;
        map["changeLog"]         = changeLog;
        QVariantList versions;
        for (const auto& v : availableVersions)
            versions.append(v);
        map["availableVersions"] = versions;
        return map;
    }
};

} // namespace qZypper

Q_DECLARE_METATYPE(qZypper::PackageInfo)
Q_DECLARE_METATYPE(qZypper::PackageDetails)

#endif // QZYPPER_COMMON_PACKAGEINFO_H
