#ifndef QZYPPER_COMMON_PACKAGEINFO_H
#define QZYPPER_COMMON_PACKAGEINFO_H

#include <QString>
#include <QVariantMap>
#include <QMetaType>
#include "Types.h"

namespace qZypper {

/**
 * @brief D-Bus経由で転送するパッケージ情報
 */
struct PackageInfo {
    QString name;                                          // パッケージ名
    QString summary;                                       // 概要
    QString version;                                       // 候補バージョン
    QString installedVersion;                              // インストール済みバージョン
    QString arch;                                          // アーキテクチャ
    QString repoAlias;                                     // リポジトリエイリアス
    QString repoName;                                      // リポジトリ名
    quint64 size = 0;                                      // インストールサイズ (bytes)
    int status = static_cast<int>(PackageStatus::NoInst);  // パッケージ状態
    bool updatable = false;                                // より新しい更新候補があるか

    /**
     * @brief D-Bus転送用のQVariantMapへ変換する
     * @return パッケージ属性を持つマップ
     */
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
            {"status",           status},
            {"updatable",        updatable}
        };
    }

    /**
     * @brief D-Busで受信したQVariantMapから生成する
     * @param map パッケージ属性を持つマップ
     * @return 復元したパッケージ情報
     */
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
        info.updatable        = map.value("updatable").toBool();
        return info;
    }
};

/**
 * @brief パッケージ詳細情報 (単一パッケージの全情報)
 */
struct PackageDetails {
    PackageInfo basic;                                  // 基本情報
    QString description;                                // 説明文
    QString license;                                    // ライセンス
    QString group;                                      // パッケージグループ
    QString vendor;                                     // ベンダー
    QString buildHost;                                  // ビルドホスト
    QString buildTime;                                  // ビルド日時 (ISO-8601、未設定は空)
    QString sourceRpm;                                  // ソースRPM名
    QString url;                                        // アップストリームURL
    qint64 installSize = 0;                             // インストールサイズ (bytes)
    qint64 downloadSize = 0;                            // ダウンロードサイズ (bytes)
    QString installTime;                                // インストール日時 (ISO-8601、未インストールは空)
    bool multiversion = false;                          // 複数版同時インストール可能か
    bool mixedMultiversion = false;                     // 一部の版のみ複数版インストールに対応するか
    QStringList provides;                               // 提供ケーパビリティ
    QStringList requirements;                           // 依存ケーパビリティ (D-Busキーはrequires)
    QStringList conflicts;                              // 衝突ケーパビリティ
    QStringList obsoletes;                              // 廃止ケーパビリティ
    QStringList recommends;                             // 推奨ケーパビリティ
    QStringList suggests;                               // 提案ケーパビリティ
    QStringList fileList;                               // インストールファイル一覧
    QStringList changeLog;                              // 変更履歴
    QList<QVariantMap> availableVersions;               // 利用可能バージョン

    /**
     * @brief D-Bus転送用のQVariantMapへ変換する
     * @return 基本情報と詳細属性を持つマップ
     */
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
        map["installSize"]       = QVariant::fromValue(installSize);
        map["downloadSize"]      = QVariant::fromValue(downloadSize);
        map["installTime"]       = installTime;
        map["multiversion"]      = multiversion;
        map["mixedMultiversion"] = mixedMultiversion;
        map["provides"]          = provides;
        map["requires"]          = requirements;
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

    /**
     * @brief D-Busで受信したQVariantMapから生成する
     * @param map 基本情報と詳細属性を持つマップ
     * @return 復元したパッケージ詳細情報
     */
    static PackageDetails fromVariantMap(const QVariantMap &map) {
        PackageDetails details;
        details.basic           = PackageInfo::fromVariantMap(map);
        details.description     = map.value("description").toString();
        details.license         = map.value("license").toString();
        details.group           = map.value("group").toString();
        details.vendor          = map.value("vendor").toString();
        details.buildHost       = map.value("buildHost").toString();
        details.buildTime       = map.value("buildTime").toString();
        details.sourceRpm       = map.value("sourceRpm").toString();
        details.url             = map.value("url").toString();
        details.installSize     = map.value("installSize").toLongLong();
        details.downloadSize    = map.value("downloadSize").toLongLong();
        details.installTime     = map.value("installTime").toString();
        details.multiversion    = map.value("multiversion").toBool();
        details.mixedMultiversion = map.value("mixedMultiversion").toBool();
        details.provides        = map.value("provides").toStringList();
        details.requirements    = map.value("requires").toStringList();
        details.conflicts       = map.value("conflicts").toStringList();
        details.obsoletes       = map.value("obsoletes").toStringList();
        details.recommends      = map.value("recommends").toStringList();
        details.suggests        = map.value("suggests").toStringList();
        details.fileList        = map.value("fileList").toStringList();
        details.changeLog       = map.value("changeLog").toStringList();
        const QVariantList versions = map.value("availableVersions").toList();
        for (const auto &version : versions) {
            details.availableVersions.append(version.toMap());
        }
        return details;
    }
};

} // namespace qZypper

Q_DECLARE_METATYPE(qZypper::PackageInfo)
Q_DECLARE_METATYPE(qZypper::PackageDetails)

#endif // QZYPPER_COMMON_PACKAGEINFO_H
