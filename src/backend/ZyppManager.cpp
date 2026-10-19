#include <QLoggingCategory>
#include <zypp/base/Logger.h>
#include <zypp/ZConfig.h>
#include <zypp/target/rpm/RpmDb.h>
#include <zypp/sat/Pool.h>
#include <zypp/DiskUsageCounter.h>
#include "ZyppManager.h"
#include "RepoPolicy.h"
#include "ZyppLogging.h"

namespace qZypper {

// ログカテゴリ (環境変数QT_LOGGING_RULES="qzypper.zypp.debug=true"で有効化)
Q_LOGGING_CATEGORY(lcZypp, "qzypper.zypp")

/**
 * @brief シングルトンインスタンスを取得する
 *
 * 常駐ワーカーの終了時に、コールバック受信器も同じスレッドで破棄する
 *
 * @return ZyppManagerの参照
 */
ZyppManager& ZyppManager::instance()
{
    static thread_local ZyppManager manager;
    return manager;
}

/**
 * @brief libzyppリソースを明示的に解放する
 */
void ZyppManager::shutdown()
{
    std::lock_guard<std::mutex> lock(m_mutex);

    cleanupLocalRpmsLocked();
    resetSolverOverridesLocked();
    m_keyRingReceiver.disconnect();
    m_digestReceiver.disconnect();
    m_keyRingReceiver.clearApprovals();
    m_repoManager.reset();

    if (m_zypp) {
        m_zypp->resolver()->reset();
        m_zypp->finishTarget();
        zypp::sat::Pool::instance().reposEraseAll();
    }

    m_zypp.reset();
    m_initialized = false;
    m_rpmStamp.clear();
}

/**
 * @brief libzyppを初期化する
 *
 * ZYppシングルトン取得、ターゲット初期化、リポジトリキャッシュ読込、インストール済みパッケージの読込、ソルバーデフォルト設定を行う
 *
 * @param root ルートパス (デフォルト : "/")
 * @return 初期化成功時はtrue
 */
bool ZyppManager::initialize(const std::string& root, bool cacheOnly)
{
    std::lock_guard<std::mutex> lock(m_mutex);

    // 前回の操作のエラーを持ち越さない (初期化済みで即時に返す場合も同様)
    // リポジトリ単位の失敗は致命的ではない (オフライン等) ため初期化自体は成功させ、警告としてm_lastErrorに集約する
    m_lastError.clear();
    if (m_initialized)
        return true;

    std::vector<std::string> repoWarnings;

    try {
        m_zypp = zypp::getZYpp();

        // GPG鍵信頼・ダイジェスト検証コールバックを先行登録 (fail-closed)
        // 以降のbuildCache/loadFromCacheより前に有効化する必要がある
        m_keyRingReceiver.connect();
        m_digestReceiver.connect();

        // ターゲット (RPMデータベース) の初期化
        m_zypp->initializeTarget(zypp::Pathname(root));

        // リポジトリマネージャの初期化
        // probeを無効化し、addRepository (refreshService内部経由を含む) がサービス提供リポジトリのメディアを開かないようにする
        zypp::RepoManagerOptions opts{zypp::Pathname(root)};
        opts.probe = false;
        m_repoManager = std::make_unique<zypp::RepoManager>(opts);

        // 有効なリポジトリのキャッシュを読み込み
        for (const auto& repo : m_repoManager->knownRepositories()) {
            if (!repo.enabled())
                continue;

            // メディア/キャッシュに触れる前にポリシー検査
            // 不適合は読み込まない
            std::string policyErr;
            if (!RepoPolicy::checkRepoPolicy(repo, policyErr)) {
                repoWarnings.push_back("skipped (policy violation): " + policyErr);
                qCWarning(lcZypp) << "Skipping repo during initialize:" << QString::fromStdString(policyErr);
                continue;
            }

            if (!m_repoManager->isCached(repo)) {
                if (cacheOnly) continue;

                // raw metadataが無い場合buildCacheは内部でrefreshMetadataを行い
                // ミラー端点を開くため、端点検査を先に行う
                if (!RepoPolicy::checkRepoOrigins(repo, policyErr)) {
                    repoWarnings.push_back("skipped (policy violation): " + policyErr);
                    qCWarning(lcZypp) << "Skipping repo during initialize:" << QString::fromStdString(policyErr);
                    continue;
                }

                try {
                    m_repoManager->buildCache(repo);
                }
                catch (const zypp::Exception& e) {
                    // キャッシュ構築失敗は警告として続行
                    repoWarnings.push_back("cache build failed for " + repo.alias() + ": " + e.msg());
                    continue;
                }
            }

            try {
                m_repoManager->loadFromCache(repo);
            }
            catch (const zypp::Exception& e) {
                repoWarnings.push_back("failed to load cache for " + repo.alias() + ": " + e.msg());
                continue;
            }
        }

        // ターゲットのリゾルバブルを読み込み (インストール済みパッケージ)
        m_zypp->target()->load();

        // zypp.confからソルバーデフォルトフラグを適用 (YaSTと同等)
        const zypp::Resolver_Ptr resolver = m_zypp->resolver();
        resolver->setDefaultSolverFlags();
        applySolverOverridesLocked(resolver);

        // ディスク使用量計算用のマウントポイントを設定
        m_zypp->setPartitions(zypp::DiskUsageCounter::detectMountPoints());

        if (!repoWarnings.empty()) {
            std::string joined;
            for (const auto &w : repoWarnings)
                joined += (joined.empty() ? "" : "; ") + w;
            m_lastError = "Some repositories could not be loaded: " + joined;
            qCWarning(lcZypp) << "initialize:" << QString::fromStdString(m_lastError);
        }

        m_initialized = true;
        // 初期化直後の要求ロケールを差分表示の基準として保持する
        m_savedRequestedLocales = zypp::sat::Pool::instance().getRequestedLocales();
        m_distUpgradeMode = false;
        recordDatabaseStamp();
        return true;

    }
    catch (const zypp::ZYppFactoryException&) {
        throw;
    }
    catch (const zypp::Exception& e) {
        m_lastError = "Initialization failed: " + e.msg();
        return false;
    }
    catch (const std::exception& e) {
        // 半端な状態の解放は呼び出し側がshutdown()で行う
        // (m_mutexは非再帰のため、ロック中にここからshutdown()を呼ばない)
        m_lastError = std::string("Initialization failed: ") + e.what();
        return false;
    }
}

/**
 * @brief GPG鍵フィンガープリントを承認する (ワンショット)
 * @param fingerprint フィンガープリント (正規化前の生文字列も可)
 * @return 正規化成功時はtrue
 *         不正な場合はfalseを返し、m_lastErrorを設定する
 * @note m_mutexは取得しない (receiverが独自mutexを持つ)
 */
bool ZyppManager::approveKeyFingerprint(const std::string &fingerprint)
{
    // NOTE: m_mutexは取得しない
    // リフレッシュ/コミット実行中のワーカースレッドからコールバックが発火する可能性があるため
    const std::string fp = KeyRingReceiver::normalizeFingerprint(fingerprint);
    if (fp.empty()) {
        m_lastError = "Invalid key fingerprint";
        return false;
    }

    m_keyRingReceiver.approveFingerprint(fp);
    return true;
}

/**
 * @brief 未信頼鍵通知コールバックを設定する
 * @param cb 通知コールバック
 * @note m_mutexは取得しない
 *       initialize()前の呼び出しも可能
 */
void ZyppManager::setUntrustedKeyCallback(UntrustedKeyCallbackFn cb)
{
    // NOTE: m_mutexは取得しない (上記と同理由)
    // receiverが独自mutexで保護する
    m_keyRingReceiver.setUntrustedKeyCallback(std::move(cb));
}

// -- ディスク使用量 --

/**
 * @brief ディスク使用量を取得する
 *
 * zypp::DiskUsageCounterを使用してシステムのマウントポイントを検出し、現在のプール状態 (パッケージ選択変更) に基づくディスク使用量を計算する
 *
 * @return DiskUsageInfoのベクタ (読み取り専用パーティションは除外)
 */
std::vector<DiskUsageInfo> ZyppManager::getDiskUsage() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    std::vector<DiskUsageInfo> result;

