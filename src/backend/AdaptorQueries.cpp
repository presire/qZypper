#include <QDBusError>
#include "PackageManagerAdaptor.h"
#include "ZyppManager.h"

namespace qZypper {
namespace {
/**
 * @brief toVariantMap()を持つ要素配列をD-Bus用リストへ変換する
 * @tparam T toVariantMap()を提供する要素型
 * @param items 変換する要素配列
 * @return 各要素をa{sv}へ変換したリスト
 */
template<typename T>
QVariant maps(const std::vector<T> &items)
{
    QVariantList result;
    for (const auto &item : items) result.append(item.toVariantMap());
    return result;
}
}

/** @brief リポジトリ一覧をワーカーから取得する */
QVariantList PackageManagerAdaptor::GetRepos()
{
    return runRead([] { return maps(ZyppManager::instance().getRepos()); }).toList();
}
/** @brief サービス一覧をワーカーから取得する */
QVariantList PackageManagerAdaptor::GetServices()
{
    return runRead([] { return maps(ZyppManager::instance().getServices()); }).toList();
}
/** @brief リテラル検索をワーカー上で行う */
QVariantList PackageManagerAdaptor::SearchPackages(const QString &query, int flags)
{
    if (query.toUtf8().size() > 256 || flags < 0 || (flags & ~255)) {
        if (checkRead()) sendErrorReply(QDBusError::InvalidArgs, QStringLiteral("Invalid search query or flags"));
        return {};
    }
    return runRead([query, flags] { return maps(ZyppManager::instance().searchPackages(query.toStdString(), flags)); }).toList();
}
/** @brief パッケージ詳細を取得する */
QVariantMap PackageManagerAdaptor::GetPackageDetails(const QString &name)
{
    return runRead([name] { return QVariant(ZyppManager::instance().getPackageDetails(name.toStdString()).toVariantMap()); }).toMap();
}
/** @brief リポジトリ別パッケージを取得する */
QVariantList PackageManagerAdaptor::GetPackagesByRepo(const QString &alias)
{
    return runRead([alias] { return maps(ZyppManager::instance().getPackagesByRepo(alias.toStdString())); }).toList();
}
/** @brief パターン一覧を取得する */
QVariantList PackageManagerAdaptor::GetPatterns()
{
    return runRead([] { return maps(ZyppManager::instance().getPatterns()); }).toList();
}
/** @brief パターン別パッケージを取得する */
QVariantList PackageManagerAdaptor::GetPackagesByPattern(const QString &name)
{
    return runRead([name] { return maps(ZyppManager::instance().getPackagesByPattern(name.toStdString())); }).toList();
}
/** @brief 検証済みカテゴリのパッチを取得する */
QVariantList PackageManagerAdaptor::GetPatches(int category)
{
    if (category < -1 || category > 4) {
        if (checkRead()) sendErrorReply(QDBusError::InvalidArgs, QStringLiteral("Invalid patch category"));
        return {};
    }
    return runRead([category] { return maps(ZyppManager::instance().getPatches(category)); }).toList();
}
/** @brief 選択内容を取得する */
QVariantList PackageManagerAdaptor::GetPendingChanges()
{
    return runRead([] { return maps(ZyppManager::instance().getPendingChanges()); }).toList();
}
/** @brief ディスク使用量を取得する */
QVariantList PackageManagerAdaptor::GetDiskUsage()
{
    return runRead([] { return maps(ZyppManager::instance().getDiskUsage()); }).toList();
}

} // namespace qZypper
