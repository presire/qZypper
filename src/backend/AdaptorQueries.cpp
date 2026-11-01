#include <QDBusError>
#include "PackageManagerAdaptor.h"
#include "RepoPolicy.h"
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

/**
 * @brief 旧形式とV2に共通する検索条件の範囲を検証する
 * @param query 検索語 (UTF-8で256バイト以内)
 * @param flags 下位8ビットの検索フラグ
 * @return 条件が有効ならtrue
 */
bool validSearchArguments(const QString &query, int flags)
{
    return query.toUtf8().size() <= 256 && flags >= 0 && !(flags & ~255);
}

constexpr int kMaxNamesPerRequest = 5000;  // GetPackagesByNamesの1回あたりの名前数上限
constexpr int kMaxPackageNameBytes = 512;   // パッケージ名1件のUTF-8上限バイト数

/**
 * @brief 名前一覧の件数と各要素が範囲内か判定する
 * @param names 検証するパッケージ名の一覧
 * @return 空要素や長すぎる名前、件数超過がなければtrue
 */
bool validPackageNames(const QStringList &names)
{
    if (names.size() > kMaxNamesPerRequest)
        return false;
    for (const QString &name : names) {
        if (name.isEmpty() || name.toUtf8().size() > kMaxPackageNameBytes)
            return false;
    }
    return true;
}

/**
 * @brief 旧形式とV2で共有する検索を実行する
 * @param query 検索語
 * @param flags 検索フラグ
 * @return 検索結果
 */
std::vector<PackageInfo> searchResults(const std::string &query, int flags)
{
    return ZyppManager::instance().searchPackages(query, flags);
}

/**
 * @brief 旧形式とV2で共有するリポジトリ別の取得を実行する
 * @param alias リポジトリエイリアス
 * @return 該当するパッケージ
 */
std::vector<PackageInfo> repoResults(const std::string &alias)
{
    return ZyppManager::instance().getPackagesByRepo(alias);
}

/**
 * @brief サービスに属するリポジトリのパッケージを取得する
 * @param alias サービスエイリアス
 * @return 該当するパッケージ
 */
std::vector<PackageInfo> serviceResults(const std::string &alias)
{
    return ZyppManager::instance().getPackagesByService(alias);
}

/**
 * @brief 旧形式とV2で共有するパターン別の取得を実行する
 * @param name パターン名
 * @return 該当するパッケージ
 */
std::vector<PackageInfo> patternResults(const std::string &name)
{
    return ZyppManager::instance().getPackagesByPattern(name);
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
    if (!validSearchArguments(query, flags)) {
        rejectListArguments(QStringLiteral("Invalid search query or flags"));
        return {};
    }
    return runRead([query, flags] { return maps(searchResults(query.toStdString(), flags)); }).toList();
}

/**
 * @brief 指定フラグに従ってパッケージを検索し、構造化した一覧で返す
 * @param query 検索語
 * @param flags 検索フラグ
 * @return リポジトリ表とパッケージ行の一覧応答
 * @note 不正な検索条件は読み取り権限を確認してからInvalidArgsを返す
 */
PackageListReply PackageManagerAdaptor::SearchPackagesV2(const QString &query, int flags)
{
    // 検証は旧メソッドと同一の条件を共有する
    if (!validSearchArguments(query, flags)) {
        rejectListArguments(QStringLiteral("Invalid search query or flags"));
        return {};
    }
    return runRead([query, flags] {
        return QVariant::fromValue(PackageListReply{toPackageRows(searchResults(query.toStdString(), flags))});
    }).value<PackageListReply>();
}
/** @brief パッケージ詳細を取得する */
QVariantMap PackageManagerAdaptor::GetPackageDetails(const QString &name)
{
    return runRead([name] { return QVariant(ZyppManager::instance().getPackageDetails(name.toStdString()).toVariantMap()); }).toMap();
}
/** @brief リポジトリ別パッケージを取得する */
QVariantList PackageManagerAdaptor::GetPackagesByRepo(const QString &alias)
{
    return runRead([alias] { return maps(repoResults(alias.toStdString())); }).toList();
}

/**
 * @brief リポジトリ別パッケージを構造化した一覧で取得する
 * @param alias リポジトリエイリアス
 * @return リポジトリ表とパッケージ行の一覧応答
 */
PackageListReply PackageManagerAdaptor::GetPackagesByRepoV2(const QString &alias)
{
    return runRead([alias] {
        return QVariant::fromValue(PackageListReply{toPackageRows(repoResults(alias.toStdString()))});
    }).value<PackageListReply>();
}

/**
 * @brief サービスに属するリポジトリが提供するパッケージを構造化した一覧で取得する
 * @param alias サービスエイリアス
 * @return パッケージ一覧の構造体
 * @note 不正なエイリアスはInvalidArgsを返す
 */
PackageListReply PackageManagerAdaptor::GetPackagesByService(const QString &alias)
{
    // サービス操作と同じ検証を共有する
    std::string detail;
    if (!RepoPolicy::validateAlias(alias.toStdString(), detail)) {
        rejectListArguments(QStringLiteral("Invalid service alias"));
        return {};
    }
    return runRead([alias] {
        return QVariant::fromValue(PackageListReply{toPackageRows(serviceResults(alias.toStdString()))});
    }).value<PackageListReply>();
}

/**
 * @brief 指定した名前のパッケージを構造化した一覧で取得する
 * @param names パッケージ名の一覧 (入力順、重複は最初の出現のみ)
 * @return パッケージ一覧の構造体
 * @note 5000件超・空文字・512バイトを超える名前はInvalidArgsを返す
 */
PackageListReply PackageManagerAdaptor::GetPackagesByNames(const QStringList &names)
{
    if (!validPackageNames(names)) {
        rejectListArguments(QStringLiteral("Invalid package names"));
        return {};
    }
    return runRead([names] {
        std::vector<std::string> stdNames;
        stdNames.reserve(static_cast<std::size_t>(names.size()));
        for (const QString &name : names)
            stdNames.push_back(name.toStdString());
        return QVariant::fromValue(
            PackageListReply{toPackageRows(ZyppManager::instance().getPackagesByNames(stdNames))});
    }).value<PackageListReply>();
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
    return runRead([name] { return maps(patternResults(name.toStdString())); }).toList();
}

/**
 * @brief パターン別パッケージを構造化した一覧で取得する
 * @param name パターン名
 * @return リポジトリ表とパッケージ行の一覧応答
 */
PackageListReply PackageManagerAdaptor::GetPackagesByPatternV2(const QString &name)
{
    return runRead([name] {
        return QVariant::fromValue(PackageListReply{toPackageRows(patternResults(name.toStdString()))});
    }).value<PackageListReply>();
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
