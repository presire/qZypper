#include <algorithm>
#include <cctype>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <set>
#include <boost/logic/tribool.hpp>
#include <QLoggingCategory>
#include <zypp/base/Logger.h>
#include <zypp/ZConfig.h>
#include <zypp-core/fs/PathInfo.h>
#include <zypp/target/rpm/RpmDb.h>
#include <zypp/sat/Pool.h>
#include <zypp/sat/Transaction.h>
#include <zypp/PoolQuery.h>
#include <zypp/ZYppCommitResult.h>
#include <zypp/DiskUsageCounter.h>
#include <zypp-core/base/UserRequestException>
#include <zypp-core/base/String.h>
#include "ZyppManager.h"
#include "ZyppCallbackReceiver.h"


namespace qZypper {

// ログカテゴリ (環境変数 QT_LOGGING_RULES="qzypper.zypp.debug=true" で有効化)
Q_LOGGING_CATEGORY(lcZypp, "qzypper.zypp")

/**
 * @brief シングルトンインスタンスを取得する。
 *
 * 静的破棄順序の問題を回避するため、意図的にリークさせる。
 * @return ZyppManager の参照
 */
ZyppManager& ZyppManager::instance()
{
    // 意図的にリークさせ、静的破棄順序の問題を回避
    // リソースは shutdown() で明示的に解放する
    static ZyppManager* s_instance = new ZyppManager();
    return *s_instance;
}

/**
 * @brief libzypp リソースを明示的に解放する。
 */
void ZyppManager::shutdown()
{
    std::lock_guard<std::mutex> lock(m_mutex);

    m_problems.clear();
    m_repoManager.reset();
    m_zypp.reset();
    m_initialized = false;
}

/**
 * @brief libzyppを初期化する。
 *
 * ZYpp シングルトン取得、ターゲット初期化、リポジトリキャッシュ読込、インストール済みパッケージの読込、ソルバーデフォルト設定を行う。
 * @param root ルートパス (デフォルト: "/")
 * @return 初期化成功時 true
 */
bool ZyppManager::initialize(const std::string& root)
{
    std::lock_guard<std::mutex> lock(m_mutex);

    if (m_initialized)
        return true;

    try {
        m_zypp = zypp::getZYpp();

        // GPG鍵信頼・ダイジェスト検証コールバックを先行登録 (fail-closed)。
        // 以降の buildCache/loadFromCache より前に有効化する必要がある。
        m_keyRingReceiver.connect();
        m_digestReceiver.connect();

        // ターゲット (RPMデータベース) の初期化
        m_zypp->initializeTarget(zypp::Pathname(root));

        // リポジトリマネージャの初期化。
        // probe を無効化し、addRepository (refreshService 内部経由を含む) が
        // サービス提供リポジトリのメディアを開かないようにする。
        zypp::RepoManagerOptions opts{zypp::Pathname(root)};
        opts.probe = false;
        m_repoManager = std::make_unique<zypp::RepoManager>(opts);

        // 有効なリポジトリのキャッシュを読み込み
        for (const auto& repo : m_repoManager->knownRepositories()) {
            if (!repo.enabled())
                continue;

            // メディア/キャッシュに触れる前にポリシー検査。不適合は読み込まない。
            std::string policyErr;
            if (!checkRepoPolicy(repo, policyErr)) {
                m_lastError = "Skipping repository with policy violation: " + policyErr;
                qCWarning(lcZypp) << "Skipping repo during initialize:"
                                  << QString::fromStdString(policyErr);
                continue;
            }

            if (!m_repoManager->isCached(repo)) {
                // raw metadata が無い場合 buildCache は内部で refreshMetadata を行い
                // ミラー端点を開くため、端点検査を先に行う
                if (!checkRepoOrigins(repo, policyErr)) {
                    m_lastError = "Skipping repository with policy violation: " + policyErr;
                    qCWarning(lcZypp) << "Skipping repo during initialize:"
                                      << QString::fromStdString(policyErr);
                    continue;
                }
                try {
                    m_repoManager->buildCache(repo);
                } catch (const zypp::Exception& e) {
                    // キャッシュ構築失敗は警告として続行
                    m_lastError = "Cache build failed for " + repo.alias() + ": " + e.msg();
                    continue;
                }
            }

            try {
                m_repoManager->loadFromCache(repo);
            } catch (const zypp::Exception& e) {
                m_lastError = "Failed to load cache for " + repo.alias() + ": " + e.msg();
                continue;
            }
        }

        // ターゲットのリゾルバブルを読み込み (インストール済みパッケージ)
        m_zypp->target()->load();

        // zypp.confからソルバーデフォルトフラグを適用 (YaSTと同等)
        m_zypp->resolver()->setDefaultSolverFlags();

        // ディスク使用量計算用のマウントポイントを設定
        m_zypp->setPartitions(zypp::DiskUsageCounter::detectMountPoints());

        m_initialized = true;
        return true;

    } catch (const zypp::Exception& e) {
        m_lastError = "Initialization failed: " + e.msg();
        return false;
    }
}

// -- 入力検証・鍵承認 --

/**
 * @brief リポジトリ/サービスURLを検証する。
 *
 * 空URL・制御文字を拒否し、zypp::Url で構文解析する。
 * スキームは許可リスト (http, https, ftp, tftp, file, dir, hd, iso,
 * cd, dvd, nfs, nfs4, smb, cifs) のみ許可する (特に plugin を拒否)。
 * iso スキームの url クエリパラメータは再帰的に検証する。
 * @param url 検証対象URL
 * @param err 失敗時のエラー詳細
 * @param allowIsoNesting iso ネスト検証を許可するか
 * @return 有効時 true
 */
bool ZyppManager::validateUrl(const std::string &url, std::string &err,
                              bool allowIsoNesting)
{
    if (url.empty()) {
        err = "empty URL";
        return false;
    }
    for (unsigned char c : url) {
        if (c < 0x20 || c == 0x7f) {
            err = "URL contains control characters";
            return false;
        }
    }

    zypp::Url parsed;
    try {
        parsed = zypp::Url(url);
    } catch (const zypp::Exception &e) {
        err = std::string("malformed URL: ") + e.msg();
        return false;
    } catch (const std::exception &e) {
        err = std::string("malformed URL: ") + e.what();
        return false;
    }
    if (!parsed.isValid()) {
        err = "malformed URL";
        return false;
    }

    const std::string scheme = zypp::str::toLower(parsed.getScheme());
    static const std::set<std::string> kAllowedSchemes = {
        "http", "https", "ftp", "tftp", "file", "dir", "hd", "iso",
        "cd", "dvd", "nfs", "nfs4", "smb", "cifs"
    };
    if (kAllowedSchemes.count(scheme) == 0) {
        err = "disallowed URL scheme: " + parsed.getScheme();
        return false;
    }

    if (scheme == "iso" && allowIsoNesting) {
        std::string nested;
        try {
            nested = parsed.getQueryParam("url");
        } catch (const zypp::Exception &e) {
            err = std::string("bad iso query parameter: ") + e.msg();
            return false;
        } catch (const std::exception &e) {
            err = std::string("bad iso query parameter: ") + e.what();
            return false;
        }
        if (!nested.empty() && !validateUrl(nested, err, false)) {
            err = "invalid nested iso url: " + err;
            return false;
        }
    }

    return true;
}

/**
 * @brief リポジトリ/サービスエイリアスを検証する。
 *
 * INI インジェクション (改行・セクション記号) とパストラバーサル
 * ('/', '\\', 先頭 '.') を防ぐ。
 * @param alias 検証対象エイリアス
 * @param err 失敗時のエラー詳細
 * @return 有効時 true
 */
bool ZyppManager::validateAlias(const std::string &alias, std::string &err)
{
    if (alias.empty()) {
        err = "empty alias";
        return false;
    }
    if (alias.size() > 200) {
        err = "alias too long (max 200 bytes)";
        return false;
    }
    if (alias[0] == '.') {
        err = "alias must not start with '.'";
        return false;
    }
    for (unsigned char c : alias) {
        if (c < 0x20 || c == 0x7f) {
            err = "alias contains control characters";
            return false;
        }
        if (c == '/' || c == '\\' || c == '[' || c == ']') {
            err = std::string("alias contains forbidden character '") + static_cast<char>(c) + "'";
            return false;
        }
    }
    return true;
}

/**
 * @brief リポジトリ/サービス表示名を検証する。
 *
 * 制御文字 (CR/LF を含む) を拒否する。空文字列は有効
 * (呼び出し側 API が空を許す箇所用)。
 * @param name 検証対象名
 * @param err 失敗時のエラー詳細
 * @return 有効時 true
 */
bool ZyppManager::validateName(const std::string &name, std::string &err)
{
    if (name.size() > 1024) {
        err = "name too long (max 1024 bytes)";
        return false;
    }
    for (unsigned char c : name) {
        if (c < 0x20 || c == 0x7f) {
            err = "name contains control characters";
            return false;
        }
    }
    return true;
}

/**
 * @brief リポジトリポリシーを検査する。
 *
 * サービスやミラーリスト由来のリポジトリデータが URL/INI/GPG 検証を
 * 迂回するのを防ぐ。制御文字・不正 URL・不審なミラーリスト/GPG 鍵 URL を拒否する。
 * エラー文にはエイリアス・理由・スキームのみを含め、完全な URL は含めない
 * (認証情報を含む可能性があるため)。
 * @param repo 検査対象リポジトリ
 * @param err 失敗時のエラー詳細
 * @return ポリシー準拠時 true
 */
bool ZyppManager::checkRepoPolicy(const zypp::RepoInfo &repo, std::string &err)
{
    const std::string alias = repo.alias();

    auto hasControlChar = [](const std::string &s) {
        for (unsigned char c : s) {
            if (c < 0x20 || c == 0x7f)
                return true;
        }
        return false;
    };

    // エイリアス・名前に制御文字があれば INI 書き出し時の注入とみなす
    if (hasControlChar(alias) || hasControlChar(repo.name())) {
        err = "repo '" + alias + "': alias or name contains control characters";
        return false;
    }

    // 実効ベース URL (変数置換済み) を全件検証
    for (const auto &u : repo.baseUrls()) {
        const std::string urlStr = u.asString();
        std::string urlErr;
        if (!validateUrl(urlStr, urlErr)) {
            err = "repo '" + alias + "': invalid base URL (" + urlErr + ")";
            return false;
        }
    }

    // ミラーリスト/メタリンクは信頼できる転送手段のみ許可する。
    // RepoMirrorList は返却ミラー端点のスキームを濾過しないため。
    const zypp::Url mirrorUrl = repo.mirrorListUrl();
    if (!mirrorUrl.asString().empty()) {
        std::string scheme;
        try {
            scheme = zypp::str::toLower(mirrorUrl.getScheme());
        } catch (...) {
            scheme.clear();
        }
        if (scheme != "https" && scheme != "file" && scheme != "dir") {
            err = "repo '" + alias + "': disallowed mirrorlist scheme: " + scheme;
            return false;
        }
    }

    // GPG 鍵 URL を全件検証 (スキームのみ記録し完全な URL は出さない)
    for (const auto &u : repo.gpgKeyUrls()) {
        const std::string urlStr = u.asString();
        std::string urlErr;
        if (!validateUrl(urlStr, urlErr)) {
            std::string scheme;
            try {
                scheme = zypp::Url(urlStr).getScheme();
            } catch (...) {
                scheme = "?";
            }
            err = "repo '" + alias + "': invalid GPG key URL (" + urlErr
                + ", scheme: " + scheme + ")";
            return false;
        }
    }

    // サービス由来リポジトリはリモート repoindex が GPG 設定・ミラーリストを
    // 注入できるため、生の三値設定と明示ミラーリストを検査する。
    // 非サービス (管理者設定) は現行動作を維持する。
    if (!repo.service().empty()) {
        if (zypp::ZConfig::instance().gpgCheck() && rawGpgChecksWeakened(repo)) {
            err = "repo '" + alias + "': signature checking weakened by remote repoindex";
            return false;
        }
        // サービスが明示ミラーリスト/メタリンクを注入してはならない
        if (!repo.rawCfgMirrorlistUrl().asString().empty()
            || !repo.rawCfgMetalinkUrl().asString().empty()) {
            err = "repo '" + alias + "': mirrorlist configured by remote service";
            return false;
        }
    }

    return true;
}

/**
 * @brief リポジトリの生 GPG 設定が弱体化されているか検査する。
 */
bool ZyppManager::rawGpgChecksWeakened(const zypp::RepoInfo &repo)
{
    zypp::TriBool g, r, p;
    try {
        repo.getRawGpgChecks(g, r, p);
    } catch (...) {
        // 生値が読めなければ安全側に倒す
        return true;
    }
    // 確定的に false のものが1つでもあれば弱体化
    // (gpgcheck=1 repo_gpgcheck=0 pkg_gpgcheck=0 等を検出)
    auto isDeterminatelyFalse = [](const zypp::TriBool &t) {
        if (boost::logic::indeterminate(t))
            return false;
        return !static_cast<bool>(t);
    };
    return isDeterminatelyFalse(g) || isDeterminatelyFalse(r) || isDeterminatelyFalse(p);
}

/**
 * @brief ミラーリスト由来の全エンドポイント URL を検証する。
 */
bool ZyppManager::checkRepoOrigins(const zypp::RepoInfo &repo, std::string &err)
{
    const std::string alias = repo.alias();
    try {
        // 同一 RepoInfo オブジェクト上で取得する。libzypp は取得済みミラー
        // リストを共有 pimpl に約1時間キャッシュするため、検査対象と
        // libzypp 使用分がその範囲で一致する。
        const zypp::MirroredOriginSet origins = repo.repoOrigins();
        for (const auto &origin : origins) {
            for (const auto &endpoint : origin) {
                std::string urlStr;
                try {
                    urlStr = endpoint.url().asString();
                } catch (const std::exception &e) {
                    err = "repo '" + alias + "': unreadable mirror endpoint: " + e.what();
                    return false;
                }
                std::string urlErr;
                if (!validateUrl(urlStr, urlErr)) {
                    std::string scheme;
                    try {
                        scheme = endpoint.url().getScheme();
                    } catch (...) {
                        scheme = "?";
                    }
                    err = "repo '" + alias + "': invalid mirror endpoint ("
                        + urlErr + ", scheme: " + scheme + ")";
                    qCWarning(lcZypp) << "Mirror endpoint violates policy for repo"
                                      << QString::fromStdString(alias)
                                      << ": scheme=" << QString::fromStdString(scheme);
                    return false;
                }
            }
        }
    } catch (const zypp::Exception &e) {
        err = "repo '" + alias + "': cannot verify mirror endpoints: " + e.msg();
        return false;
    } catch (const std::exception &e) {
        err = "repo '" + alias + "': cannot verify mirror endpoints: " + e.what();
        return false;
    }
    return true;
}

/**
 * @brief サービス情報を検査する。
 */
bool ZyppManager::checkServicePolicy(const zypp::ServiceInfo &svc, std::string &err)
{
    const std::string alias = svc.alias();

    std::string fieldErr;
    if (!validateAlias(alias, fieldErr)) {
        err = "service '" + alias + "': invalid alias: " + fieldErr;
        return false;
    }
    if (!validateName(svc.name(), fieldErr)) {
        err = "service '" + alias + "': invalid name: " + fieldErr;
        return false;
    }
    if (!validateUrl(svc.url().asString(), fieldErr)) {
        err = "service '" + alias + "': invalid URL: " + fieldErr;
        return false;
    }

    // qZypper はプラグインサービスを更新しない。zypper の利用を促す。
    if (svc.type() == zypp::repo::ServiceType::PLUGIN) {
        err = "service '" + alias + "': plugin services are not supported, use zypper";
        return false;
    }

    // repoStates キーは .service ファイルに素通しされるため INI 注入対策
    for (const auto &kv : svc.repoStates()) {
        if (!validateAlias(kv.first, fieldErr)) {
            err = "service '" + alias + "': invalid repository state entry";
            return false;
        }
    }

    return true;
}

/**
 * @brief on-disk の .service ファイルがメモリ上のサービスと一致するか検査する。
 */
bool ZyppManager::serviceFileMatches(const zypp::Pathname &path, const zypp::ServiceInfo &svc)
{
    // zypp::parser::ServiceFileReader は libzypp 17.38.16 で export されて
    // いないため、単一セクション・alias/type/url 一致の最小 INI 照合を行う。
    if (path.empty())
        return false;
    std::ifstream in(path.asString());
    if (!in)
        return false;

    auto trim = [](std::string s) {
        const char *ws = " \t\r";
        const auto b = s.find_first_not_of(ws);
        if (b == std::string::npos)
            return std::string();
        return s.substr(b, s.find_last_not_of(ws) - b + 1);
    };

    unsigned sections = 0;
    std::string sectionName;
    bool inTarget = false;
    bool urlSeen = false;
    bool ok = true;
    // libzypp はファイルへ rawUrl (変数未展開・資格情報込み) を書くため、
    // 双方を解析して完全形式で比較する (展開済み url() と比べると正規サービスを誤隔離する)
    const std::string wantUrl = svc.rawUrl().asCompleteString();
    const std::string wantType = svc.type().asString();

    std::string line;
    while (std::getline(in, line)) {
        line = trim(line);
        if (line.empty() || line[0] == '#' || line[0] == ';')
            continue;
        if (line.front() == '[') {
            if (line.back() != ']')
                return false;
            ++sections;
            sectionName = trim(line.substr(1, line.size() - 2));
            inTarget = (sectionName == svc.alias());
            continue;
        }
        if (!inTarget)
            continue;
        const auto eq = line.find('=');
        if (eq == std::string::npos)
            continue;
        std::string key = trim(line.substr(0, eq));
        for (char &c : key)
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        const std::string value = trim(line.substr(eq + 1));
        // 差し込まれた複数行も検出するため全出現を照合する
        if (key == "url") {
            urlSeen = true;
            try {
                if (zypp::Url(value).asCompleteString() != wantUrl)
                    ok = false;
            } catch (...) {
                ok = false;
            }
        } else if (key == "type") {
            if (value != wantType)
                ok = false;
        }
    }

    return sections == 1 && sectionName == svc.alias() && urlSeen && ok;
}

/**
 * @brief サービス由来のリポジトリを浄化する。
 *
 * 指定サービスの全リポジトリを検査し、ポリシー違反や署名検査弱体化が
 * あるものを削除する。残留 .repo ファイルも削除する
 * (サービス生成 .repo は単一リポジトリであり、残留内容は INI 注入の可能性があるため)。
 * @param serviceAlias 対象サービスエイリアス
 * @param report 削除内容の報告 (エイリアスと理由のみ、URL なし)
 * @return 違反がなく全て健全時 true
 * @note 呼び出し側の m_mutex 配下で呼ぶこと (非再帰 mutex のため内部でロックしない)。
 */
bool ZyppManager::sanitizeServiceRepos(const std::string &serviceAlias, std::string &report,
                                        const std::set<std::string> &preexistingRepoFiles)
{
    // NOTE: m_mutex は取得しない。呼び出し側 (addService/refreshService) が保持している。
    if (!m_repoManager)
        return false;

    const bool gpgRequired = zypp::ZConfig::instance().gpgCheck();
    const zypp::Pathname reposDir = zypp::ZConfig::instance().knownReposPath();
    bool clean = true;
    std::string removed;

    for (const auto &repo : m_repoManager->knownRepositories()) {
        if (repo.service() != serviceAlias)
            continue;

        const std::string alias = repo.alias();
        std::string reason;

        std::string policyErr;
        if (!checkRepoPolicy(repo, policyErr)) {
            reason = policyErr;
        } else if (gpgRequired && rawGpgChecksWeakened(repo)) {
            reason = "repo '" + alias + "': signature checking weakened by remote repoindex";
        }

        if (reason.empty())
            continue;

        // 違反リポジトリを削除する
        const zypp::Pathname fpath = repo.filepath();
        try {
            m_repoManager->removeRepository(repo);
        } catch (const zypp::Exception &e) {
            qCWarning(lcZypp) << "sanitizeServiceRepos: remove failed for"
                              << QString::fromStdString(alias) << ":"
                              << QString::fromStdString(e.msg());
        }
        // 残留 .repo は共有ファイル削除を避けるためガード付きでのみ消す:
        // knownReposPath 直下の通常ファイルで、サービス追加/更新前から存在せず、
        // 残存リポジトリのいずれにも所有されていない場合のみ unlink する。
        if (!fpath.empty() && zypp::PathInfo(fpath).isExist()) {
            bool owned = false;
            for (const auto &remaining : m_repoManager->knownRepositories()) {
                if (remaining.filepath() == fpath) {
                    owned = true;
                    break;
                }
            }
            if (!owned && fpath.dirname() == reposDir
                && zypp::PathInfo(fpath).isFile()
                && preexistingRepoFiles.count(fpath.asString()) == 0) {
                if (zypp::filesystem::unlink(fpath) != 0) {
                    qCWarning(lcZypp) << "sanitizeServiceRepos: unlink failed for"
                                      << QString::fromStdString(fpath.asString());
                }
            } else {
                qCWarning(lcZypp) << "sanitizeServiceRepos: keeping unexpected file"
                                  << QString::fromStdString(fpath.asString());
            }
        }

        if (!removed.empty())
            removed += "; ";
        removed += reason;
        clean = false;
    }

    if (!clean)
        report = removed;
    return clean;
}

/**
 * @brief 既知リポジトリパス直下の通常ファイルを列挙する。
 */
std::set<std::string> ZyppManager::snapshotRepoFiles()
{
    std::set<std::string> files;
    const zypp::Pathname dir = zypp::ZConfig::instance().knownReposPath();
    std::error_code ec;
    for (const auto &entry : std::filesystem::directory_iterator(dir.asString(), ec)) {
        if (ec)
            break;
        std::error_code ec2;
        if (entry.is_regular_file(ec2) && !ec2)
            files.insert(entry.path().string());
    }
    return files;
}

/**
 * @brief サービスの .service ファイル汚染を検証し必要なら隔離する。
 */
bool ZyppManager::quarantineServiceIfTainted(const std::string &serviceAlias)
{
    // NOTE: m_mutex は取得しない。呼び出し側が保持している。
    if (!m_repoManager)
        return true;

    zypp::ServiceInfo info;
    try {
        info = m_repoManager->getService(serviceAlias);
    } catch (...) {
        return true;  // 検証対象なし (既に削除済み等)
    }

    bool tainted = false;
    std::string detail;
    if (!checkServicePolicy(info, detail)) {
        qCWarning(lcZypp) << "quarantineServiceIfTainted: policy violation for service"
                          << QString::fromStdString(serviceAlias)
                          << ":" << QString::fromStdString(detail);
        tainted = true;
    } else {
        // on-disk の .service ファイルを解析し、単一セクションかつ
        // alias/type/url の一致を要求する (INI 注入検出)。
        try {
            if (!serviceFileMatches(info.filepath(), info))
                tainted = true;
        } catch (const std::exception &e) {
            qCWarning(lcZypp) << "quarantineServiceIfTainted: service file unreadable for"
                              << QString::fromStdString(serviceAlias) << ":" << e.what();
            tainted = true;
        } catch (...) {
            tainted = true;
        }
    }

    if (!tainted)
        return true;

    // 隔離: サービス (配下リポジトリと .service ファイル) を削除する
    zypp::Pathname fpath;
    try {
        fpath = info.filepath();
    } catch (...) {
    }
    bool removed = true;
    try {
        m_repoManager->removeService(serviceAlias);
    } catch (const zypp::Exception &e) {
        removed = false;
        qCWarning(lcZypp) << "quarantineServiceIfTainted: removeService failed for"
                          << QString::fromStdString(serviceAlias) << ":"
                          << QString::fromStdString(e.msg());
    }
    // removeService 後に残存し、かつ既知サービスパス直下の通常ファイルなら unlink
    const zypp::Pathname svcDir = zypp::ZConfig::instance().knownServicesPath();
    if (!fpath.empty() && fpath.dirname() == svcDir
        && zypp::PathInfo(fpath).isFile()) {
        if (zypp::filesystem::unlink(fpath) != 0) {
            qCWarning(lcZypp) << "quarantineServiceIfTainted: unlink failed for"
                              << QString::fromStdString(fpath.asString());
        }
    }
    // 削除が拒否された場合 (SELinux 等) は汚染ファイルが残るため、成功と報告しない
    if (!fpath.empty() && zypp::PathInfo(fpath).isExist())
        removed = false;

    m_lastError = removed
        ? "Service " + serviceAlias + " removed: index contained invalid data"
        : "Service " + serviceAlias + " contains invalid data but could not be removed;"
          " remove it manually with zypper";
    qCWarning(lcZypp) << QString::fromStdString(m_lastError);
    return false;
}

/**
 * @brief GPG鍵フィンガープリントを承認する (ワンショット)。
 * @param fingerprint フィンガープリント (正規化前の生文字列も可)
 * @return 正規化成功時 true。不正な場合は false を返し m_lastError を設定する。
 * @note m_mutex は取得しない (receiver が独自 mutex を持つ)。
 */
bool ZyppManager::approveKeyFingerprint(const std::string &fingerprint)
{
    // NOTE: m_mutex は取得しない。リフレッシュ/コミット実行中の
    // ワーカースレッドからコールバックが発火する可能性があるため。
    const std::string fp = KeyRingReceiver::normalizeFingerprint(fingerprint);
    if (fp.empty()) {
        m_lastError = "Invalid key fingerprint";
        return false;
    }
    m_keyRingReceiver.approveFingerprint(fp);
    return true;
}

/**
 * @brief 未信頼鍵通知コールバックを設定する。
 * @param cb 通知コールバック
 * @note m_mutex は取得しない。initialize() 前の呼び出しも可能。
 */
void ZyppManager::setUntrustedKeyCallback(UntrustedKeyCallbackFn cb)
{
    // NOTE: m_mutex は取得しない (上記と同理由)。receiver が独自 mutex で保護する。
    m_keyRingReceiver.setUntrustedKeyCallback(std::move(cb));
}

// -- リポジトリ管理 --

/**
 * @brief 既知リポジトリの一覧を取得する。
 * @return RepoInfoのベクタ
 */
std::vector<RepoInfo> ZyppManager::getRepos() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    std::vector<RepoInfo> result;

