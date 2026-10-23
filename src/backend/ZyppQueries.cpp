#include <algorithm>
#include <QDateTime>
#include <zypp/sat/Pool.h>
#include <zypp/PoolQuery.h>
#include <zypp/ResObject.h>
#include "ZyppManager.h"

namespace qZypper {

namespace {
/**
 * @brief libzyppの日時をISO-8601のローカル時刻文字列へ変換する
 * @param date 変換する日時
 * @return ISO-8601文字列 (未設定やepoch 0の場合は空)
 */
QString isoDate(const zypp::Date& date)
{
    const auto seconds = static_cast<qint64>(date);

    // 未設定やepoch 0は1970年ではなく空文字として扱う
    if (seconds <= 0) {
        return QString();
    }
    return QDateTime::fromSecsSinceEpoch(seconds).toString(Qt::ISODate);
}
} // namespace

/**
 * @brief パッケージの詳細情報を取得する
 *
 * 依存関係、ファイル一覧、利用可能バージョン等を含む
 *
 * @param name パッケージ名
 * @return パッケージ詳細情報
 */
PackageDetails ZyppManager::getPackageDetails(const std::string& name) const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    PackageDetails details;

    if (!m_initialized)
        return details;

    auto pool = m_zypp->poolProxy();
    for (auto it = pool.byKindBegin<zypp::Package>();
         it != pool.byKindEnd<zypp::Package>(); ++it) {

        const auto& sel = *it;
        if (sel->name() != name)
            continue;

        details.basic = makePackageInfo(sel);

        // 候補またはインストール済みオブジェクトからの詳細取得
        auto obj = sel->candidateObj() ? sel->candidateObj() : sel->installedObj();
        if (!obj)
            break;

        auto pkg = zypp::asKind<zypp::Package>(obj);
        if (!pkg)
            break;

        details.description = QString::fromStdString(pkg->description());
        details.license     = QString::fromStdString(pkg->license());
        details.group       = QString::fromStdString(pkg->group());
        details.vendor      = QString::fromStdString(pkg->vendor());
        details.buildHost   = QString::fromStdString(pkg->buildhost());
        details.buildTime   = isoDate(pkg->buildtime());
        details.sourceRpm   = QString::fromStdString(pkg->sourcePkgName() + "-" + pkg->sourcePkgEdition().asString());
        details.url         = QString::fromStdString(pkg->url());

        // 技術情報は表示対象 (候補、候補がなければインストール済み) の値を使う
        details.installSize  = static_cast<qint64>(obj->installSize());
        details.downloadSize = static_cast<qint64>(obj->downloadSize());

        // インストール日時は、候補と同一のインストール済み版がある場合のみ設定する
        // (無関係な旧版の日時を候補へ紐付けない)
        const zypp::PoolItem installedObj =
            sel->candidateObj() ? sel->identicalInstalledObj(obj) : sel->installedObj();

        if (installedObj) {
            details.installTime = isoDate(installedObj.installtime());
        }

        // 複数版同時インストール可否 (Selectable::multiversionInstallは
        // 「利用可能な版の少なくとも1つがmultiversion」を意味する)
        details.multiversion = sel->multiversionInstall();

        bool sawMultiversion = false;
        bool sawPlain = false;

        // 依存関係
        for (const auto& cap : pkg->dep(zypp::Dep::PROVIDES))
            details.provides << QString::fromStdString(cap.asString());
        for (const auto& cap : pkg->dep(zypp::Dep::REQUIRES))
            details.requirements << QString::fromStdString(cap.asString());
        for (const auto& cap : pkg->dep(zypp::Dep::CONFLICTS))
            details.conflicts << QString::fromStdString(cap.asString());
        for (const auto& cap : pkg->dep(zypp::Dep::OBSOLETES))
            details.obsoletes << QString::fromStdString(cap.asString());
        for (const auto& cap : pkg->dep(zypp::Dep::RECOMMENDS))
            details.recommends << QString::fromStdString(cap.asString());
        for (const auto& cap : pkg->dep(zypp::Dep::SUGGESTS))
            details.suggests << QString::fromStdString(cap.asString());

        // ファイル一覧 (インストール済みパッケージのみ)
        if (auto instPkg = zypp::asKind<zypp::Package>(sel->installedObj())) {
            for (const auto& file : instPkg->filelist())
                details.fileList << QString::fromStdString(file);
        }

        // 利用可能バージョン (インストール済みで候補と同一でない版と、利用可能な版の両方)
        for (auto avail = sel->picklistBegin(); avail != sel->picklistEnd(); ++avail) {
            QVariantMap ver;
            // インストール済みの全版 (kernel等の複数版同時インストール対応) のいずれかと同一か
            // identicalInstalled()はedition・archに加えてvendor・buildtimeも比較するため、
            // 同一版の別アーキテクチャや別ビルドをインストール済みと誤判定しない
            const bool installed = avail->status().isInstalled() || sel->identicalInstalled(*avail);
            const bool itemMultiversion = avail->multiversionInstall();
            ver["installed"]    = installed;
            ver["version"]      = QString::fromStdString(avail->edition().asString());
            ver["arch"]         = QString::fromStdString(avail->arch().asString());
            ver["repoAlias"]    = QString::fromStdString(avail->repoInfo().alias());
            ver["repoName"]     = QString::fromStdString(avail->repoInfo().name());
            ver["status"]       = fromZyppStatus(sel->pickStatus(*avail));
            ver["multiversion"] = itemMultiversion;
            ver["size"]         = QVariant::fromValue(static_cast<qint64>(avail->installSize()));
            ver["downloadSize"] = QVariant::fromValue(static_cast<qint64>(avail->downloadSize()));
            ver["buildTime"]    = isoDate(avail->buildtime());
            ver["installTime"]  = installed ? isoDate(avail->installtime()) : QString();
            sawMultiversion = sawMultiversion || itemMultiversion;
            sawPlain = sawPlain || !itemMultiversion;
            details.availableVersions.append(ver);
        }
        details.mixedMultiversion = details.multiversion && sawPlain;

        break;
    }

