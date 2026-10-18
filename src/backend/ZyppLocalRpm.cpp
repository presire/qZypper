#include "ZyppManager.h"
#include "LocalRpm.h"

#include <QDebug>

#include <zypp/Pathname.h>
#include <zypp/PoolQuery.h>
#include <zypp/ResKind.h>
#include <zypp/RepoInfo.h>
#include <zypp/RepoManager.h>
#include <zypp/Url.h>
#include <zypp/repo/RepoType.h>
#include <zypp/sat/Pool.h>
#include <zypp/target/rpm/RpmHeader.h>
#include <zypp/ui/Selectable.h>

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <filesystem>
#include <random>
#include <sstream>
#include <system_error>
#include <vector>

namespace qZypper {
namespace {

/** @brief ローカル RPM 一時リポジトリを配置するルートディレクトリ。 */
constexpr const char *kLocalRpmParent = "/var/cache/qzypper";
constexpr const char *kLocalRpmRoot = "/var/cache/qzypper/local-rpms";

/** @brief 衝突しない 16 進ランダムトークンを生成する。 */
std::string randomToken()
{
    std::random_device device;
    std::mt19937_64 engine(device());
    std::uniform_int_distribution<std::uint64_t> dist(0, 0xffffffffffffffffULL);
    std::ostringstream out;
    out << std::hex << dist(engine);
    return out.str();
}

/**
 * @brief 指定モードでディレクトリを作成する。
 *
 * 既存の場合は symlink でない root 所有のディレクトリで、group/other 書込み不可であることを要求する
 * (他ユーザーが差し替えた場所へ root が RPM を配置しないため)。
 */
bool ensureDirectory(const std::string &path, mode_t mode)
{
    if (::mkdir(path.c_str(), mode) != 0 && errno != EEXIST)
        return false;
    struct stat st {};
    return ::lstat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode) && st.st_uid == 0
           && (st.st_mode & (S_IWGRP | S_IWOTH)) == 0;
}

} // namespace

/**
 * @brief 署名鍵プロンプト用コールバックを設定する。
 * @param cb コールバック (解除は空の std::function)
 */
void ZyppManager::setKeyTrustPromptCallback(KeyTrustPromptFn cb)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_keyRingReceiver.setKeyTrustPromptCallback(std::move(cb));
}

/**
 * @brief 未署名ファイル・検証失敗プロンプト用コールバックを設定する。
 * @param cb コールバック (解除は空の std::function)
 */
void ZyppManager::setUnsignedFilePromptCallback(UnsignedFilePromptFn cb)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_keyRingReceiver.setUnsignedFilePromptCallback(std::move(cb));
}

/**
 * @brief 一時ローカルリポジトリとディレクトリを破棄する (排他取得あり)。
 */
void ZyppManager::cleanupLocalRpms()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    cleanupLocalRpmsLocked();
}

/**
 * @brief 一時ローカルリポジトリとディレクトリを破棄する (m_mutex 保持前提)。
 */
void ZyppManager::cleanupLocalRpmsLocked()
{
    if (!m_localRpmAlias.empty()) {
        try {
            zypp::sat::Pool::instance().reposErase(m_localRpmAlias);
        } catch (const std::exception &e) {
            qWarning() << "Failed to erase local RPM repo from pool:" << e.what();
        }
        if (m_repoManager) {
            try {
                zypp::RepoInfo info;
                info.setAlias(m_localRpmAlias);
                m_repoManager->cleanCache(info);
            } catch (const std::exception &e) {
                qWarning() << "Failed to clean local RPM repo cache:" << e.what();
            }
        }
        m_localRpmAlias.clear();
    }

    if (!m_localRpmDir.empty()) {
        std::error_code ec;
        std::filesystem::remove_all(m_localRpmDir, ec);
        if (ec) {
            qWarning() << "Failed to remove local RPM directory:"
                       << QString::fromStdString(ec.message());
        }
        m_localRpmDir.clear();
    }
}

/**
 * @brief ローカル RPM 群を一時 plaindir リポジトリとして導入し解決する。
 *
 * 検証済みの各ファイルを root 専用一時ディレクトリへ TOCTOU 安全に複製し、
 * プールへ読み込んだうえで S_Install を選択してソルバーを実行する。
 * リポジトリは /etc/zypp/repos.d へは永続化しない。
 * @param paths 呼び出し元が指定した絶対パス群
 * @param callerUid D-Bus 呼び出し元の uid
 * @return 解決結果 (success, problems, summary, packages, unsignedPackages)
 */