    if (!m_repoManager)
        return result;

    for (const auto& repo : m_repoManager->knownRepositories()) {
        RepoInfo info;
        info.alias        = QString::fromStdString(repo.alias());
        info.name         = QString::fromStdString(repo.name());
        info.enabled      = repo.enabled();
        info.autoRefresh  = repo.autorefresh();
        info.priority     = repo.priority();
        info.type         = QString::fromStdString(repo.type().asString());
        info.keepPackages = repo.keepPackages();
        info.service      = QString::fromStdString(repo.service());

        if (!repo.baseUrlsEmpty()) {
            info.url    = QString::fromStdString(repo.url().asString());
            info.rawUrl = QString::fromStdString(repo.rawUrl().asString());
        }

        result.push_back(std::move(info));
    }

    return result;
}

/**
 * @brief URL と名前を指定してリポジトリを追加する。
 * @param url リポジトリURL
 * @param name リポジトリ名 (エイリアスとしても使用)
 * @return 追加成功時 true
 */
bool ZyppManager::addRepo(const std::string& url, const std::string& name)
{
    // NOTE: ロックは委譲先 addRepo(const RepoInfo&) が取得する。
    // ここでロックすると非再帰 mutex の二重取得でデッドロックするため取得しない。
    RepoInfo info;
    info.url         = QString::fromStdString(url);
    info.name        = QString::fromStdString(name);
    info.alias       = QString::fromStdString(name);
    info.enabled     = true;
    info.autoRefresh = true;
    return addRepo(info);
}