    return details;
}

/**
 * @brief 指定リポジトリに属するパッケージ一覧を取得する
 *
 * 利用可能なバージョンのいずれかが指定リポジトリにあるパッケージを返す (YaSTと同じ)
 * 候補バージョンのリポジトリでは判定しないため、ユーザーが別リポジトリのバージョンを選択しても一覧から外れない
 *
 * @param repoAlias リポジトリエイリアス
 * @return パッケージ情報のベクタ
 */
std::vector<PackageInfo> ZyppManager::getPackagesByRepo(const std::string& repoAlias) const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    std::vector<PackageInfo> result;

    if (!m_initialized)
        return result;

    auto pool = m_zypp->poolProxy();
    for (auto it = pool.byKindBegin<zypp::Package>();
         it != pool.byKindEnd<zypp::Package>(); ++it) {

        const auto& sel = *it;
        for (auto avail = sel->availableBegin(); avail != sel->availableEnd(); ++avail) {
            if (avail->repoInfo().alias() == repoAlias) {
                result.push_back(makePackageInfo(sel));
                break;
            }
        }
    }

    return result;
}

// --- パターン・パッチ ---

/**
 * @brief ユーザーに表示されるパターンの一覧を取得する
 *
 * 非表示のパターンは除外し、order順にソートして返す
 *
 * @return PatternInfoのベクタ
 */
std::vector<PatternInfo> ZyppManager::getPatterns() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    std::vector<PatternInfo> result;

    if (!m_initialized)
        return result;

    auto pool = m_zypp->poolProxy();
    for (auto it = pool.byKindBegin<zypp::Pattern>();
         it != pool.byKindEnd<zypp::Pattern>(); ++it) {

        const auto& sel = *it;
        auto obj = sel->candidateObj() ? sel->candidateObj() : sel->installedObj();
        if (!obj)
            continue;

        auto pat = zypp::asKind<zypp::Pattern>(obj);
        if (!pat)
            continue;

        // 非表示パターンを除外 (YaSTと同様)
        if (!pat->userVisible())
            continue;

        PatternInfo info;
        info.name        = QString::fromStdString(sel->name());
        info.summary     = QString::fromStdString(pat->summary());
        info.description = QString::fromStdString(pat->description());
        info.category    = QString::fromStdString(pat->category());
        info.icon        = QString::fromStdString(pat->icon().asString());
        info.status      = fromZyppStatus(sel->status());

        try {
            info.order = std::stoi(pat->order());
        }
        catch (...) {
            info.order = 0;
        }

        result.push_back(std::move(info));
    }

    // パターンのorder順にソートする
    std::sort(result.begin(), result.end(), [](const PatternInfo& a, const PatternInfo& b) { return a.order < b.order; });

    return result;
}

/**
 * @brief 製品一覧を取得する (読み取り専用)
 *
 * インストール済み製品はインストール済みオブジェクトの情報を表示し
 * 候補の情報でインストール済みの内容を隠さない
 * 候補の有無はavailable、インストール済みバージョンはinstalledVersionで別途示す
 *
 * @return プール内の製品情報を名前順に並べたベクタ (初期化前は空)
 * @note プールへの読み取りアクセスのみで、選択状態は変更しない
 */
