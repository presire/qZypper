#include <algorithm>
#include <functional>
#include <string>
#include <vector>
#include <zypp/ZConfig.h>
#include <zypp-core/base/UserRequestException>
#include "ZyppManager.h"
#include "RepoPolicy.h"
#include "ZyppLogging.h"

namespace qZypper {

/**
 * @brief 既知リポジトリの一覧を取得する
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
 * @brief URLと名前を指定してリポジトリを追加する
 * @param url リポジトリURL
 * @param name リポジトリ名 (エイリアスとしても使用)
 * @return 追加成功時はtrue
 */
bool ZyppManager::addRepo(const std::string& url, const std::string& name)
{
    // NOTE: ロックは委譲先addRepo(const RepoInfo&)が取得する
    //       ここでロックすると、非再帰mutexの2重取得でデッドロックするため取得しない
    RepoInfo info;
    info.url         = QString::fromStdString(url);
    info.name        = QString::fromStdString(name);
    info.alias       = QString::fromStdString(name);
    info.enabled     = true;
    info.autoRefresh = true;
    return addRepo(info);
}

/**
 * @brief RepoInfoを指定してリポジトリを追加する
 *
 * リポジトリの追加、メタデータリフレッシュ、キャッシュ構築、キャッシュ読込を行う
 *
 * @param info リポジトリ情報
 * @return 追加成功時true
 */
bool ZyppManager::addRepo(const RepoInfo& info)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    // リポジトリ/プールの変更で、PoolItemを保持する既存の解決策は無効になる
    m_problems.clear();

    if (!m_repoManager) {
        m_lastError = "Backend not initialized";
        return false;
    }

    const std::string alias = info.alias.isEmpty() ? info.name.toStdString()
                                                   : info.alias.toStdString();
    const std::string name = info.name.toStdString();
    const std::string url = info.url.toStdString();

    std::string validationErr;
    if (!RepoPolicy::validateAlias(alias, validationErr)) {
        m_lastError = "Invalid alias: " + validationErr;
        return false;
    }

    if (!RepoPolicy::validateName(name, validationErr)) {
        m_lastError = "Invalid repository name: " + validationErr;
        return false;
    }

    if (!RepoPolicy::validateUrl(url, validationErr)) {
        m_lastError = "Invalid repository URL: " + validationErr;
        return false;
    }