/**
 * @brief RepoInfo を指定してリポジトリを追加する。
 *
 * リポジトリの追加、メタデータリフレッシュ、キャッシュ構築、キャッシュ読込を行う。
 * @param info リポジトリ情報
 * @return 追加成功時 true
 */
bool ZyppManager::addRepo(const RepoInfo& info)
{
    std::lock_guard<std::mutex> lock(m_mutex);

    if (!m_repoManager) {
        m_lastError = "Backend not initialized";
        return false;
    }

    const std::string alias = info.alias.isEmpty() ? info.name.toStdString()
                                                   : info.alias.toStdString();
    const std::string name = info.name.toStdString();
    const std::string url = info.url.toStdString();

    std::string validationErr;
    if (!validateAlias(alias, validationErr)) {
        m_lastError = "Invalid alias: " + validationErr;
        return false;
    }
    if (!validateName(name, validationErr)) {
        m_lastError = "Invalid repository name: " + validationErr;
        return false;
    }
    if (!validateUrl(url, validationErr)) {
        m_lastError = "Invalid repository URL: " + validationErr;
        return false;
    }

    bool added = false;
    zypp::RepoInfo repo;
    try {
        repo.addBaseUrl(zypp::Url(url));
        repo.setName(name);
        repo.setAlias(alias);
        repo.setEnabled(info.enabled);
        repo.setAutorefresh(info.autoRefresh);
        repo.setPriority(info.priority);
        repo.setKeepPackages(info.keepPackages);

        if (!info.type.isEmpty())
            repo.setType(zypp::repo::RepoType(info.type.toStdString()));

        // 追加前にポリシー検査 (メディアに触れる前の最終ゲート)
        std::string policyErr;
        if (!checkRepoPolicy(repo, policyErr)) {
            m_lastError = "Repository violates policy: " + policyErr;
            return false;
        }

        m_repoManager->addRepository(repo);
        added = true;
        // ミラー端点はリフレッシュ直前に検査する (offline の initialize とは別扱い)
        std::string originsErr;
        if (!checkRepoOrigins(repo, originsErr)) {
            m_lastError = "Repository violates policy: " + originsErr;
            try {
                m_repoManager->removeRepository(repo);
                added = false;
            } catch (...) {
            }
            return false;
        }
        m_repoManager->refreshMetadata(repo);
        m_repoManager->buildCache(repo);
        m_repoManager->loadFromCache(repo);

        return true;
    } catch (const zypp::Exception& e) {
        std::string failure = "Failed to add repository: " + e.msg();
        if (added) {
            // 部分的に追加されたリポジトリをロールバックする (best-effort)
            try {
                m_repoManager->removeRepository(repo);
            } catch (const zypp::Exception& rollbackErr) {
                failure += " (rollback failed: " + rollbackErr.msg() + ")";
            }
        }
        m_lastError = failure;
        return false;
    }
}

