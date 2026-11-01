#include <QDBusError>
#include <QString>
#include <QVariant>
#include <QVariantList>

#include <string>
#include <optional>
#include <vector>

#include "PackageManagerAdaptor.h"
#include "ZyppManager.h"

namespace qZypper {
namespace {
/**
 * @brief 旧形式とV2で共有するRPMグループ別の取得を実行する
 * @param group グループパス
 * @return 該当するパッケージ
 */
std::vector<PackageInfo> rpmGroupResults(const std::string &group)
{
    return ZyppManager::instance().getPackagesByRpmGroup(group);
}

/**
 * @brief 旧形式とV2で共有するロケール別の取得を実行する
 * @param locale ロケールコード
 * @return 該当するパッケージ
 */
std::vector<PackageInfo> localeResults(const std::string &locale)
{
    return ZyppManager::instance().getPackagesByLocale(locale);
}

/**
 * @brief グループパスの長さを検証して文字列へ変換する
 * @param path 入力されたグループパス
 * @return 有効なパス、空文字または256バイト超の場合はstd::nullopt
 */
std::optional<std::string> validGroupPath(const QString &path)
{
    if (path.isEmpty() || path.toUtf8().size() > 256) return std::nullopt;
    return path.toStdString();
}

/**
 * @brief ロケールコードの長さを検証して文字列へ変換する
 * @param code 入力されたロケールコード
 * @return 有効なコード、空文字または256バイト超の場合はstd::nullopt
 */
std::optional<std::string> validLocaleCode(const QString &code)
{
    if (code.isEmpty() || code.toUtf8().size() > 256) return std::nullopt;
    return code.toStdString();
}
}

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
    const std::optional<std::string> group = validGroupPath(path);
    if (!group) {
        // 不正な入力は認証を確認したうえで拒否する
        rejectListArguments(QStringLiteral("Invalid RPM group path"));
        return {};
    }

    return runRead([group]() -> QVariant {
        const std::vector<PackageInfo> packages = rpmGroupResults(*group);
        QVariantList list;
        list.reserve(static_cast<int>(packages.size()));
        for (const PackageInfo &info : packages)
            list.append(info.toVariantMap());
        return list;
    }).toList();
}

/**
 * @brief 指定RPMグループ配下のパッケージ一覧を構造化して返す
 * @param path グループパス
 * @return PackageListReply
 * @note 空文字または256バイトを超えるパスはInvalidArgsを返す
 */
PackageListReply PackageManagerAdaptor::GetPackagesByRpmGroupV2(const QString &path)
{
    const std::optional<std::string> group = validGroupPath(path);
    if (!group) {
        rejectListArguments(QStringLiteral("Invalid RPM group path"));
        return {};
    }

    return runRead([group] {
        return QVariant::fromValue(PackageListReply{toPackageRows(rpmGroupResults(*group))});
    }).value<PackageListReply>();
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
    const std::optional<std::string> locale = validLocaleCode(code);
    if (!locale) {
        // 不正な入力は認証を確認したうえで拒否する
        rejectListArguments(QStringLiteral("Invalid locale code"));
        return {};
    }

    return runRead([locale]() -> QVariant {
        const std::vector<PackageInfo> packages = localeResults(*locale);
        QVariantList list;
        list.reserve(static_cast<int>(packages.size()));
        for (const PackageInfo &info : packages)
            list.append(info.toVariantMap());
        return list;
    }).toList();
}

/**
 * @brief 指定ロケールに対応するパッケージ一覧を構造化して返す
 * @param code ロケールコード
 * @return PackageListReply
 * @note 空文字または256バイトを超えるコードはInvalidArgsを返す
 */
PackageListReply PackageManagerAdaptor::GetPackagesByLocaleV2(const QString &code)
{
    const std::optional<std::string> locale = validLocaleCode(code);
    if (!locale) {
        rejectListArguments(QStringLiteral("Invalid locale code"));
        return {};
    }

    return runRead([locale] {
        return QVariant::fromValue(PackageListReply{toPackageRows(localeResults(*locale))});
    }).value<PackageListReply>();
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
