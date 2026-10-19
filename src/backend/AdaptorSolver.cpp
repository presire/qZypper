#include <QDBusError>
#include "PackageManagerAdaptor.h"
#include "SolverSettingsParse.h"
#include "ZyppManager.h"

namespace qZypper {

/**
 * @brief 有効なソルバー設定とzypp.conf既定値を返す
 * @return installRecommends/cleandepsOnRemove/allowVendorChangeとdefaultsを持つマップ
 */
QVariantMap PackageManagerAdaptor::GetSolverSettings()
{
    return runRead([] { return QVariant(ZyppManager::instance().getSolverSettings()); }).toMap();
}

/**
 * @brief 指定したソルバー設定だけを上書きする
 * @param settings 上書きする項目を持つマップ
 * @return 設定を反映した場合はtrue (遅延返信)、検証失敗時はInvalidArgs、リゾルバへの適用失敗時はFailedエラーを返す
 */
bool PackageManagerAdaptor::SetSolverSettings(const QVariantMap &settings)
{
    ZyppManager::SolverOverrides overrides;
    QString error;

    if (!parseSolverSettings(settings, overrides, error)) {
        if (checkNotBusy() && checkOwner()) {
            sendErrorReply(QDBusError::InvalidArgs, error);
        }
        return false;
    }

    if (!checkNotBusy() || !checkOwner() || !checkArguments()) {
        return false;
    }

    startWorker(message(), [overrides] {
        AsyncOutcome result;
        auto &manager = ZyppManager::instance();
        result.value = manager.setSolverSettings(overrides);

        if (!result.value.toBool()) {
            result.errorName = QStringLiteral("org.freedesktop.DBus.Error.Failed");
            result.errorText = QString::fromStdString(manager.lastError());
        }

        return result;
    });
    return false;
}

} // namespace qZypper