ZyppManager::LocalRpmInstallResult ZyppManager::installLocalRpms(std::vector<LocalRpmFile> files)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    LocalRpmInstallResult result;
    result.success = false;
    m_problems.clear();

    if (!m_initialized || !m_zypp || !m_repoManager) {
        m_lastError = "Backend not initialized";
        return result;
    }
    // 記述子は呼び出し側 (D-Bus アダプタ) で adoptLocalRpmFd により検証済み。
    if (files.empty()) {
        m_lastError = "No RPM files given";
        return result;
    }
    if (files.size() > kMaxLocalRpmCount) {
        m_lastError = "Too many RPM files (limit " + std::to_string(kMaxLocalRpmCount) + ")";
        return result;
    }
    for (std::size_t i = 0; i < files.size(); ++i) {
        for (std::size_t j = i + 1; j < files.size(); ++j) {
            if (files[i].filename == files[j].filename) {
                m_lastError = "Duplicate file name: " + files[i].filename;
                return result;
            }
        }
    }

    // 前回の一時リポジトリ/ディレクトリを先に破棄する。
    cleanupLocalRpmsLocked();

    const std::string alias = "qzypper-local-" + randomToken();
    const std::string dir = std::string(kLocalRpmRoot) + "/" + alias;
    std::string ioError;

    if (!ensureDirectory(kLocalRpmParent, 0755) || !ensureDirectory(kLocalRpmRoot, 0700)
        || !ensureDirectory(dir, 0700)) {
        m_lastError = "Failed to create local RPM directory";
        return result;
    }
    m_localRpmAlias = alias;
    m_localRpmDir = dir;

    for (const LocalRpmFile &file : files) {
        const std::string dest = dir + "/" + file.filename;
        if (!copyLocalRpmTo(file, dest, kMaxLocalRpmBytes, ioError)) {
            m_lastError = "Failed to stage '" + file.path + "': " + ioError;
            cleanupLocalRpmsLocked();
            return result;
        }
    }

    try {
        zypp::RepoInfo repo;
        repo.setAlias(alias);
        repo.setName(alias);
        repo.setEnabled(true);
        repo.setAutorefresh(false);
        repo.setType(zypp::repo::RepoType::RPMPLAINDIR);
        repo.setBaseUrl(zypp::Url("dir:" + dir));

        m_repoManager->buildCache(repo);
        m_repoManager->loadFromCache(repo);
    } catch (const zypp::Exception &e) {
        m_lastError = "Failed to load local repository: " + e.asString();
        cleanupLocalRpmsLocked();
        return result;
    }

    try {
        zypp::PoolQuery query;
        query.addRepo(alias);
        query.addKind(zypp::ResKind::package);

        for (auto it = query.begin(); it != query.end(); ++it) {
            const zypp::sat::Solvable &item = *it;
            result.packages.push_back(item.name());

            // Selectable::get() は同名グループを返すだけなので、setStatus(S_Install) では
            // 他リポジトリの候補が選ばれ得る。ローカルの PoolItem を明示的に候補として導入指定する。
            const zypp::PoolItem poolItem(item);
            zypp::ui::Selectable::Ptr selectable = zypp::ui::Selectable::get(item);
            if (!selectable) {
                m_lastError = "Failed to select " + item.name() + " for installation";
                cleanupLocalRpmsLocked();
                return result;
            }
            // 同一内容が既に導入済みなら変更不要 (正常な no-op)。それ以外は指定 PoolItem の
            // 導入選択が実際に行われたことを検証する。
            if (selectable->identicalInstalled(poolItem))
                continue;
            if (!selectable->setOnSystem(poolItem, zypp::ResStatus::USER)
                || !poolItem.status().isToBeInstalled()) {
                m_lastError = "Failed to select " + item.name() + " for installation";
                cleanupLocalRpmsLocked();
                return result;
            }
        }
    } catch (const zypp::Exception &e) {
        m_lastError = "Failed to select local packages: " + e.asString();
        cleanupLocalRpmsLocked();
        return result;
    }

    if (result.packages.empty()) {
        m_lastError = "No packages found in the supplied RPM files";
        cleanupLocalRpmsLocked();
        return result;
    }

    // 未署名 RPM の検出 (zypper 同様に許可するが GUI へ警告材料として返す)。
    for (const LocalRpmFile &file : files) {
        try {
            const zypp::target::rpm::RpmHeader::constPtr header =
                zypp::target::rpm::RpmHeader::readPackage(
                    zypp::Pathname(dir + "/" + file.filename), zypp::target::rpm::RpmHeader::NOVERIFY);
            if (!header || header->signatureKeyID().empty())
                result.unsignedPackages.push_back(file.filename);
        } catch (const std::exception &) {
            result.unsignedPackages.push_back(file.filename);
        }
    }

    try {
        auto resolver = m_zypp->resolver();
        resolver->setDefaultSolverFlags();
        resolver->setIgnoreAlreadyRecommended(true);
        result.success = resolver->resolvePool();
    } catch (const zypp::Exception &e) {
        m_lastError = "Solver error: " + e.asString();
        result.success = false;
    } catch (const std::exception &e) {
        m_lastError = std::string("Solver error: ") + e.what();
        result.success = false;
    }

    if (!result.success) {
        try {
            m_problems = m_zypp->resolver()->problems();
            for (const auto &problem : m_problems) {
                ConflictInfo conflict;
                conflict.description = QString::fromStdString(problem->description());
                conflict.details     = QString::fromStdString(problem->details());
                for (const auto &sol : problem->solutions()) {
                    QVariantMap solMap;
                    solMap["description"] = QString::fromStdString(sol->description());
                    solMap["details"]     = QString::fromStdString(sol->details());
                    conflict.solutions.append(solMap);
                }
                result.problems.push_back(std::move(conflict));
            }
        } catch (const std::exception &e) {
            qWarning() << "Failed to get local RPM solver problems:" << e.what();
        }
    }

    result.summary = computeTransactionSummaryLocked();
    return result;
}

} // namespace qZypper