std::vector<ProductInfo> ZyppManager::getProducts() const
{
    // プール参照と選択状態の読み取りが他のスレッドの反映処理と競合しないようロックする
    std::lock_guard<std::mutex> lock(m_mutex);
    std::vector<ProductInfo> result;

    // 初期化前はプールを参照できないため空の一覧を返す
    if (!m_initialized) {
        return result;
    }

    // ZyppManager::m_zyppからプールプロキシを取得する
    auto pool = m_zypp->poolProxy();

    // libzyppのプールから製品種別のオブジェクトを順に走査する
    for (auto it = pool.byKindBegin<zypp::Product>();
         it != pool.byKindEnd<zypp::Product>(); ++it) {
        const auto& sel = *it;

        // libzyppの選択状態からインストール済みオブジェクトと候補オブジェクトを取得する
        auto installedObj = sel->installedObj();
        auto candidateObj = sel->candidateObj();

        // インストール済み製品はインストール済みオブジェクトを優先する
        auto obj = installedObj ? installedObj : candidateObj;

        // インストール済みも候補もない製品は表示対象外とする
        if (!obj) {
            continue;
        }

        // libzyppのオブジェクトを製品として解釈できるか確認する
        auto product = zypp::asKind<zypp::Product>(obj);

        // 製品以外のオブジェクトは表示対象外とする
        if (!product) {
            continue;
        }

        ProductInfo info;
        info.name      = QString::fromStdString(sel->name());
        info.shortName = QString::fromStdString(product->shortName());
        info.summary   = QString::fromStdString(product->summary());
        info.version   = QString::fromStdString(obj.edition().asString());
        info.vendor    = QString::fromStdString(product->vendor());
        info.arch      = QString::fromStdString(obj.arch().asString());
        info.repoAlias = QString::fromStdString(obj.repoInfo().alias());
        info.repoName  = QString::fromStdString(obj.repoInfo().name());

        // ZyppManager::fromZyppStatusでlibzyppの状態を共通のパッケージ状態へ変換する
        info.status    = fromZyppStatus(sel->status());
        info.installed = installedObj != nullptr;
        info.available = candidateObj != nullptr;

        // インストール済みの場合はインストール済みバージョンとベース製品かどうかを別途設定する
        if (installedObj) {
            info.installedVersion = QString::fromStdString(installedObj.edition().asString());

            // libzyppのisTargetDistributionでターゲットディストリビューションを判定する
            auto installedProduct = zypp::asKind<zypp::Product>(installedObj);
            info.isBase = installedProduct && installedProduct->isTargetDistribution();
        }

        result.push_back(std::move(info));
    }

    // 表示順を安定させるため製品名の昇順に並べ替える
    std::sort(result.begin(), result.end(),
              [](const ProductInfo& a, const ProductInfo& b) {
                  return a.name < b.name;
              });

    return result;
}

/**
 * @brief 指定パターンに属するパッケージ一覧を取得する
 * @param patternName パターン名
 * @return パッケージ情報のベクタ
 */
std::vector<PackageInfo> ZyppManager::getPackagesByPattern(const std::string& patternName) const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    std::vector<PackageInfo> result;

    if (!m_initialized)
        return result;

    // プールからパターン種別のオブジェクトを順に走査する
    auto pool = m_zypp->poolProxy();
    for (auto it = pool.byKindBegin<zypp::Pattern>();
         it != pool.byKindEnd<zypp::Pattern>(); ++it) {

        const auto& sel = *it;
        if (sel->name() != patternName)
            continue;

        auto obj = sel->candidateObj() ? sel->candidateObj() : sel->installedObj();
        if (!obj)
            break;

        auto pat = zypp::asKind<zypp::Pattern>(obj);
        if (!pat)
            break;

        // パターンの内容からパッケージ一覧を取得する (YaSTのYQPkgPatternList::filter()と同様)
        zypp::Pattern::Contents contents(pat->contents());
        for (auto cit = contents.selectableBegin();
             cit != contents.selectableEnd(); ++cit) {

            auto pkgSel = *cit;
            if (!pkgSel)
                continue;

            // パターンにはパッチなども含まれるため、パッケージ種別だけを残す
            auto pkgObj = pkgSel->candidateObj() ? pkgSel->candidateObj() : pkgSel->installedObj();
            if (!pkgObj)
                continue;

            if (pkgObj->kind() == zypp::ResKind::package)
                result.push_back(makePackageInfo(pkgSel));
        }
        break;
    }

    return result;
}

/**
 * @brief パッチ一覧を取得する
 * @param category パッチカテゴリフィルタ (PatchCategoryの値、-1で全件)
 * @return PatchInfoのベクタ
 */
