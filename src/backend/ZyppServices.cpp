#include <cerrno>
#include <cstring>
#include <filesystem>
#include <set>
#include <string>
#include <zypp/ZConfig.h>
#include "ZyppManager.h"
#include "RepoPolicy.h"
#include "ZyppLogging.h"

namespace qZypper {

/**
 * @brief 既知サービスの一覧を取得する
 * @return ServiceInfoのベクタ
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
 * @brief サービスを追加する
 * @param url サービスURL
 * @param alias サービスエイリアス
 * @return 追加成功時はtrue
 */
bool ZyppManager::addService(const std::string& url, const std::string& alias)
{
    std::lock_guard<std::mutex> lock(m_mutex);

    // リポジトリ/プールの変更で、PoolItemを保持する既存の解決策は無効になる
    m_problems.clear();

    if (!m_repoManager) {
        m_lastError = "Backend not initialized";
        return false;
    }

    std::string validationErr;
    if (!RepoPolicy::validateAlias(alias, validationErr)) {
        m_lastError = "Invalid alias: " + validationErr;
        return false;
    }

    if (!RepoPolicy::validateUrl(url, validationErr)) {
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

        // unlinkガード用のスナップショットをlibzypp追加前に取得する
        const std::set<std::string> preexistingRepoFiles = RepoPolicy::snapshotRepoFiles();

        m_repoManager->addService(svc);
        added = true;

        try {
            m_repoManager->refreshService(svc);
        }
        catch (...) {
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
    }
    catch (const zypp::Exception& e) {
        std::string failure = "Failed to add service: " + e.msg();
        if (added) {
            // 部分的に追加されたサービスをロールバックする (best-effort)
            try {
                m_repoManager->removeService(svc);
            }
            catch (const zypp::Exception& rollbackErr) {
                failure += " (rollback failed: " + rollbackErr.msg() + ")";
            }
        }

        m_lastError = failure;
        return false;
    }
}

/**
 * @brief サービスを削除する
 * @param alias 削除対象のサービスエイリアス
 * @return 削除成功時はtrue
 */
bool ZyppManager::removeService(const std::string& alias)
{
    std::lock_guard<std::mutex> lock(m_mutex);

    // リポジトリ/プールの変更で、PoolItemを保持する既存の解決策は無効になる
    m_problems.clear();

    if (!m_repoManager) {
        m_lastError = "Backend not initialized";
        return false;
    }

    try {
        zypp::ServiceInfo svc = m_repoManager->getService(alias);
        m_repoManager->removeService(svc);
        return true;
    }
    catch (const zypp::Exception& e) {
        m_lastError = "Failed to remove service: " + e.msg();
        return false;
    }
}

/**
 * @brief サービスのプロパティを変更する
 * @param alias 対象のサービスエイリアス
 * @param changes 変更する項目 (未設定の項目は現状維持)
 * @return 変更成功時はtrue
 */
bool ZyppManager::modifyService(const std::string& alias, const ServiceChanges& changes)
{
    std::lock_guard<std::mutex> lock(m_mutex);

    // リポジトリ/プールの変更で、PoolItemを保持する既存の解決策は無効になる
    m_problems.clear();

    if (!m_repoManager) {
        m_lastError = "Backend not initialized";
        return false;
    }

    // 空文字のname / urlは従来どおり「現状維持」として扱う
    const bool hasName = changes.name && !changes.name->isEmpty();
    const bool hasUrl = changes.url && !changes.url->isEmpty();

    std::string validationErr;
    if (hasName && !RepoPolicy::validateName(changes.name->toStdString(), validationErr)) {
        m_lastError = "Invalid service name: " + validationErr;
        return false;
    }

    if (hasUrl && !RepoPolicy::validateUrl(changes.url->toStdString(), validationErr)) {
        m_lastError = "Invalid service URL: " + validationErr;
        return false;
    }

    try {
        zypp::ServiceInfo svc = m_repoManager->getService(alias);

        // 渡された項目だけを適用し、それ以外は現在値を維持する
        if (hasName)
            svc.setName(changes.name->toStdString());

        if (hasUrl)
            svc.setUrl(zypp::Url(changes.url->toStdString()));

        if (changes.enabled)
            svc.setEnabled(*changes.enabled);

        if (changes.autoRefresh)
            svc.setAutorefresh(*changes.autoRefresh);

        m_repoManager->modifyService(alias, svc);
        return true;
    }
    catch (const zypp::Exception& e) {
        m_lastError = "Failed to modify service: " + e.msg();
        return false;
    }
}

/**
 * @brief サービスをリフレッシュする
 * @param alias リフレッシュ対象のサービスエイリアス
 * @return リフレッシュ成功時はtrue
 */
bool ZyppManager::refreshService(const std::string& alias)
{
    std::lock_guard<std::mutex> lock(m_mutex);

    // リポジトリ/プールの変更で、PoolItemを保持する既存の解決策は無効になる
    m_problems.clear();

    if (!m_repoManager) {
        m_lastError = "Backend not initialized";
        return false;
    }

    try {
        zypp::ServiceInfo svc = m_repoManager->getService(alias);
        // libzyppリフレッシュ前にサービス自体のポリシー検査を行う
        std::string svcErr;
        if (!RepoPolicy::checkServicePolicy(svc, svcErr)) {
            m_lastError = "Service violates policy: " + svcErr;
            qCWarning(lcZypp) << "refreshService refused for"
                              << QString::fromStdString(alias) << ":"
                              << QString::fromStdString(svcErr);
            return false;
        }

        // 隔離時の.service unlinkはファイルがこのサービス専有であることが前提
        // 複数サービスを定義した共有ファイル (libzypp正式サポート) や 既に不一致のファイルは、他サービスの誤削除を避けるため更新前に非破壊で拒否する
        if (!RepoPolicy::serviceFileMatches(svc.filepath(), svc)) {
            m_lastError = "Service " + alias + " is defined in a shared or modified service file; refresh it with zypper";
            qCWarning(lcZypp) << "refreshService refused (non-exclusive service file) for" << QString::fromStdString(alias);
            return false;
        }

        // unlinkガード用のスナップショットをlibzypp更新前に取得する
        const std::set<std::string> preexistingRepoFiles = RepoPolicy::snapshotRepoFiles();
        try {
            m_repoManager->refreshService(svc);
        }
        catch (...) {
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
    }
    catch (const zypp::Exception& e) {
        m_lastError = "Failed to refresh service: " + e.msg();
        return false;
    }
}

/**
 * @brief サービス由来のリポジトリを浄化する
 *
 * 指定サービスの全リポジトリを検査し、ポリシー違反や署名検査弱体化があるものを削除する
 * 残留.repoファイルも削除する
 * (サービス生成.repoは単一リポジトリであり、残留内容はINI注入の可能性があるため)
 *
 * @param serviceAlias 対象サービスエイリアス
 * @param report 削除内容の報告 (エイリアスと理由のみ、URLなし)
 * @return 違反がなく全て健全時はtrue
 * @note 呼び出し側のm_mutex配下で呼ぶこと (非再帰mutexのため内部でロックしない)
 */
bool ZyppManager::sanitizeServiceRepos(const std::string &serviceAlias, std::string &report,
                                        const std::set<std::string> &preexistingRepoFiles)
{
    // NOTE: m_mutexは取得しない
    // 呼び出し側 (addService/refreshService) が保持している
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

        if (!RepoPolicy::checkRepoPolicy(repo, policyErr)) {
            reason = policyErr;
        }
        else if (gpgRequired && RepoPolicy::rawGpgChecksWeakened(repo)) {
            reason = "repo '" + alias + "': signature checking weakened by remote repoindex";
        }

        if (reason.empty())
            continue;

        // 違反リポジトリを削除する
        const zypp::Pathname fpath = repo.filepath();
        try {
            m_repoManager->removeRepository(repo);
        }
        catch (const zypp::Exception &e) {
            qCWarning(lcZypp) << "sanitizeServiceRepos: remove failed for"
                              << QString::fromStdString(alias) << ":"
                              << QString::fromStdString(e.msg());
        }

        // 残留.repoは共有ファイル削除を避けるためガード付きでのみ消す:
        // knownReposPath直下の通常ファイルで、サービス追加/更新前から存在せず、
        // 残存リポジトリのいずれにも所有されていない場合のみunlinkする
        if (!fpath.empty() && zypp::PathInfo(fpath).isExist()) {
            bool owned = false;
            for (const auto &remaining : m_repoManager->knownRepositories()) {
                if (remaining.filepath() == fpath) {
                    owned = true;
                    break;
                }
            }

            if (!owned && fpath.dirname() == reposDir && zypp::PathInfo(fpath).isFile() && preexistingRepoFiles.count(fpath.asString()) == 0) {
                if (zypp::filesystem::unlink(fpath) != 0) {
                    qCWarning(lcZypp) << "sanitizeServiceRepos: unlink failed for"
                                      << QString::fromStdString(fpath.asString());
                }
            }
            else {
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
 * @brief サービスの.serviceファイル汚染を検証し必要なら隔離する
 */
bool ZyppManager::quarantineServiceIfTainted(const std::string &serviceAlias)
{
    // NOTE: m_mutexは取得しない
    // 呼び出し側が保持している
    if (!m_repoManager)
        return true;

    zypp::ServiceInfo info;
    try {
        info = m_repoManager->getService(serviceAlias);
    }
    catch (...) {
        return true;  // 検証対象なし (既に削除済み等)
    }

    bool tainted = false;
    std::string detail;

    if (!RepoPolicy::checkServicePolicy(info, detail)) {
        qCWarning(lcZypp) << "quarantineServiceIfTainted: policy violation for service"
                          << QString::fromStdString(serviceAlias)
                          << ":" << QString::fromStdString(detail);
        tainted = true;
    }
    else {
        // on-diskの.serviceファイルを解析し、単一セクションかつ
        // alias/type/urlの一致を要求する (INI注入検出)
        try {
            if (!RepoPolicy::serviceFileMatches(info.filepath(), info))
                tainted = true;
        }
        catch (const std::exception &e) {
            qCWarning(lcZypp) << "quarantineServiceIfTainted: service file unreadable for"
                              << QString::fromStdString(serviceAlias) << ":" << e.what();
            tainted = true;
        }
        catch (...) {
            tainted = true;
        }
    }

    if (!tainted)
        return true;

    // 隔離 : サービス (配下リポジトリと.serviceファイル) を削除する
    zypp::Pathname fpath;
    try {
        fpath = info.filepath();
    }
    catch (...) {
    }

    bool removed = true;

    try {
        m_repoManager->removeService(serviceAlias);
    }
    catch (const zypp::Exception &e) {
        removed = false;
        qCWarning(lcZypp) << "quarantineServiceIfTainted: removeService failed for"
                          << QString::fromStdString(serviceAlias) << ":"
                          << QString::fromStdString(e.msg());
    }

    // removeService後に残存し、かつ既知サービスパス直下の通常ファイルならunlink
    const zypp::Pathname svcDir = zypp::ZConfig::instance().knownServicesPath();
    if (!fpath.empty() && fpath.dirname() == svcDir && zypp::PathInfo(fpath).isFile()) {
        if (zypp::filesystem::unlink(fpath) != 0) {
            qCWarning(lcZypp) << "quarantineServiceIfTainted: unlink failed for" << QString::fromStdString(fpath.asString());
        }
    }

    // 削除が拒否された場合 (SELinux等) は汚染ファイルが残るため、成功と報告しない
    // PathInfo::isExistはstatの失敗理由を区別せず (EACCES等も「存在しない」扱い)、シンボリックリンクも辿るため、lstatでENOENT/ENOTDIRの時のみ削除済みとみなす
    if (!fpath.empty()) {
        struct stat st {};

        if (::lstat(fpath.c_str(), &st) == 0) {
            removed = false;
        }
        else if (errno != ENOENT && errno != ENOTDIR) {
            const int savedErrno = errno;
            removed = false;
            qCWarning(lcZypp) << "quarantineServiceIfTainted: cannot verify removal of"
                              << QString::fromStdString(fpath.asString()) << ":"
                              << std::strerror(savedErrno);
        }
    }

    m_lastError = removed ? "Service " + serviceAlias + " removed: index contained invalid data"
                          : "Service " + serviceAlias + " contains invalid data but could not be removed;"
                            " remove it manually with zypper";
    qCWarning(lcZypp) << QString::fromStdString(m_lastError);
    return false;
}

} // namespace qZypper