/**
 * @brief リポジトリを削除する。
 * @param alias 削除対象のリポジトリエイリアス
 * @return 削除成功時 true
 */
bool ZyppManager::removeRepo(const std::string& alias)
{
    std::lock_guard<std::mutex> lock(m_mutex);

    if (!m_repoManager) {
        m_lastError = "Backend not initialized";
        return false;
    }

    try {
        zypp::RepoInfo repo = m_repoManager->getRepositoryInfo(alias);
        m_repoManager->removeRepository(repo);
        return true;
    } catch (const zypp::Exception& e) {
        m_lastError = "Failed to remove repository: " + e.msg();
        return false;
    }
}

/**
 * @brief リポジトリの有効/無効を切り替える。
 * @param alias 対象のリポジトリエイリアス
 * @param enabled 有効にする場合 true
 * @return 変更成功時 true
 */
bool ZyppManager::setRepoEnabled(const std::string& alias, bool enabled)
{
    std::lock_guard<std::mutex> lock(m_mutex);

    if (!m_repoManager) {
        m_lastError = "Backend not initialized";
        return false;
    }

    try {
        zypp::RepoInfo repo = m_repoManager->getRepositoryInfo(alias);
        repo.setEnabled(enabled);
        m_repoManager->modifyRepository(alias, repo);
        return true;
    } catch (const zypp::Exception& e) {
        m_lastError = "Failed to modify repository: " + e.msg();
        return false;
    }
}

/**
 * @brief リポジトリのプロパティを変更する。
 * @param alias 対象のリポジトリエイリアス
 * @param newInfo 新しいリポジトリ情報
 * @return 変更成功時 true
 */
bool ZyppManager::modifyRepo(const std::string& alias, const RepoInfo& newInfo)
{
    std::lock_guard<std::mutex> lock(m_mutex);

    if (!m_repoManager) {
        m_lastError = "Backend not initialized";
        return false;
    }

    std::string validationErr;
    if (!newInfo.name.isEmpty() && !validateName(newInfo.name.toStdString(), validationErr)) {
        m_lastError = "Invalid repository name: " + validationErr;
        return false;
    }
    if (!newInfo.url.isEmpty() && !validateUrl(newInfo.url.toStdString(), validationErr)) {
        m_lastError = "Invalid repository URL: " + validationErr;
        return false;
    }

    try {
        zypp::RepoInfo repo = m_repoManager->getRepositoryInfo(alias);

        if (!newInfo.name.isEmpty())
            repo.setName(newInfo.name.toStdString());
        if (!newInfo.url.isEmpty())
            repo.setBaseUrl(zypp::Url(newInfo.url.toStdString()));

        repo.setEnabled(newInfo.enabled);
        repo.setAutorefresh(newInfo.autoRefresh);
        repo.setPriority(newInfo.priority);
        repo.setKeepPackages(newInfo.keepPackages);

        m_repoManager->modifyRepository(alias, repo);
        return true;
    } catch (const zypp::Exception& e) {
        m_lastError = "Failed to modify repository: " + e.msg();
        return false;
    }
}

/**
 * @brief 指定リポジトリをリフレッシュする。
 *
 * メタデータの強制リフレッシュ、キャッシュ構築、キャッシュ読込を行う。
 * キャンセル可能。
 * @param alias リフレッシュ対象のリポジトリエイリアス
 * @param progressCallback 進捗コールバック (エイリアス, パーセント)
 * @return リフレッシュ成功時 true
 */
bool ZyppManager::refreshRepo(const std::string& alias,
    std::function<void(const std::string&, int)> progressCallback)
{
    std::lock_guard<std::mutex> lock(m_mutex);

    if (!m_repoManager) {
        m_lastError = "Backend not initialized";
        return false;
    }

    m_cancelRequested = false;

    // libzypp操作中にキャンセル要求を検出するコールバック
    // false を返すと refreshMetadata/buildCache/loadFromCache が中断される
    auto cancelCheck = [this](const zypp::ProgressData &) -> bool {
        return !m_cancelRequested.load();
    };

    try {
        zypp::RepoInfo repo = m_repoManager->getRepositoryInfo(alias);

        // メディア/キャッシュに触れる前にポリシー検査
        std::string policyErr;
        if (!checkRepoPolicy(repo, policyErr)) {
            m_lastError = "Repository violates policy: " + policyErr;
            return false;
        }
        // ミラー端点検査はリフレッシュ直前に行う (取得リストと使用分を束ねる)
        std::string originsErr;
        if (!checkRepoOrigins(repo, originsErr)) {
            m_lastError = "Repository violates policy: " + originsErr;
            return false;
        }

        if (progressCallback)
            progressCallback(alias, 0);

        m_repoManager->refreshMetadata(repo, zypp::RepoManager::RefreshForced, cancelCheck);
        m_repoManager->buildCache(repo, zypp::RepoManager::BuildIfNeeded, cancelCheck);

        if (repo.enabled())
            m_repoManager->loadFromCache(repo, cancelCheck);

        if (progressCallback)
            progressCallback(alias, 100);

        return true;
    } catch (const zypp::AbortRequestException&) {
        m_lastError = "Operation cancelled";
        return false;
    } catch (const zypp::Exception& e) {
        m_lastError = "Failed to refresh repository: " + e.msg();
        return false;
    }
}

/**
 * @brief URL からリポジトリタイプを検出する。
 * @param url 検出対象のURL
 * @return リポジトリタイプ文字列 ("rpm-md", "yast2" 等)
 */
std::string ZyppManager::probeRepoType(const std::string& url)
{
    std::lock_guard<std::mutex> lock(m_mutex);

    if (!m_repoManager) {
        m_lastError = "Backend not initialized";
        return "";
    }

    try {
        zypp::repo::RepoType type = m_repoManager->probe(zypp::Url(url));
        return type.asString();
    } catch (const zypp::Exception& e) {
        m_lastError = "Failed to probe repository type: " + e.msg();
        return "";
    }
}

