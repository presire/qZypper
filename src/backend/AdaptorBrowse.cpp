#include <QDBusError>
#include <QString>
#include <QVariant>
#include <QVariantList>

#include <string>
#include <vector>

#include "PackageManagerAdaptor.h"
#include "ZyppManager.h"

namespace qZypper {

/**
 * @brief RPMグループのツリーを件数付きで返す
 * @return path/label/depth/countを持つマップのリスト
 */
QVariantList PackageManagerAdaptor::GetRpmGroups()
{
    return runRead([] { return QVariant(ZyppManager::instance().getRpmGroups()); }).toList();
}

/**
 * @brief 指定RPMグループ配下のパッケージ一覧を返す
 * @param path グループパス
 * @return パッケージ情報マップのリスト
 * @note 空文字または256バイトを超えるパスはInvalidArgsを返す
 */
QVariantList PackageManagerAdaptor::GetPackagesByRpmGroup(const QString &path)
{
    // 検証 : 空文字と長すぎる入力を拒否する
    if (path.isEmpty() || path.toUtf8().size() > 256) {
        if (checkRead())
            sendErrorReply(QDBusError::InvalidArgs, QStringLiteral("Invalid RPM group path"));
        return {};
    }

    const std::string group = path.toStdString();
    return runRead([group]() -> QVariant {
        const std::vector<PackageInfo> packages =
            ZyppManager::instance().getPackagesByRpmGroup(group);
        QVariantList list;
        list.reserve(static_cast<int>(packages.size()));
        for (const PackageInfo &info : packages)
            list.append(info.toVariantMap());
        return list;
    }).toList();
}

/**
 * @brief 利用可能・要求済みロケールの一覧を返す
 * @return code/name/requested/availableを持つマップのリスト
 */
QVariantList PackageManagerAdaptor::GetLocales()
{
    return runRead([] { return QVariant(ZyppManager::instance().getLocales()); }).toList();
}

/**
 * @brief 指定ロケールに対応するパッケージ一覧を返す
 * @param code ロケールコード
 * @return パッケージ情報マップのリスト
 * @note 空文字または256バイトを超えるコードはInvalidArgsを返す
 */
QVariantList PackageManagerAdaptor::GetPackagesByLocale(const QString &code)
{
    // 検証 : 空文字と長すぎる入力を拒否する
    if (code.isEmpty() || code.toUtf8().size() > 256) {
        if (checkRead())
            sendErrorReply(QDBusError::InvalidArgs, QStringLiteral("Invalid locale code"));
        return {};
    }

    const std::string locale = code.toStdString();
    return runRead([locale]() -> QVariant {
        const std::vector<PackageInfo> packages =
            ZyppManager::instance().getPackagesByLocale(locale);
        QVariantList list;
        list.reserve(static_cast<int>(packages.size()));
        for (const PackageInfo &info : packages)
            list.append(info.toVariantMap());
        return list;
    }).toList();
}

/**
 * @brief 保存済み状態との差分として未反映のロケール変更を返す
 * @return code/name/actionを持つマップのリスト
 */
QVariantList PackageManagerAdaptor::GetPendingLocaleChanges()
{
    return runRead([] {
        return QVariant(ZyppManager::instance().getPendingLocaleChanges());
    }).toList();
}

/**
 * @brief 指定ロケールの要求状態を変更する
 * @param code ロケールコード
 * @param requested 要求する場合はtrue
 * @return 変更できた場合はtrue
 */
bool PackageManagerAdaptor::SetLocaleRequested(const QString &code, bool requested)
{
    if (code.isEmpty() || code.toUtf8().size() > 256) {
        if (checkNotBusy() && checkOwner())
            sendErrorReply(QDBusError::InvalidArgs, QStringLiteral("Invalid locale code"));
        return false;
    }

    const std::string locale = code.toStdString();
    return runOwnerSync([locale, requested] {
        return QVariant(ZyppManager::instance().setLocaleRequested(locale, requested));
    }).toBool();
}

} // namespace qZypper