    if (!m_initialized || !m_zypp)
        return result;

    try {
        auto diskUsage = m_zypp->diskUsage();

        for (const auto& mp : diskUsage) {
            if (mp.readonly)
                continue;

            DiskUsageInfo info;
            info.mountPoint   = QString::fromStdString(mp.dir);
            info.totalSize    = static_cast<quint64>(mp.total_size) * 1024;              // KiB → bytes
            info.usedSize     = static_cast<quint64>(mp.used_size)  * 1024;              // KiB → bytes
            info.packageUsage = static_cast<qint64>(mp.pkg_size - mp.used_size) * 1024;  // KiB → bytes
            result.push_back(info);
        }
    }
    catch (const zypp::Exception& e) {
        m_lastError = "Failed to get disk usage: " + e.msg();
    }

    return result;
}

// --- ヘルパー ---

/**
 * @brief SelectableからPackageInfoを生成するヘルパー
 *
 * 導入済みパッケージは、libzyppの更新候補 (updateCandidateObj) の有無をupdatableへ設定する
 *
 * @param sel libzypp Selectableポインタ
 * @return パッケージ情報
 */
PackageInfo ZyppManager::makePackageInfo(const zypp::ui::Selectable::Ptr& sel) const
{
    PackageInfo info;
    info.name   = QString::fromStdString(sel->name());
    info.status = fromZyppStatus(sel->status());

    if (auto cand = sel->candidateObj()) {
        info.version   = QString::fromStdString(cand.edition().asString());
        info.summary   = QString::fromStdString(cand.summary());
        info.arch      = QString::fromStdString(cand.arch().asString());
        info.size      = cand.installSize();
        info.repoAlias = QString::fromStdString(cand.repoInfo().alias());
        info.repoName  = QString::fromStdString(cand.repoInfo().name());
    }

    if (auto inst = sel->installedObj()) {
        info.installedVersion = QString::fromStdString(inst.edition().asString());

        if (info.summary.isEmpty())
            info.summary = QString::fromStdString(inst.summary());

        if (info.arch.isEmpty())
            info.arch = QString::fromStdString(inst.arch().asString());

        // 候補の版は優先度などで導入済みより古くなる場合があるため、版の大小は文字列ではなくlibzyppに判定させる
        // 一括操作の[update]と同じupdateCandidateObjを基準にし、GUIの[Update]操作の可否と一致させる
        info.updatable = static_cast<bool>(sel->updateCandidateObj());
    }

    return info;
}

