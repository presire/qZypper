#ifndef QZYPPER_COMMON_RESOLVERINFO_H
#define QZYPPER_COMMON_RESOLVERINFO_H

#include <QList>
#include <QString>
#include <QVariant>
#include <QVariantList>
#include <QVariantMap>
#include <QMetaType>

namespace qZypper {

/**
 * @brief 依存関係コンフリクト情報
 */
struct ConflictInfo {
    QString description;                                // 問題の説明
    QString details;                                    // 詳細情報
    QList<QVariantMap> solutions;                       // 解決策リスト

    /**
     * @brief D-Bus転送用のQVariantMapへ変換する
     * @return 競合情報を持つマップ
     */
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
 * @brief ディスク使用量情報
 */
struct DiskUsageInfo {
    QString mountPoint;                                 // マウントポイント
    quint64 totalSize = 0;                              // 総容量 (bytes)
    quint64 usedSize = 0;                               // 使用量 (bytes)
    qint64  packageUsage = 0;                           // パッケージ変更による増減 (bytes, 負 = 削減)

    /**
     * @brief D-Bus転送用のQVariantMapへ変換する
     * @return ディスク使用量を持つマップ
     */
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

Q_DECLARE_METATYPE(qZypper::ConflictInfo)
Q_DECLARE_METATYPE(qZypper::DiskUsageInfo)

#endif // QZYPPER_COMMON_RESOLVERINFO_H
