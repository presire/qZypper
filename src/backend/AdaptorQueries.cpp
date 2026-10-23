#include <QDBusError>
#include "PackageManagerAdaptor.h"
#include "RepoPresets.h"
#include "ZyppManager.h"
#include "Types.h"

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

/** @brief リポジトリ一覧をワーカースレッドから取得する */
QVariantList PackageManagerAdaptor::GetRepos()
{
    return runRead([] { return maps(ZyppManager::instance().getRepos()); }).toList();
}

/**
 * @brief URL内のリポジトリ変数を展開したプレビュー文字列を返す (読み取り専用、ネットワーク不使用)
 * @param rawUrl 生のURL (2048バイト以内、制御文字は不可)
 * @return 展開後のURL (パスワードは"***"で伏せる)
 *         変数が展開されない場合や解析できない場合は空文字列
 */
QString PackageManagerAdaptor::GetExpandedRepoUrl(const QString &rawUrl)
{
    // 過大な文字列や制御文字を含む文字列は、libzyppのURL解析へ渡す前に拒否する
    // checkRead()が真の場合のみエラー応答を返し、権限のない呼び出し元には入力検証の詳細を明かさない
    if (!validRepoUrlInput(rawUrl)) {
        if (checkRead()) {
            sendErrorReply(QDBusError::InvalidArgs, QStringLiteral("Invalid repository URL"));
        }
        return {};
    }

    // expandRepoUrlPreviewはlibzyppのRepoVariablesUrlReplacerでURLを展開する (変数の値はベース製品から取得する)
    return runRead([rawUrl]() -> QVariant {
        return expandRepoUrlPreview(rawUrl);
    }).toString();
}

/**
 * @brief ベース製品に対応するコミュニティリポジトリのプリセットを返す (読み取り専用)
 * @return プリセットのマップ一覧、未対応のディストリビューションでは空のリスト
 */
QVariantList PackageManagerAdaptor::GetRepoPresets()
{
    // detectRepoPresetsはlibzyppのターゲットからベース製品を取得して対象ディストリビューションを判定する
    return runRead([]() -> QVariant {
        return detectRepoPresets();
    }).toList();
}

/** @brief サービス一覧をワーカースレッドから取得する */
QVariantList PackageManagerAdaptor::GetServices()
{
    return runRead([] { return maps(ZyppManager::instance().getServices()); }).toList();
}
/** @brief 指定フラグに従ってパッケージを検索する */
QVariantList PackageManagerAdaptor::SearchPackages(const QString &query, int flags)
{
    // 検索語はUTF-8で256バイト以内、フラグは下位8ビットの範囲に限り、範囲外は引数エラーとして扱う
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
/**
 * @brief 製品一覧を取得する (読み取り専用)
 * @return 製品情報のQVariantMap一覧
 */
QVariantList PackageManagerAdaptor::GetProducts()
{
    // 読み取り専用のためrunReadでワーカースレッド上のZyppManager::getProductsを呼ぶ
    return runRead([] { return maps(ZyppManager::instance().getProducts()); }).toList();
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
/** @brief カテゴリを検証したうえでパッチ一覧を取得する */
QVariantList PackageManagerAdaptor::GetPatches(int category)
{
    if (!isValidPatchCategory(category)) {
        if (checkRead()) sendErrorReply(QDBusError::InvalidArgs, QStringLiteral("Invalid patch category"));
        return {};
    }
    return runRead([category] { return maps(ZyppManager::instance().getPatches(category)); }).toList();
}
/** @brief 未確定の変更内容を取得する */
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
