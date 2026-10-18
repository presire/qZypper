#include "ZyppManager.h"
#include <zypp/target/rpm/RpmDb.h>
#include <sys/stat.h>

namespace qZypper {
namespace {
/** @brief RPM DB本体とSQLite WALの高精度タイムスタンプを比較する。 */
std::string databaseStamp(zypp::Target &target)
{
    const auto directory = target.assertRootPrefix(target.rpmDb().dbPath());
    std::string stamp;
    for (const char *name : {"rpmdb.sqlite", "rpmdb.sqlite-wal", "Packages", "Packages.db", "data.mdb"}) {
        struct stat info {};
        const auto path = directory / name;
        if (::stat(path.c_str(), &info) == 0) {
            stamp += std::string(name) + ':' + std::to_string(info.st_ino) + ':'
                + std::to_string(info.st_size) + ':' + std::to_string(info.st_mtim.tv_sec)
                + ':' + std::to_string(info.st_mtim.tv_nsec) + ';';
        }
    }
    return stamp;
}
}

/** @brief 初回読込時のDB世代を記録する。呼出側がマネージャをロックする。 */
void ZyppManager::recordDatabaseStamp()
{
    m_rpmStamp = databaseStamp(*m_zypp->target());
}

/** @brief 初期化済みセッションを再利用する直前に外部変更を検出する。 */
bool ZyppManager::reloadIfChanged()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_initialized)
        return false;
    const auto stamp = databaseStamp(*m_zypp->target());
    if (m_rpmStamp.empty()) {
        m_rpmStamp = stamp;
        return false;
    }
    if (stamp == m_rpmStamp)
        return false;
    m_problems.clear();
    m_zypp->resolver()->undo();
    m_zypp->target()->reload();
    m_rpmStamp = databaseStamp(*m_zypp->target());
    return true;
}

} // namespace qZypper
