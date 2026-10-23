/**
 * @file LicenseConfirmationResult.h
 * @brief コミット前ライセンス確認のD-Bus転送結果を定義する
 *
 * ZyppManagerとPackageControllerがD-Bus経由で確認結果と拒否候補を受け渡す
 *
 * 入力 : ライセンス確認の成否、選択変更、承認数、エラー内容
 * 出力 : QVariantMap形式のD-Bus転送値
 * 依存 : Qt Core
 */
#ifndef QZYPPER_COMMON_LICENSECONFIRMATIONRESULT_H
#define QZYPPER_COMMON_LICENSECONFIRMATIONRESULT_H

#include <QMetaType>
#include <QStringList>
#include <QVariantMap>

namespace qZypper {

/**
 * @struct LicenseConfirmationResult
 * @brief コミット前のライセンス確認結果を保持する
 *
 * 拒否で選択が変化したかと、GUIへ表示する拒否パッケージをD-Busで転送する
 */
struct LicenseConfirmationResult {
    bool success = false;                 // 確認処理が完了したか
    bool changed = false;                 // 拒否で選択が変わったか
    QStringList rejectedPackages;         // 今回拒否したパッケージ
    int acceptedCount = 0;                // 今回承認した件数
    QString errorMessage;                 // 準備失敗の説明

    /**
     * @brief 結果をD-Bus転送用のマップへ変換する
     * @return success、changed、rejectedPackages、acceptedCount、errorMessageを持つマップ
     */
    QVariantMap toVariantMap() const
    {
        return {{QStringLiteral("success"), success}, {QStringLiteral("changed"), changed},
                {QStringLiteral("rejectedPackages"), rejectedPackages},
                {QStringLiteral("acceptedCount"), acceptedCount},
                {QStringLiteral("errorMessage"), errorMessage}};
    }

    /**
     * @brief D-Busで受信したマップから結果を復元する
     * @param map D-Busで受信した結果マップ
     * @return 復元したライセンス確認結果
     */
    static LicenseConfirmationResult fromVariantMap(const QVariantMap &map)
    {
        LicenseConfirmationResult result;
        result.success = map.value(QStringLiteral("success")).toBool();
        result.changed = map.value(QStringLiteral("changed")).toBool();
        result.rejectedPackages = map.value(QStringLiteral("rejectedPackages")).toStringList();
        result.acceptedCount = map.value(QStringLiteral("acceptedCount")).toInt();
        result.errorMessage = map.value(QStringLiteral("errorMessage")).toString();
        return result;
    }
};

} // namespace qZypper

Q_DECLARE_METATYPE(qZypper::LicenseConfirmationResult)

#endif // QZYPPER_COMMON_LICENSECONFIRMATIONRESULT_H