/**
 * @brief zypp::ui::StatusをqZypper::PackageStatusに変換する
 * @param status zyppのステータス
 * @return qZypperのステータス値
 */
int ZyppManager::fromZyppStatus(zypp::ui::Status status)
{
    switch (status) {
        case zypp::ui::S_NoInst:        return static_cast<int>(PackageStatus::NoInst);
        case zypp::ui::S_Install:       return static_cast<int>(PackageStatus::Install);
        case zypp::ui::S_AutoInstall:   return static_cast<int>(PackageStatus::AutoInstall);
        case zypp::ui::S_KeepInstalled: return static_cast<int>(PackageStatus::KeepInstalled);
        case zypp::ui::S_Update:        return static_cast<int>(PackageStatus::Update);
        case zypp::ui::S_AutoUpdate:    return static_cast<int>(PackageStatus::AutoUpdate);
        case zypp::ui::S_Del:           return static_cast<int>(PackageStatus::Del);
        case zypp::ui::S_AutoDel:       return static_cast<int>(PackageStatus::AutoDel);
        case zypp::ui::S_Taboo:         return static_cast<int>(PackageStatus::Taboo);
        case zypp::ui::S_Protected:     return static_cast<int>(PackageStatus::Protected);
        default:                        return static_cast<int>(PackageStatus::NoInst);
    }
}

/**
 * @brief qZypper::PackageStatusをzypp::ui::Statusに変換する
 * @param status qZypperのステータス値
 * @param out 変換結果 (成功時のみ設定)
 * @return statusがPackageStatusの範囲内ならtrue
 */
bool ZyppManager::toZyppStatus(int status, zypp::ui::Status &out)
{
    switch (static_cast<PackageStatus>(status)) {
        case PackageStatus::NoInst:        out = zypp::ui::S_NoInst;        return true;
        case PackageStatus::Install:       out = zypp::ui::S_Install;       return true;
        case PackageStatus::AutoInstall:   out = zypp::ui::S_AutoInstall;   return true;
        case PackageStatus::KeepInstalled: out = zypp::ui::S_KeepInstalled; return true;
        case PackageStatus::Update:        out = zypp::ui::S_Update;        return true;
        case PackageStatus::AutoUpdate:    out = zypp::ui::S_AutoUpdate;    return true;
        case PackageStatus::Del:           out = zypp::ui::S_Del;           return true;
        case PackageStatus::AutoDel:       out = zypp::ui::S_AutoDel;       return true;
        case PackageStatus::Taboo:         out = zypp::ui::S_Taboo;         return true;
        case PackageStatus::Protected:     out = zypp::ui::S_Protected;     return true;
    }
    // 範囲外の値をS_NoInst (削除扱い) に倒さず拒否する
    return false;
}

/**
 * @brief PackageStatusとして有効な値か判定する
 * @param status 判定する値
 * @return 有効時はtrue
 */
bool ZyppManager::isValidPackageStatus(int status)
{
    zypp::ui::Status unused;
    return toZyppStatus(status, unused);
}

} // namespace qZypper