/**
 * @brief 全有効リポジトリをリフレッシュする。
 *
 * 各リポジトリに対してメタデータリフレッシュ、キャッシュ構築、キャッシュ読込を行う。
 * キャンセル可能。
 * @param progressCallback 進捗コールバック (エイリアス, パーセント)
 * @return リフレッシュ成功時 true
 */
bool ZyppManager::refreshRepos(std::function<void(const std::string&, int)> progressCallback)
{
    std::lock_guard<std::mutex> lock(m_mutex);

    if (!m_repoManager) {
        m_lastError = "Backend not initialized";
        return false;
    }

    m_cancelRequested = false;

    // libzypp操作中にキャンセル要求を検出するコールバック
    // false を返すと refreshMetadata/buildCache/loadFromCache が中断される
    auto cancelCheck = [this](const zypp::ProgressData &) -> bool {
        return !m_cancelRequested.load();
    };

    try {
        auto repos = m_repoManager->knownRepositories();
        int total = static_cast<int>(repos.size());
        int current = 0;
        bool skippedViolation = false;
        std::string skippedDetail;

        for (const auto& repo : repos) {
            if (m_cancelRequested) {
                m_lastError = "Operation cancelled";
                return false;
            }

            if (!repo.enabled()) {
                ++current;
                continue;
            }

            // ポリシー違反リポジトリは飛ばして他を続行する
            std::string policyErr;
            if (!checkRepoPolicy(repo, policyErr)) {
                skippedViolation = true;
                if (!skippedDetail.empty())
                    skippedDetail += "; ";
                skippedDetail += policyErr;
                qCWarning(lcZypp) << "Skipping repo during refreshRepos:"
                                  << QString::fromStdString(policyErr);
                ++current;
                continue;
            }
            // ミラー端点違反も同様に飛ばす (ネットワーク取得を伴うため直前検査)
            std::string originsErr;
            if (!checkRepoOrigins(repo, originsErr)) {
                skippedViolation = true;
                if (!skippedDetail.empty())
                    skippedDetail += "; ";
                skippedDetail += originsErr;
                ++current;
                continue;
            }

            if (progressCallback)
                progressCallback(repo.alias(), (current * 100) / total);

            m_repoManager->refreshMetadata(repo, zypp::RepoManager::RefreshForced, cancelCheck);
            m_repoManager->buildCache(repo, zypp::RepoManager::BuildIfNeeded, cancelCheck);
            m_repoManager->loadFromCache(repo, cancelCheck);
            ++current;
        }

        if (progressCallback)
            progressCallback("", 100);

        if (skippedViolation) {
            m_lastError = "Skipped repositories with policy violations: " + skippedDetail;
            return false;
        }

        return true;
    } catch (const zypp::AbortRequestException&) {
        m_lastError = "Operation cancelled";
        return false;
    } catch (const zypp::Exception& e) {
        m_lastError = "Failed to refresh repositories: " + e.msg();
        return false;
    }
}

// -- サービス管理 --

/**
 * @brief 既知サービスの一覧を取得する。
 * @return ServiceInfo のベクタ
 */
std::vector<ServiceInfo> ZyppManager::getServices() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    std::vector<ServiceInfo> result;

    if (!m_repoManager)
        return result;

    for (const auto& svc : m_repoManager->knownServices()) {
        ServiceInfo info;
        info.alias       = QString::fromStdString(svc.alias());
        info.name        = QString::fromStdString(svc.name());
        info.url         = QString::fromStdString(svc.url().asString());
        info.enabled     = svc.enabled();
        info.autoRefresh = svc.autorefresh();
        info.type        = QString::fromStdString(svc.type().asString());
        result.push_back(std::move(info));
    }

    return result;
}

/**
 * @brief サービスを追加する。
 * @param url サービスURL
 * @param alias サービスエイリアス
 * @return 追加成功時 true
 */
bool ZyppManager::addService(const std::string& url, const std::string& alias)
{
    std::lock_guard<std::mutex> lock(m_mutex);

    if (!m_repoManager) {
        m_lastError = "Backend not initialized";
        return false;
    }

    std::string validationErr;
    if (!validateAlias(alias, validationErr)) {
        m_lastError = "Invalid alias: " + validationErr;
        return false;
    }
    if (!validateUrl(url, validationErr)) {
        m_lastError = "Invalid service URL: " + validationErr;
        return false;
    }

    bool added = false;
    zypp::ServiceInfo svc;
    try {
        svc.setAlias(alias);
        svc.setUrl(zypp::Url(url));
        svc.setEnabled(true);
        svc.setAutorefresh(true);

        // unlink ガード用のスナップショットを libzypp 追加前に取得する
        const std::set<std::string> preexistingRepoFiles = snapshotRepoFiles();

        m_repoManager->addService(svc);
        added = true;
        try {
            m_repoManager->refreshService(svc);
        } catch (...) {
            // 部分追加後に例外が出ても浄化と隔離は実行する
            std::string report;
            sanitizeServiceRepos(alias, report, preexistingRepoFiles);
            quarantineServiceIfTainted(alias);
            throw;
        }
        std::string report;
        const bool reposClean = sanitizeServiceRepos(alias, report, preexistingRepoFiles);
        if (!quarantineServiceIfTainted(alias))
            return false;
        if (!reposClean) {
            m_lastError = "Service added with violating repositories removed: " + report;
            return false;
        }
        return true;
    } catch (const zypp::Exception& e) {
        std::string failure = "Failed to add service: " + e.msg();
        if (added) {
            // 部分的に追加されたサービスをロールバックする (best-effort)
            try {
                m_repoManager->removeService(svc);
            } catch (const zypp::Exception& rollbackErr) {
                failure += " (rollback failed: " + rollbackErr.msg() + ")";
            }
        }
        m_lastError = failure;
        return false;
    }
}

/**
 * @brief サービスを削除する。
 * @param alias 削除対象のサービスエイリアス
 * @return 削除成功時 true
 */
bool ZyppManager::removeService(const std::string& alias)
{
    std::lock_guard<std::mutex> lock(m_mutex);

    if (!m_repoManager) {
        m_lastError = "Backend not initialized";
        return false;
    }

    try {
        zypp::ServiceInfo svc = m_repoManager->getService(alias);
        m_repoManager->removeService(svc);
        return true;
    } catch (const zypp::Exception& e) {
        m_lastError = "Failed to remove service: " + e.msg();
        return false;
    }
}

/**
 * @brief サービスのプロパティを変更する。
 * @param alias 対象のサービスエイリアス
 * @param newInfo 新しいサービス情報
 * @return 変更成功時 true
 */
bool ZyppManager::modifyService(const std::string& alias, const ServiceInfo& newInfo)
{
    std::lock_guard<std::mutex> lock(m_mutex);

    if (!m_repoManager) {
        m_lastError = "Backend not initialized";
        return false;
    }

    std::string validationErr;
    if (!newInfo.name.isEmpty() && !validateName(newInfo.name.toStdString(), validationErr)) {
        m_lastError = "Invalid service name: " + validationErr;
        return false;
    }
    if (!newInfo.url.isEmpty() && !validateUrl(newInfo.url.toStdString(), validationErr)) {
        m_lastError = "Invalid service URL: " + validationErr;
        return false;
    }

    try {
        zypp::ServiceInfo svc = m_repoManager->getService(alias);

        if (!newInfo.name.isEmpty())
            svc.setName(newInfo.name.toStdString());
        if (!newInfo.url.isEmpty())
            svc.setUrl(zypp::Url(newInfo.url.toStdString()));

        svc.setEnabled(newInfo.enabled);
        svc.setAutorefresh(newInfo.autoRefresh);

        m_repoManager->modifyService(alias, svc);
        return true;
    } catch (const zypp::Exception& e) {
        m_lastError = "Failed to modify service: " + e.msg();
        return false;
    }
}

/**
 * @brief サービスをリフレッシュする。
 * @param alias リフレッシュ対象のサービスエイリアス
 * @return リフレッシュ成功時 true
 */
bool ZyppManager::refreshService(const std::string& alias)
{
    std::lock_guard<std::mutex> lock(m_mutex);

    if (!m_repoManager) {
        m_lastError = "Backend not initialized";
        return false;
    }

    try {
        zypp::ServiceInfo svc = m_repoManager->getService(alias);
        // libzypp リフレッシュ前にサービス自体のポリシー検査を行う
        std::string svcErr;
        if (!checkServicePolicy(svc, svcErr)) {
            m_lastError = "Service violates policy: " + svcErr;
            qCWarning(lcZypp) << "refreshService refused for"
                              << QString::fromStdString(alias) << ":"
                              << QString::fromStdString(svcErr);
            return false;
        }
        // 隔離時の .service unlink はファイルがこのサービス専有であることが前提。
        // 複数サービスを定義した共有ファイル (libzypp 正式サポート) や既に不一致の
        // ファイルは、他サービスの誤削除を避けるため更新前に非破壊で拒否する。
        if (!serviceFileMatches(svc.filepath(), svc)) {
            m_lastError = "Service " + alias
                + " is defined in a shared or modified service file; refresh it with zypper";
            qCWarning(lcZypp) << "refreshService refused (non-exclusive service file) for"
                              << QString::fromStdString(alias);
            return false;
        }
        // unlink ガード用のスナップショットを libzypp 更新前に取得する
        const std::set<std::string> preexistingRepoFiles = snapshotRepoFiles();
        try {
            m_repoManager->refreshService(svc);
        } catch (...) {
            std::string report;
            sanitizeServiceRepos(alias, report, preexistingRepoFiles);
            quarantineServiceIfTainted(alias);
            throw;
        }
        std::string report;
        const bool reposClean = sanitizeServiceRepos(alias, report, preexistingRepoFiles);
        if (!quarantineServiceIfTainted(alias))
            return false;
        if (!reposClean) {
            m_lastError = "Service refreshed with violating repositories removed: " + report;
            return false;
        }
        return true;
    } catch (const zypp::Exception& e) {
        m_lastError = "Failed to refresh service: " + e.msg();
        return false;
    }
}

