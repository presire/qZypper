/**
 * @file LicensePreparation.h
 * @brief ライセンス拒否後のコミット準備を検査する補助関数
 *
 * PackageControllerが依存関係の再解決後に、拒否した候補が再選択されていないか確認する
 *
 * 入力 : 拒否済みパッケージ名と現在の変更予定
 * 出力 : 再び導入または更新対象になったパッケージ名
 * 依存 : Types、Qt Core
 */
#ifndef QZYPPER_COMMON_LICENSEPREPARATION_H
#define QZYPPER_COMMON_LICENSEPREPARATION_H

#include "Types.h"
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

namespace qZypper {

/**
 * @brief 再解決で導入または更新対象へ戻った拒否済みパッケージを返す
 * @param rejected ライセンスを拒否したパッケージ名
 * @param pending 現在の変更予定を表すマップの一覧
 * @return 再選択された拒否済みパッケージ名の重複なし一覧
 */
inline QStringList reintroducedLicensePackages(const QStringList &rejected, const QVariantList &pending)
{
    QStringList result;
    for (const auto &value : pending) {
        const auto item = value.toMap();
        const auto name = item.value(QStringLiteral("name")).toString();
        const int status = item.value(QStringLiteral("status"), -1).toInt();
        if (rejected.contains(name)
            && (status == static_cast<int>(PackageStatus::Install)
                || status == static_cast<int>(PackageStatus::AutoInstall)
                || status == static_cast<int>(PackageStatus::Update)
                || status == static_cast<int>(PackageStatus::AutoUpdate))) {
            result.append(name);
        }
    }
    result.removeDuplicates();
    return result;
}

} // namespace qZypper

#endif // QZYPPER_COMMON_LICENSEPREPARATION_H