std::vector<PatchInfo> ZyppManager::getPatches(int category) const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    std::vector<PatchInfo> result;

    if (!m_initialized)
        return result;

    auto pool = m_zypp->poolProxy();
    for (auto it = pool.byKindBegin<zypp::Patch>();
         it != pool.byKindEnd<zypp::Patch>(); ++it) {

        const auto& sel = *it;
        auto obj = sel->candidateObj() ? sel->candidateObj() : sel->installedObj();
        if (!obj)
            continue;

        auto patch = zypp::asKind<zypp::Patch>(obj);
        if (!patch)
            continue;

        // カテゴリ指定がある場合は、libzyppのカテゴリ文字列と照合する
        if (category >= 0 && category != static_cast<int>(PatchCategory::All)) {
            std::string cat = patch->category();
            bool match = false;

            switch (static_cast<PatchCategory>(category)) {
                case PatchCategory::Security:    match = (cat == "security"); break;
                case PatchCategory::Recommended: match = (cat == "recommended"); break;
                case PatchCategory::Optional:    match = (cat == "optional"); break;
                case PatchCategory::Feature:     match = (cat == "feature"); break;
                case PatchCategory::Document:    match = (cat == "document"); break;
                case PatchCategory::YaST:        match = (cat == "yast"); break;
                default: match = true; break;
            }

            if (!match)
                continue;
        }

        PatchInfo info;
        info.name        = QString::fromStdString(sel->name());
        info.summary     = QString::fromStdString(patch->summary());
        info.category    = QString::fromStdString(patch->category());
        info.severity    = QString::fromStdString(patch->severity());
        info.version     = QString::fromStdString(patch->edition().asString());
        info.status      = fromZyppStatus(sel->status());
        info.interactive = patch->interactive();

        const std::string pArch = obj.arch().asString();
        const std::string pRepo = obj.repoInfo().alias();
        info.id               = QString::fromStdString(makePatchId(sel->name(), patch->edition().asString(), pArch, pRepo));
        info.isNeeded         = obj.isNeeded();
        info.isRelevant       = obj.isRelevant();
        info.isSatisfied      = obj.isSatisfied();
        info.rebootSuggested  = patch->rebootSuggested();
        info.restartSuggested = patch->restartSuggested();
        info.selected         = sel->status() == zypp::ui::S_Install || sel->status() == zypp::ui::S_AutoInstall;

        result.push_back(std::move(info));
    }

    return result;
}

// -- 変更予定パッケージ一覧 --

/**
 * @brief 変更予定のパッケージ一覧を取得する
 *
 * Install, AutoInstall, Update, AutoUpdate, Del, AutoDelのステータスを持つパッケージを返す
 *
 * @return 変更予定パッケージ情報のベクタ
 */
std::vector<PackageInfo> ZyppManager::getPendingChanges() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    std::vector<PackageInfo> result;

    if (!m_initialized)
        return result;

    auto pool = m_zypp->poolProxy();
    for (auto it = pool.byKindBegin<zypp::Package>();
         it != pool.byKindEnd<zypp::Package>(); ++it) {

        const auto& sel = *it;
        auto st = sel->status();

        // 複数版同時インストール可能なパッケージは、実際にトランザクション対象となる
        // プールアイテムごとに1件返す (同一Selectableでも旧版削除と新版導入が併存し得る)
        if (sel->multiversionInstall()) {
            // picklistは同一の利用可能版を持つインストール済みアイテムを除外してしまうため使わない
            // 同一版の削除を取りこぼさないよう、インストール済みと利用可能の全アイテムを走査する
            std::vector<zypp::PoolItem> items;
            items.insert(items.end(), sel->installedBegin(), sel->installedEnd());
            items.insert(items.end(), sel->availableBegin(), sel->availableEnd());

            for (auto pick = items.begin(); pick != items.end(); ++pick) {
                // 実際に導入・削除されるアイテムだけを保留一覧へ含める
                if (!pick->status().transacts()) {
                    continue;
                }

                PackageInfo info;
                info.name      = QString::fromStdString(sel->name());
                info.version   = QString::fromStdString(pick->edition().asString());
                info.arch      = QString::fromStdString(pick->arch().asString());
                info.repoAlias = QString::fromStdString(pick->repoInfo().alias());
                info.repoName  = QString::fromStdString(pick->repoInfo().name());
                info.size      = pick->installSize();
                info.status    = static_cast<int>(pick->status().isInstalled()
                                                      ? PackageStatus::Del
                                                      : PackageStatus::Install);
                // 削除行は概要表示がinstalledVersionを参照するため、対象版をそこにも入れる
                if (pick->status().isInstalled()) {
                    info.installedVersion = info.version;
                }
                result.push_back(std::move(info));
            }
            continue;
        }

        // Install, AutoInstall, Update, AutoUpdate, Del, AutoDelのみ
        if (st == zypp::ui::S_Install   || st == zypp::ui::S_AutoInstall ||
            st == zypp::ui::S_Update    || st == zypp::ui::S_AutoUpdate  ||
            st == zypp::ui::S_Del       || st == zypp::ui::S_AutoDel) {
            result.push_back(makePackageInfo(sel));
        }
    }

    return result;
}

} // namespace qZypper