    if (!RepoPolicy::validatePriority(info.priority, validationErr)) {
        m_lastError = "Invalid repository priority: " + validationErr;
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
        if (!RepoPolicy::checkRepoPolicy(repo, policyErr)) {
            m_lastError = "Repository violates policy: " + policyErr;
            return false;
        }

        m_repoManager->addRepository(repo);
        added = true;

        // ミラー端点はリフレッシュ直前に検査する (offlineのinitializeとは別扱い)
        std::string originsErr;
        if (!RepoPolicy::checkRepoOrigins(repo, originsErr)) {
            m_lastError = "Repository violates policy: " + originsErr;

            try {
                m_repoManager->removeRepository(repo);
                added = false;
            }
            catch (...) {
            }

            return false;
        }

        m_repoManager->refreshMetadata(repo);
        m_repoManager->buildCache(repo);
        m_repoManager->loadFromCache(repo);

        return true;
    }
    catch (const zypp::Exception& e) {
        std::string failure = "Failed to add repository: " + e.msg();
        if (added) {
            // 部分的に追加されたリポジトリをロールバックする (best-effort)
            try {
                m_repoManager->removeRepository(repo);
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
 * @brief リポジトリを削除する
 * @param alias 削除対象のリポジトリエイリアス
 * @return 削除成功時はtrue
 */
bool ZyppManager::removeRepo(const std::string& alias)
{
    std::lock_guard<std::mutex> lock(m_mutex);

    // リポジトリ/プールの変更で、PoolItemを保持する既存の解決策は無効になる
    m_problems.clear();

    if (!m_repoManager) {
        m_lastError = "Backend not initialized";
        return false;
    }

    try {
        zypp::RepoInfo repo = m_repoManager->getRepositoryInfo(alias);
        m_repoManager->removeRepository(repo);
        return true;
    }
    catch (const zypp::Exception& e) {
        m_lastError = "Failed to remove repository: " + e.msg();
        return false;
    }
}

/**
 * @brief リポジトリの有効/無効を切り替える
 * @param alias 対象のリポジトリエイリアス
 * @param enabled 有効にする場合true
 * @return 変更成功時はtrue
 */
bool ZyppManager::setRepoEnabled(const std::string& alias, bool enabled)
{
    std::lock_guard<std::mutex> lock(m_mutex);

    // リポジトリ/プールの変更で、PoolItemを保持する既存の解決策は無効になる
    m_problems.clear();

    if (!m_repoManager) {
        m_lastError = "Backend not initialized";
        return false;
    }

    try {
        zypp::RepoInfo repo = m_repoManager->getRepositoryInfo(alias);
        repo.setEnabled(enabled);
        m_repoManager->modifyRepository(alias, repo);
        return true;
    }
    catch (const zypp::Exception& e) {
        m_lastError = "Failed to modify repository: " + e.msg();
        return false;
    }
}

/**
 * @brief リポジトリのプロパティを変更する
 * @param alias 対象のリポジトリエイリアス
 * @param changes 変更する項目 (未設定の項目は現状維持)
 * @return 変更成功時はtrue
 */
bool ZyppManager::modifyRepo(const std::string& alias, const RepoChanges& changes)
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
        m_lastError = "Invalid repository name: " + validationErr;
        return false;
    }

    if (hasUrl && !RepoPolicy::validateUrl(changes.url->toStdString(), validationErr)) {
        m_lastError = "Invalid repository URL: " + validationErr;
        return false;
    }

    if (changes.priority && !RepoPolicy::validatePriority(*changes.priority, validationErr)) {
        m_lastError = "Invalid repository priority: " + validationErr;
        return false;
    }

    try {
        zypp::RepoInfo repo = m_repoManager->getRepositoryInfo(alias);

        // 渡された項目だけを適用し、それ以外は現在値を維持する
        if (hasName)
            repo.setName(changes.name->toStdString());

        if (hasUrl)
            repo.setBaseUrl(zypp::Url(changes.url->toStdString()));

        if (changes.enabled)
            repo.setEnabled(*changes.enabled);

        if (changes.autoRefresh)
            repo.setAutorefresh(*changes.autoRefresh);

        if (changes.priority)
            repo.setPriority(static_cast<unsigned>(*changes.priority));

        if (changes.keepPackages)
            repo.setKeepPackages(*changes.keepPackages);

        // 保存前に、変更後のRepoInfo全体 (ベースURL・GPG鍵URL・ミラーリスト・サービス由来の設定) をポリシー検査する
        // 違反時は.repoファイルを書き換えない
        //
        // checkRepoOriginsはミラーリスト取得 (ネットワークI/O) を伴い、ここではメディアに触れないため行わない
        //
        // 端点はrefreshRepo / refreshRepos / commitが使用直前に検査する
        std::string policyErr;
        if (!RepoPolicy::checkRepoPolicy(repo, policyErr)) {
            m_lastError = "Repository violates policy: " + policyErr;
            return false;
        }

        m_repoManager->modifyRepository(alias, repo);
        return true;
    }
    catch (const zypp::Exception& e) {
        m_lastError = "Failed to modify repository: " + e.msg();
        return false;
    }
}

/**
 * @brief 指定リポジトリをリフレッシュする
 *
 * メタデータの強制リフレッシュ、キャッシュ構築、キャッシュ読込を行う
 * (キャンセル可能)
 *
 * @param alias リフレッシュ対象のリポジトリエイリアス
 * @param progressCallback 進捗コールバック (エイリアス, パーセント)
 * @return リフレッシュ成功時はtrue
 */
bool ZyppManager::refreshRepo(const std::string& alias,
    std::function<void(const std::string&, int)> progressCallback)
{
    std::lock_guard<std::mutex> lock(m_mutex);

    // リポジトリ/プールの変更で、PoolItemを保持する既存の解決策は無効になる
    m_problems.clear();

    if (!m_repoManager) {
        m_lastError = "Backend not initialized";
        return false;
    }

    // libzypp操作中にキャンセル要求を検出するコールバック
    // falseを返すと、refreshMetadata/buildCache/loadFromCacheが中断される
    auto cancelCheck = [this](const zypp::ProgressData &) -> bool {
        return !m_cancelRequested.load();
    };

    try {
        zypp::RepoInfo repo = m_repoManager->getRepositoryInfo(alias);

        // メディア/キャッシュに触れる前にポリシー検査
        std::string policyErr;
        if (!RepoPolicy::checkRepoPolicy(repo, policyErr)) {
            m_lastError = "Repository violates policy: " + policyErr;
            return false;
        }

        // ミラー端点検査はリフレッシュ直前に行う (取得リストと使用分を束ねる)
        std::string originsErr;
        if (!RepoPolicy::checkRepoOrigins(repo, originsErr)) {
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
    }
    catch (const zypp::AbortRequestException& e) {
        // 取消し要求による中断だけを取消しとして扱う (他のABORTは失敗)
        if (m_cancelRequested.load())
            markCancelled();
        else
            m_lastError = "Operation aborted: " + e.msg();

        return false;
    }
    catch (const zypp::Exception& e) {
        m_lastError = "Failed to refresh repository: " + e.msg();
        return false;
    }
}

/**
 * @brief 全有効リポジトリをリフレッシュする
 *
 * 各リポジトリに対してメタデータリフレッシュ、キャッシュ構築、キャッシュ読込を行う
 * (キャンセル可能)
 *
 * @param progressCallback 進捗コールバック (エイリアス, パーセント)
 * @return リフレッシュ成功時true
 */
bool ZyppManager::refreshRepos(std::function<void(const std::string&, int)> progressCallback)
{
    std::lock_guard<std::mutex> lock(m_mutex);

    // リポジトリ/プールの変更で、PoolItemを保持する既存の解決策は無効になる
    m_problems.clear();

    if (!m_repoManager) {
        m_lastError = "Backend not initialized";
        return false;
    }

    // libzypp操作中にキャンセル要求を検出するコールバック
    // falseを返すと、refreshMetadata/buildCache/loadFromCacheが中断される
    auto cancelCheck = [this](const zypp::ProgressData &) -> bool {
        return !m_cancelRequested.load();
    };

    try {
        auto repos = m_repoManager->knownRepositories();
        int total = static_cast<int>(repos.size());
        int current = 0;
        bool skippedViolation = false;
        std::string skippedDetail;
        std::vector<std::string> failedDetail;

        for (const auto& repo : repos) {
            if (m_cancelRequested) {
                markCancelled();
                return false;
            }

            if (!repo.enabled()) {
                ++current;
                continue;
            }

            // ポリシー違反リポジトリは飛ばして他を続行する
            std::string policyErr;
            if (!RepoPolicy::checkRepoPolicy(repo, policyErr)) {
                skippedViolation = true;

                if (!skippedDetail.empty())
                    skippedDetail += "; ";

                skippedDetail += policyErr;
                qCWarning(lcZypp) << "Skipping repo during refreshRepos:" << QString::fromStdString(policyErr);
                ++current;
                continue;
            }

            // ミラー端点違反も同様に飛ばす (ネットワーク取得を伴うため直前検査)
            std::string originsErr;
            if (!RepoPolicy::checkRepoOrigins(repo, originsErr)) {
                skippedViolation = true;

                if (!skippedDetail.empty())
                    skippedDetail += "; ";

                skippedDetail += originsErr;
                ++current;
                continue;
            }

            if (progressCallback)
                progressCallback(repo.alias(), (current * 100) / total);

            // 1件の失敗 (到達不能・メタデータ不正等) で残りの更新を止めない
            // 取消しによる中断 (AbortRequestException) は外側で処理する
            try {
                m_repoManager->refreshMetadata(repo, zypp::RepoManager::RefreshForced, cancelCheck);
                m_repoManager->buildCache(repo, zypp::RepoManager::BuildIfNeeded, cancelCheck);
                m_repoManager->loadFromCache(repo, cancelCheck);
            }
            catch (const zypp::AbortRequestException&) {
                if (m_cancelRequested.load())
                    throw;

                failedDetail.push_back(repo.alias() + ": operation aborted");
            }
            catch (const zypp::Exception& e) {
                failedDetail.push_back(repo.alias() + ": " + e.msg());
                qCWarning(lcZypp) << "Failed to refresh repo" << QString::fromStdString(repo.alias()) << ":" << QString::fromStdString(e.msg());
            }

            ++current;
        }

        if (progressCallback)
            progressCallback("", 100);

        std::string error;
        if (!failedDetail.empty()) {
            error = "Failed to refresh repositories: ";
            for (std::size_t i = 0; i < failedDetail.size(); ++i)
                error += (i ? "; " : "") + failedDetail[i];
        }

        if (skippedViolation) {
            if (!error.empty())
                error += "; ";

            error += "Skipped repositories with policy violations: " + skippedDetail;
        }

        if (!error.empty()) {
            m_lastError = error;
            return false;
        }

        return true;
    }
    catch (const zypp::AbortRequestException& e) {
        // 取消し要求による中断だけを取消しとして扱う (他のABORTは失敗)
        if (m_cancelRequested.load())
            markCancelled();
        else
            m_lastError = "Operation aborted: " + e.msg();

        return false;
    }
    catch (const zypp::Exception& e) {
        m_lastError = "Failed to refresh repositories: " + e.msg();
        return false;
    }
}

} // namespace qZypper