/**
 * @brief URL からサービスタイプを検出する。
 * @param url 検出対象のURL
 * @return サービスタイプ文字列
 */
std::string ZyppManager::probeServiceType(const std::string& url)
{
    std::lock_guard<std::mutex> lock(m_mutex);

    if (!m_repoManager) {
        m_lastError = "Backend not initialized";
        return "";
    }

    try {
        zypp::repo::ServiceType type = m_repoManager->probeService(zypp::Url(url));
        return type.asString();
    } catch (const zypp::Exception& e) {
        m_lastError = "Failed to probe service type: " + e.msg();
        return "";
    }
}

// -- パッケージ検索・一覧 --

/**
 * @brief パッケージを検索する。
 *
 * 名前、概要、説明からリテラル部分一致 (ASCII 大文字小文字不問) で検索する。
 * 正規表現は使わない (ReDoS 防止)。
 * @param query 検索クエリ文字列 (256 バイト超は拒否)
 * @param flags 検索フラグ (SearchFlag のビットマスク)
 * @return 一致したパッケージ情報のベクタ
 */
std::vector<PackageInfo> ZyppManager::searchPackages(const std::string& query, int flags) const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    std::vector<PackageInfo> result;

    if (!m_initialized)
        return result;

    if (query.size() > 256) {
        m_lastError = "Search query too long";
        return result;
    }

    auto toAsciiLower = [](const std::string &s) {
        std::string out(s);
        for (char &ch : out)
            ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
        return out;
    };
    const std::string loweredQuery = toAsciiLower(query);

    auto containsLiteral = [&loweredQuery, &toAsciiLower](const std::string &text) {
        return toAsciiLower(text).find(loweredQuery) != std::string::npos;
    };

    auto pool = m_zypp->poolProxy();

    for (auto it = pool.byKindBegin<zypp::Package>();
         it != pool.byKindEnd<zypp::Package>(); ++it) {

        const auto& sel = *it;
        bool match = false;

        auto obj = sel->candidateObj() ? sel->candidateObj() : sel->installedObj();
        if (!obj)
            continue;

        if ((flags & static_cast<int>(SearchFlag::Name)) &&
            containsLiteral(sel->name()))
            match = true;

        if (!match && (flags & static_cast<int>(SearchFlag::Summary)) &&
            containsLiteral(obj->summary()))
            match = true;

        if (!match && (flags & static_cast<int>(SearchFlag::Description)) &&
            containsLiteral(obj->description()))
            match = true;

        if (match)
            result.push_back(makePackageInfo(sel));
    }

    return result;
}

/**
 * @brief パッケージの詳細情報を取得する。
 *
 * 依存関係、ファイル一覧、利用可能バージョン等を含む。
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
        details.sourceRpm   = QString::fromStdString(pkg->sourcePkgName() + "-" + pkg->sourcePkgEdition().asString());
        details.url         = QString::fromStdString(pkg->url());

        // 依存関係
        for (const auto& cap : pkg->dep(zypp::Dep::PROVIDES))
            details.provides << QString::fromStdString(cap.asString());
        for (const auto& cap : pkg->dep(zypp::Dep::REQUIRES))
            details.requires << QString::fromStdString(cap.asString());
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

        // 利用可能バージョン
        for (auto avail = sel->availableBegin(); avail != sel->availableEnd(); ++avail) {
            QVariantMap ver;
            ver["version"]   = QString::fromStdString(avail->edition().asString());
            ver["arch"]      = QString::fromStdString(avail->arch().asString());
            ver["repoAlias"] = QString::fromStdString(avail->repoInfo().alias());
            ver["repoName"]  = QString::fromStdString(avail->repoInfo().name());
            details.availableVersions.append(ver);
        }

        break;
    }

    return details;
}

/**
 * @brief 指定リポジトリに属するパッケージ一覧を取得する。
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
        auto cand = sel->candidateObj();
        if (cand && cand.repoInfo().alias() == repoAlias) {
            result.push_back(makePackageInfo(sel));
        }
    }

    return result;
}

// -- パターン・パッチ --

/**
 * @brief ユーザ可視パターンの一覧を取得する。
 *
 * 非表示パターンは除外し、order でソートして返す。
 * @return PatternInfo のベクタ
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
        } catch (...) {
            info.order = 0;
        }

        result.push_back(std::move(info));
    }

    // orderでソート
    std::sort(result.begin(), result.end(),
              [](const PatternInfo& a, const PatternInfo& b) { return a.order < b.order; });

    return result;
}

/**
 * @brief 指定パターンに属するパッケージ一覧を取得する。
 * @param patternName パターン名
 * @return パッケージ情報のベクタ
 */
std::vector<PackageInfo> ZyppManager::getPackagesByPattern(const std::string& patternName) const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    std::vector<PackageInfo> result;

    if (!m_initialized)
        return result;

    // パターンを検索
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

        // パターンの内容 (パッケージ一覧) を取得 (YaSTのYQPkgPatternList::filter()と同様)
        zypp::Pattern::Contents contents(pat->contents());
        for (auto cit = contents.selectableBegin();
             cit != contents.selectableEnd(); ++cit) {

            auto pkgSel = *cit;
            if (!pkgSel)
                continue;

            // パッケージのみ取得
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
 * @brief パッチ一覧を取得する。
 * @param category パッチカテゴリフィルタ (PatchCategory、-1 で全件)
 * @return PatchInfo のベクタ
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

        // カテゴリフィルタ
        if (category >= 0 && category != static_cast<int>(PatchCategory::All)) {
            std::string cat = patch->category();
            bool match = false;
            switch (static_cast<PatchCategory>(category)) {
                case PatchCategory::Security:    match = (cat == "security"); break;
                case PatchCategory::Recommended: match = (cat == "recommended"); break;
                case PatchCategory::Optional:    match = (cat == "optional"); break;
                case PatchCategory::Feature:     match = (cat == "feature"); break;
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

        result.push_back(std::move(info));
    }

    return result;
}

// -- 変更予定パッケージ一覧 --

/**
 * @brief 変更予定のパッケージ一覧を取得する。
 *
 * Install, AutoInstall, Update, AutoUpdate, Del, AutoDel のステータスを持つパッケージを返す。
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
        // Install, AutoInstall, Update, AutoUpdate, Del, AutoDel のみ
        if (st == zypp::ui::S_Install   || st == zypp::ui::S_AutoInstall ||
            st == zypp::ui::S_Update    || st == zypp::ui::S_AutoUpdate  ||
            st == zypp::ui::S_Del       || st == zypp::ui::S_AutoDel) {
            result.push_back(makePackageInfo(sel));
        }
    }

    return result;
}

// -- 状態管理 --

/**
 * @brief パッケージのステータスを変更する。
 *
 * zypp ステートマシンが直接遷移を拒否する場合はフォールバック遷移を試みる。
 * @param name パッケージ名
 * @param status 新しいステータス (PackageStatus)
 * @return 変更成功時 true
 */
bool ZyppManager::setPackageStatus(const std::string& name, int status)
{
    std::lock_guard<std::mutex> lock(m_mutex);

    if (!m_initialized)
        return false;

    auto pool = m_zypp->poolProxy();
    for (auto it = pool.byKindBegin<zypp::Package>();
         it != pool.byKindEnd<zypp::Package>(); ++it) {

        const auto& sel = *it;
        if (sel->name() == name) {
            auto target = toZyppStatus(status);
            if (sel->setStatus(target))
                return true;

            // zyppステートマシンが直接遷移を拒否 → フォールバック
            auto cur = sel->status();
            if (cur == zypp::ui::S_AutoInstall && target == zypp::ui::S_NoInst) {
                // AutoInstall → Taboo (自動インストール禁止)
                return sel->setStatus(zypp::ui::S_Taboo);
            }
            if (cur == zypp::ui::S_AutoDel && target == zypp::ui::S_KeepInstalled) {
                // AutoDel → KeepInstalled (自動削除キャンセル)
                return sel->setStatus(zypp::ui::S_KeepInstalled);
            }
            if (cur == zypp::ui::S_AutoUpdate && target == zypp::ui::S_KeepInstalled) {
                // AutoUpdate → KeepInstalled (自動更新キャンセル)
                return sel->setStatus(zypp::ui::S_KeepInstalled);
            }
            if (cur == zypp::ui::S_Taboo && target == zypp::ui::S_Install) {
                // Taboo → NoInst → Install
                sel->setStatus(zypp::ui::S_NoInst);
                return sel->setStatus(zypp::ui::S_Install);
            }

            m_lastError = "Status transition not allowed";
            return false;
        }
    }

    m_lastError = "Package not found: " + name;
    return false;
}

/**
 * @brief パッケージのバージョンを変更する。
 *
 * 指定されたバージョン・アーキテクチャ・リポジトリに一致する PoolItem を候補として設定し、
 * インストール済みパッケージの場合はステータスを S_Update (または S_KeepInstalled) に変更する。
 * YaST の YQPkgVersionsView::checkForChangedCandidate() と同等の動作。
 * @param name パッケージ名
 * @param version 選択バージョン (edition 文字列)
 * @param arch アーキテクチャ
 * @param repoAlias リポジトリエイリアス
 * @return 変更成功時 true
 */
bool ZyppManager::setPackageVersion(const std::string& name, const std::string& version,
                                    const std::string& arch, const std::string& repoAlias)
{
    std::lock_guard<std::mutex> lock(m_mutex);

    if (!m_initialized)
        return false;

    auto pool = m_zypp->poolProxy();
    for (auto it = pool.byKindBegin<zypp::Package>();
         it != pool.byKindEnd<zypp::Package>(); ++it) {

        const auto& sel = *it;
        if (sel->name() != name)
            continue;

        // version + arch + repoAlias に一致する PoolItem を検索
        zypp::PoolItem matchedItem;
        for (auto avail = sel->availableBegin(); avail != sel->availableEnd(); ++avail) {
            if (avail->edition().asString() == version &&
                avail->arch().asString() == arch &&
                avail->repoInfo().alias() == repoAlias) {
                matchedItem = *avail;
                break;
            }
        }

        if (!matchedItem) {
            m_lastError = "Matching version not found: " + version;
            return false;
        }

        // 候補バージョンを設定
        sel->setCandidate(matchedItem);

        // インストール済みパッケージのステータスを決定
        if (!sel->installedEmpty()) {
            auto inst = sel->installedObj();
            bool isSameVersion = (inst.edition().asString() == version &&
                                  inst.arch().asString() == arch);

            if (isSameVersion) {
                // インストール済みと同じバージョンを選択 → 変更なし
                auto cur = sel->status();
                if (cur != zypp::ui::S_KeepInstalled &&
                    cur != zypp::ui::S_Protected) {
                    sel->setStatus(zypp::ui::S_KeepInstalled);
                }
            } else {
                // 異なるバージョンを選択 → 更新
                if (!sel->setStatus(zypp::ui::S_Update)) {
                    // フォールバック: AutoUpdate → KeepInstalled → Update
                    auto cur = sel->status();
                    if (cur == zypp::ui::S_AutoUpdate || cur == zypp::ui::S_AutoDel) {
                        sel->setStatus(zypp::ui::S_KeepInstalled);
                        sel->setStatus(zypp::ui::S_Update);
                    }
                }
            }
        }

        return true;
    }

    m_lastError = "Package not found: " + name;
    return false;
}

/**
 * @brief パターンのステータスを変更する。
 * @param name パターン名
 * @param status 新しいステータス (PackageStatus)
 * @return 変更成功時 true
 */
bool ZyppManager::setPatternStatus(const std::string& name, int status)
{
    std::lock_guard<std::mutex> lock(m_mutex);

    if (!m_initialized)
        return false;

    auto pool = m_zypp->poolProxy();
    for (auto it = pool.byKindBegin<zypp::Pattern>();
         it != pool.byKindEnd<zypp::Pattern>(); ++it) {

        const auto& sel = *it;
        if (sel->name() == name) {
            return sel->setStatus(toZyppStatus(status));
        }
    }

    m_lastError = "Pattern not found: " + name;
    return false;
}

/**
 * @brief パッケージ・パターン・パッチの選択状態を保存する。
 */
void ZyppManager::saveState()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_initialized) {
        m_zypp->pool().proxy().saveState<zypp::Package>();
        m_zypp->pool().proxy().saveState<zypp::Pattern>();
        m_zypp->pool().proxy().saveState<zypp::Patch>();
    }
}

