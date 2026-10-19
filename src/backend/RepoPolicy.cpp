#include <cctype>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <set>
#include <string>
#include <boost/logic/tribool.hpp>
#include <zypp/Url.h>
#include <zypp/ZConfig.h>
#include <zypp-core/fs/PathInfo.h>
#include <zypp-core/base/String.h>
#include "RepoPolicy.h"
#include "ZyppLogging.h"

namespace qZypper {

/**
 * @brief リポジトリ/サービスURLを検証する
 *
 * 空URL・制御文字を拒否し、zypp::Urlで構文解析する
 * スキームは許可リスト (http, https, ftp, tftp, file, dir, hd, iso, cd, dvd, nfs, nfs4, smb, cifs) のみ許可する (特にpluginを拒否)
 *
 * isoスキームのurlクエリパラメータは再帰的に検証する
 *
 * @param url 検証対象URL
 * @param err 失敗時のエラー詳細
 * @param allowIsoNesting isoネスト検証を許可するか
 * @return 有効時はtrue
 */
bool RepoPolicy::validateUrl(const std::string &url, std::string &err, bool allowIsoNesting)
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
    }
    catch (const zypp::Exception &e) {
        err = std::string("malformed URL: ") + e.msg();
        return false;
    }
    catch (const std::exception &e) {
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
        }
        catch (const zypp::Exception &e) {
            err = std::string("bad iso query parameter: ") + e.msg();
            return false;
        }
        catch (const std::exception &e) {
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
 * @brief リポジトリ/サービスエイリアスを検証する
 *
 * INIインジェクション (改行・セクション記号) と パストラバーサル ('/', '\\', 先頭'.') を防ぐ
 *
 * @param alias 検証対象エイリアス
 * @param err 失敗時のエラー詳細
 * @return 有効時true
 */
bool RepoPolicy::validateAlias(const std::string &alias, std::string &err)
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
 * @brief リポジトリ/サービス表示名を検証する
 *
 * 制御文字 (CR/LFを含む) を拒否する
 * 空文字列は有効
 * (呼び出し側APIが空を許す箇所用)
 *
 * @param name 検証対象名
 * @param err 失敗時のエラー詳細
 * @return 有効時はtrue
 */
bool RepoPolicy::validateName(const std::string &name, std::string &err)
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
 * @brief リポジトリ優先度を検証する
 *
 * zypp::RepoInfo::setPriorityはunsignedを受け取るため、負値は巨大値に化ける
 * また0は「既定値 (99)」の意味になるため受け付けない
 *
 * UIと同じ1〜200に制限する
 *
 * @param priority 検証対象の優先度
 * @param err 失敗時のエラー詳細
 * @return 有効時true
 */
bool RepoPolicy::validatePriority(long long priority, std::string &err)
{
    if (priority < 1 || priority > 200) {
        err = "priority must be between 1 and 200";
        return false;
    }
    return true;
}

/**
 * @brief リポジトリポリシーを検査する
 *
 * サービスやミラーリスト由来のリポジトリデータがURL/INI/GPG検証を迂回するのを防ぐ
 * 制御文字・不正URL・不審なミラーリスト/GPG鍵URLを拒否する
 *
 * エラー文にはエイリアス・理由・スキームのみを含め、完全なURLは含めない
 * (認証情報を含む可能性があるため)
 *
 * @param repo 検査対象リポジトリ
 * @param err 失敗時のエラー詳細
 * @return ポリシー準拠時はtrue
 */
bool RepoPolicy::checkRepoPolicy(const zypp::RepoInfo &repo, std::string &err)
{
    const std::string alias = repo.alias();

    auto hasControlChar = [](const std::string &s) {
        for (unsigned char c : s) {
            if (c < 0x20 || c == 0x7f)
                return true;
        }
        return false;
    };

    // エイリアス・名前に制御文字があればINI書き出し時の注入とみなす
    if (hasControlChar(alias) || hasControlChar(repo.name())) {
        err = "repo '" + alias + "': alias or name contains control characters";
        return false;
    }

    // 実効ベースURL (変数置換済み) を全件検証
    for (const auto &u : repo.baseUrls()) {
        const std::string urlStr = u.asString();
        std::string urlErr;

        if (!validateUrl(urlStr, urlErr)) {
            err = "repo '" + alias + "': invalid base URL (" + urlErr + ")";
            return false;
        }
    }

    // ミラーリスト/メタリンクは信頼できる転送手段のみ許可する
    // RepoMirrorListは返却ミラー端点のスキームを濾過しないため
    const zypp::Url mirrorUrl = repo.mirrorListUrl();
    if (!mirrorUrl.asString().empty()) {
        std::string scheme;
        try {
            scheme = zypp::str::toLower(mirrorUrl.getScheme());
        }
        catch (...) {
            scheme.clear();
        }

        if (scheme != "https" && scheme != "file" && scheme != "dir") {
            err = "repo '" + alias + "': disallowed mirrorlist scheme: " + scheme;
            return false;
        }
    }

    // GPG鍵URLを全件検証 (スキームのみ記録し完全なURLは出さない)
    for (const auto &u : repo.gpgKeyUrls()) {
        const std::string urlStr = u.asString();
        std::string urlErr;

        if (!validateUrl(urlStr, urlErr)) {
            std::string scheme;
            try {
                scheme = zypp::Url(urlStr).getScheme();
            }
            catch (...) {
                scheme = "?";
            }

            err = "repo '" + alias + "': invalid GPG key URL (" + urlErr + ", scheme: " + scheme + ")";
            return false;
        }
    }

    // サービス由来リポジトリはリモートrepoindexがGPG設定・ミラーリストを注入できるため、生の三値設定と明示ミラーリストを検査する
    // 非サービス (管理者設定) は現行動作を維持する
    if (!repo.service().empty()) {
        if (zypp::ZConfig::instance().gpgCheck() && rawGpgChecksWeakened(repo)) {
            err = "repo '" + alias + "': signature checking weakened by remote repoindex";
            return false;
        }

        // サービスが明示ミラーリスト/メタリンクを注入してはならない
        if (!repo.rawCfgMirrorlistUrl().asString().empty() || !repo.rawCfgMetalinkUrl().asString().empty()) {
            err = "repo '" + alias + "': mirrorlist configured by remote service";
            return false;
        }
    }

    return true;
}

/**
 * @brief リポジトリの生GPG設定が弱体化されているか検査する
 */
bool RepoPolicy::rawGpgChecksWeakened(const zypp::RepoInfo &repo)
{
    zypp::TriBool g, r, p;

    try {
        repo.getRawGpgChecks(g, r, p);
    }
    catch (...) {
        // 生値が読めなければ安全側に倒す
        return true;
    }

    // 確定的にfalseのものが1つでもあれば弱体化
    // (gpgcheck = 1, repo_gpgcheck = 0, pkg_gpgcheck = 0等を検出)
    auto isDeterminatelyFalse = [](const zypp::TriBool &t) {
        if (boost::logic::indeterminate(t))
            return false;

        return !static_cast<bool>(t);
    };

    return isDeterminatelyFalse(g) || isDeterminatelyFalse(r) || isDeterminatelyFalse(p);
}

/**
 * @brief ミラーリスト由来の全エンドポイントURLを検証する
 */
bool RepoPolicy::checkRepoOrigins(const zypp::RepoInfo &repo, std::string &err)
{
    const std::string alias = repo.alias();

    try {
        // 同一RepoInfoオブジェクト上で取得する
        // libzyppは取得済みミラーリストを共有pimplに約1時間キャッシュするため、
        // 検査対象とlibzypp使用分がその範囲で一致する
        const zypp::MirroredOriginSet origins = repo.repoOrigins();
        for (const auto &origin : origins) {
            for (const auto &endpoint : origin) {
                std::string urlStr;
                try {
                    urlStr = endpoint.url().asString();
                }
                catch (const std::exception &e) {
                    err = "repo '" + alias + "': unreadable mirror endpoint: " + e.what();
                    return false;
                }

                std::string urlErr;
                if (!validateUrl(urlStr, urlErr)) {
                    std::string scheme;
                    try {
                        scheme = endpoint.url().getScheme();
                    }
                    catch (...) {
                        scheme = "?";
                    }

                    err = "repo '" + alias + "': invalid mirror endpoint (" + urlErr + ", scheme: " + scheme + ")";
                    qCWarning(lcZypp) << "Mirror endpoint violates policy for repo"
                                      << QString::fromStdString(alias)
                                      << ": scheme=" << QString::fromStdString(scheme);
                    return false;
                }
            }
        }
    }
    catch (const zypp::Exception &e) {
        err = "repo '" + alias + "': cannot verify mirror endpoints: " + e.msg();
        return false;
    }
    catch (const std::exception &e) {
        err = "repo '" + alias + "': cannot verify mirror endpoints: " + e.what();
        return false;
    }

    return true;
}

/**
 * @brief サービス情報を検査する
 */
bool RepoPolicy::checkServicePolicy(const zypp::ServiceInfo &svc, std::string &err)
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

    // qZypperはプラグインサービスを更新しない
    // zypperの利用を促す
    if (svc.type() == zypp::repo::ServiceType::PLUGIN) {
        err = "service '" + alias + "': plugin services are not supported, use zypper";
        return false;
    }

    // repoStatesキーは.serviceファイルに素通しされるためINI注入対策
    for (const auto &kv : svc.repoStates()) {
        if (!validateAlias(kv.first, fieldErr)) {
            err = "service '" + alias + "': invalid repository state entry";
            return false;
        }
    }

    return true;
}

/**
 * @brief on-diskの.serviceファイルがメモリ上のサービスと一致するか検査する
 */
bool RepoPolicy::serviceFileMatches(const zypp::Pathname &path, const zypp::ServiceInfo &svc)
{
    // zypp::parser::ServiceFileReaderはlibzypp 17.38.16でexportされていないため、単一セクション・alias/type/url一致の最小INI照合を行う
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

    // libzyppはファイルへrawUrl (変数未展開・資格情報込み) を書くため、双方を解析して完全形式で比較する
    // (展開済みurl()と比べると正規サービスを誤隔離する)
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
            }
            catch (...) {
                ok = false;
            }
        }
        else if (key == "type") {
            if (value != wantType)
                ok = false;
        }
    }

    return sections == 1 && sectionName == svc.alias() && urlSeen && ok;
}

/**
 * @brief 既知リポジトリパス直下の通常ファイルを列挙する
 */
std::set<std::string> RepoPolicy::snapshotRepoFiles()
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

} // namespace qZypper