/**
 * @brief 保存された選択状態を復元する。
 */
void ZyppManager::restoreState()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_initialized) {
        m_zypp->pool().proxy().restoreState<zypp::Package>();
        m_zypp->pool().proxy().restoreState<zypp::Pattern>();
        m_zypp->pool().proxy().restoreState<zypp::Patch>();
    }
}

// -- 依存関係解決 --


/**
 * @brief 依存関係を解決する。
 *
 * ソルバーを実行し、衝突がある場合は問題リストと解決策を返す。
 * @return ソルバー結果 (success, problems)
 */
ZyppManager::SolverResult ZyppManager::resolveDependencies()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    SolverResult result;

    if (!m_initialized) {
        result.success = false;
        return result;
    }

    // YaSTと同じ直接呼び出しパターン — fork()はlibzyppの内部状態を破壊するため使用しない
    try {
        auto resolver = m_zypp->resolver();
        resolver->setDefaultSolverFlags();          // zypp.confデフォルト再適用
        resolver->setIgnoreAlreadyRecommended(true); // 既推奨パッケージの再展開を抑制

        result.success = resolver->resolvePool();
    } catch (const zypp::Exception& e) {
        m_lastError = "Solver error: " + e.asString();
        result.success = false;
        return result;
    } catch (const std::exception& e) {
        m_lastError = std::string("Solver error: ") + e.what();
        result.success = false;
        return result;
    }

    if (!result.success) {
        // 衝突あり — 問題リストを取得
        try {
            m_problems = m_zypp->resolver()->problems();

            for (const auto& problem : m_problems) {
                ConflictInfo conflict;
                conflict.description = QString::fromStdString(problem->description());
                conflict.details     = QString::fromStdString(problem->details());

                auto solutions = problem->solutions();
                for (const auto& sol : solutions) {
                    QVariantMap solMap;
                    solMap["description"] = QString::fromStdString(sol->description());
                    solMap["details"]     = QString::fromStdString(sol->details());
                    conflict.solutions.append(solMap);
                }

                result.problems.push_back(std::move(conflict));
            }
        } catch (const std::exception& e) {
            qWarning() << "Failed to get solver problems:" << e.what();
        }
    }

    return result;
}

/**
 * @brief 全パッケージの更新を実行する。
 *
 * libzypp の Resolver::doUpdate() を呼び出し、
 * インストール済みパッケージのうち更新可能なものを全てマークし依存解決を行う。
 * @return SolverResult (成功フラグ + 衝突リスト)
 */
ZyppManager::SolverResult ZyppManager::updateAllPackages()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    SolverResult result;

    if (!m_initialized) {
        result.success = false;
        return result;
    }

    try {
        auto resolver = m_zypp->resolver();
        resolver->setDefaultSolverFlags();
        resolver->setIgnoreAlreadyRecommended(true);

        resolver->doUpdate();
        result.success = resolver->resolvePool();
    } catch (const zypp::Exception& e) {
        m_lastError = "Update all error: " + e.asString();
        result.success = false;
        return result;
    } catch (const std::exception& e) {
        m_lastError = std::string("Update all error: ") + e.what();
        result.success = false;
        return result;
    }

    if (!result.success) {
        // 衝突あり — 問題リストを取得
        try {
            m_problems = m_zypp->resolver()->problems();

            for (const auto& problem : m_problems) {
                ConflictInfo conflict;
                conflict.description = QString::fromStdString(problem->description());
                conflict.details     = QString::fromStdString(problem->details());

                auto solutions = problem->solutions();
                for (const auto& sol : solutions) {
                    QVariantMap solMap;
                    solMap["description"] = QString::fromStdString(sol->description());
                    solMap["details"]     = QString::fromStdString(sol->details());
                    conflict.solutions.append(solMap);
                }

                result.problems.push_back(std::move(conflict));
            }
        } catch (const std::exception& e) {
            qWarning() << "Failed to get solver problems:" << e.what();
        }
    }

    return result;
}

/**
 * @brief 依存関係の衝突に対する解決策を適用する。
 * @param problemIndex 問題のインデックス
 * @param solutionIndex 解決策のインデックス
 * @return 適用成功時 true
 */
bool ZyppManager::applySolution(int problemIndex, int solutionIndex)
{
    std::lock_guard<std::mutex> lock(m_mutex);

    if (problemIndex < 0 || problemIndex >= static_cast<int>(m_problems.size()))
        return false;

    auto problemIt = m_problems.begin();
    std::advance(problemIt, problemIndex);

    auto solutions = (*problemIt)->solutions();
    if (solutionIndex < 0 || solutionIndex >= static_cast<int>(solutions.size()))
        return false;

    auto solutionIt = solutions.begin();
    std::advance(solutionIt, solutionIndex);

    zypp::ProblemSolutionList solutionList;
    solutionList.push_back(*solutionIt);
    m_zypp->resolver()->applySolutions(solutionList);
    return true;
}

// -- コミット --

/**
 * @brief パッケージの変更をコミットする。
 *
 * libzypp のコールバックレシーバーを登録して進捗をリアルタイム通知し、
 * transactionStepList を走査して正確なカウント・サイズ情報を収集する。
 * @param progressCallback 進捗コールバック
 * @return コミット結果
 */
ZyppManager::CommitResult ZyppManager::commit(ProgressCallbackFn progressCallback,
                                              StateEventCallbackFn stateCallback)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    CommitResult result;

    if (!m_initialized) {
        result.success = false;
        result.errorMessage = "Backend not initialized";
        return result;
    }

    // コミット前にプール既知の全リポジトリをポリシー検査。不適合があれば拒否する。
    // NOTE: m_mutex 保持中のため checkRepoPolicy (static、ロックなし) を直接呼ぶ。
    for (const auto &known : m_zypp->pool().knownRepositories()) {
        std::string policyErr;
        if (!checkRepoPolicy(known.info(), policyErr)) {
            result.success = false;
            result.errorMessage = "Commit refused, repository violates policy: " + policyErr;
            m_lastError = result.errorMessage;
            return result;
        }
        std::string originsErr;
        if (!checkRepoOrigins(known.info(), originsErr)) {
            result.success = false;
            result.errorMessage = "Commit refused, repository violates policy: " + originsErr;
            m_lastError = result.errorMessage;
            return result;
        }
    }

    try {
        m_cancelRequested = false;

        // 経過時間計測開始
        auto startTime = std::chrono::steady_clock::now();

        // アクション予定数をプールから計算
        // ダウンロード + インストール/削除 の両方をステップとしてカウント
        int downloadSteps = 0;
        int actionSteps   = 0;
        for (auto it = m_zypp->pool().begin(); it != m_zypp->pool().end(); ++it) {
            if (it->status().transacts()) {
                ++actionSteps;
                // インストール/更新はダウンロードも必要
                if (!it->status().isToBeUninstalled())
                    ++downloadSteps;
            }
        }
        int totalSteps = downloadSteps + actionSteps;
        qCDebug(lcZypp) << "commit: actionSteps=" << actionSteps
                        << "downloadSteps=" << downloadSteps;
        if (totalSteps == 0) {
            qCDebug(lcZypp) << "commit: no packages to transact in pool";
            totalSteps = 1;
        }

        int completedSteps = 0;

        // Install/Remove/Download の problem() コールバックで発生した
        // エラー詳細を格納する (commit 失敗時に errorMessage として返す)
        std::string problemDetail;

        // コールバックレシーバー登録
        InstallReceiver installRcv(progressCallback, stateCallback, completedSteps,
                                   totalSteps, m_cancelRequested, problemDetail);
        RemoveReceiver removeRcv(progressCallback, stateCallback, completedSteps,
                                 totalSteps, m_cancelRequested, problemDetail);
        DownloadReceiver downloadRcv(progressCallback, stateCallback, completedSteps,
                                     totalSteps, m_cancelRequested, problemDetail);
        InstallReceiverSA installRcvSA(progressCallback, stateCallback, completedSteps,
                                       totalSteps, m_cancelRequested);
        RemoveReceiverSA removeRcvSA(progressCallback, stateCallback, completedSteps,
                                     totalSteps, m_cancelRequested);
        installRcv.connect();
        removeRcv.connect();
        downloadRcv.connect();
        installRcvSA.connect();
        removeRcvSA.connect();

        // コミット実行
        zypp::ZYppCommitPolicy policy;
        policy.dryRun(false);
        auto commitResult = m_zypp->commit(policy);

        // レシーバー切断
        installRcv.disconnect();
        removeRcv.disconnect();
        downloadRcv.disconnect();
        installRcvSA.disconnect();
        removeRcvSA.disconnect();

        result.success = commitResult.noError();
        auto txSteps = std::distance(commitResult.transaction().begin(),
                                     commitResult.transaction().end());
        qCDebug(lcZypp) << "commit: noError=" << result.success
                        << "transaction_steps=" << txSteps;

        // transaction 全体を走査して正確なカウント・サイズ情報を収集
        const auto &trans = commitResult.transaction();

        // 更新判定用: 同一ident が INSTALL と ERASE の両方にある場合は更新
        std::set<zypp::IdString> eraseIdents;
        std::set<zypp::IdString> installIdents;
        for (auto it = trans.begin(); it != trans.end(); ++it) {
            if (it->stepStage() != zypp::sat::Transaction::STEP_DONE)
                continue;
            if (it->stepType() == zypp::sat::Transaction::TRANSACTION_ERASE)
                eraseIdents.insert(it->ident());
            else if (it->stepType() == zypp::sat::Transaction::TRANSACTION_INSTALL
                     || it->stepType() == zypp::sat::Transaction::TRANSACTION_MULTIINSTALL)
                installIdents.insert(it->ident());
        }

        for (auto it = trans.begin(); it != trans.end(); ++it) {
            if (it->stepType() == zypp::sat::Transaction::TRANSACTION_IGNORE)
                continue;

            std::string name = it->ident().asString();

            if (it->stepStage() == zypp::sat::Transaction::STEP_DONE) {
                auto type = it->stepType();

                if (type == zypp::sat::Transaction::TRANSACTION_INSTALL
                    || type == zypp::sat::Transaction::TRANSACTION_MULTIINSTALL) {
                    // サイズ集計
                    auto solv = it->satSolvable();
                    if (solv) {
                        result.totalInstalledSize += solv.installSize();
                        result.totalDownloadSize  += solv.downloadSize();
                    }
                    // 更新 vs 新規インストール判定
                    if (eraseIdents.count(it->ident())) {
                        result.updated++;
                        result.updatedPackages.push_back(name);
                    } else {
                        result.installed++;
                        result.installedPackages.push_back(name);
                    }
                } else if (type == zypp::sat::Transaction::TRANSACTION_ERASE) {
                    // 同一ident の INSTALL がなければ純粋な削除
                    if (!installIdents.count(it->ident())) {
                        result.removed++;
                        result.removedPackages.push_back(name);
                    }
                }
            } else if (it->stepStage() == zypp::sat::Transaction::STEP_ERROR) {
                result.failedPackages.push_back(name);
            }
        }

        // 経過時間計算
        auto endTime = std::chrono::steady_clock::now();
        result.elapsedSeconds =
            std::chrono::duration<double>(endTime - startTime).count();

        qCDebug(lcZypp) << "commit result: success=" << result.success
                        << "installed=" << result.installed
                        << "updated=" << result.updated
                        << "removed=" << result.removed
                        << "failed=" << result.failedPackages.size();
        // problem() コールバックで詳細が記録されていれば優先採用
        if (!result.success && result.errorMessage.empty()) {
            if (!problemDetail.empty())
                result.errorMessage = problemDetail;
            else
                result.errorMessage = m_lastError;
        }

        return result;
    } catch (const zypp::Exception& e) {
        m_lastError = "Commit failed: " + e.asString();
        result.success = false;
        result.errorMessage = m_lastError;
        qCWarning(lcZypp) << "commit exception:" << QString::fromStdString(e.asString())
                          << "| history:" << QString::fromStdString(e.historyAsString());
        return result;
    } catch (const std::exception& e) {
        m_lastError = std::string("Commit failed: ") + e.what();
        result.success = false;
        result.errorMessage = m_lastError;
        qCWarning(lcZypp) << "commit std::exception:" << e.what();
        return result;
    }
}

// -- ディスク使用量 --

/**
 * @brief ディスク使用量を取得する。
 *
 * zypp::DiskUsageCounter を使用してシステムのマウントポイントを検出し、
 * 現在のプール状態（パッケージ選択変更）に基づくディスク使用量を計算する。
 * @return DiskUsageInfo のベクタ (読み取り専用パーティションは除外)
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
            info.totalSize    = static_cast<quint64>(mp.total_size) * 1024;  // KiB → bytes
            info.usedSize     = static_cast<quint64>(mp.used_size)  * 1024;  // KiB → bytes
            info.packageUsage = static_cast<qint64>(mp.pkg_size - mp.used_size) * 1024;  // KiB → bytes
            result.push_back(info);
        }
    } catch (const zypp::Exception& e) {
        m_lastError = "Failed to get disk usage: " + e.msg();
    }

    return result;
}

// -- ヘルパー --

/**
 * @brief Selectable から PackageInfo を生成するヘルパー。
 * @param sel libzypp Selectable ポインタ
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
    }

    return info;
}

/**
 * @brief zypp::ui::Status を qZypper::PackageStatus に変換する。
 * @param status zypp のステータス
 * @return qZypper のステータス値
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
 * @brief qZypper::PackageStatus を zypp::ui::Status に変換する。
 * @param status qZypper のステータス値
 * @return zypp のステータス
 */
zypp::ui::Status ZyppManager::toZyppStatus(int status)
{
    switch (static_cast<PackageStatus>(status)) {
        case PackageStatus::NoInst:        return zypp::ui::S_NoInst;
        case PackageStatus::Install:       return zypp::ui::S_Install;
        case PackageStatus::AutoInstall:   return zypp::ui::S_AutoInstall;
        case PackageStatus::KeepInstalled: return zypp::ui::S_KeepInstalled;
        case PackageStatus::Update:        return zypp::ui::S_Update;
        case PackageStatus::AutoUpdate:    return zypp::ui::S_AutoUpdate;
        case PackageStatus::Del:           return zypp::ui::S_Del;
        case PackageStatus::AutoDel:       return zypp::ui::S_AutoDel;
        case PackageStatus::Taboo:         return zypp::ui::S_Taboo;
        case PackageStatus::Protected:     return zypp::ui::S_Protected;
        default:                           return zypp::ui::S_NoInst;
    }
}

} // namespace qZypper
