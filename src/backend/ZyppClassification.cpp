#include "ZyppManager.h"

#include <zypp/Package.h>
#include <zypp/PoolItem.h>
#include <zypp/ResPool.h>
#include <zypp/ResPoolProxy.h>
#include <zypp/ResStatus.h>
#include <zypp/Resolver.h>
#include <zypp/ui/Selectable.h>

#include <string>
#include <vector>

namespace qZypper {

namespace {

/**
 * @brief Selectable が指定の分類に該当するか判定する。
 *
 * recommended/suggested/orphaned/unneeded はソルバー実行後の
 * PoolItem の ResStatus を参照する。それ以外は Selectable の
 * 補助述語のみで判定する。
 *
 * @param selectable 判定対象
 * @param cls 分類名 (小文字)
 * @return 該当する場合は true
 */
bool matchesClassification(const zypp::ui::Selectable::Ptr &selectable, const std::string &cls)
{
    if (cls == "multiversion") {
        return selectable->multiversionInstall();
    }
    if (cls == "locked") {
        return selectable->status() == zypp::ui::S_Taboo;
    }
    if (cls == "retracted") {
        return selectable->hasRetracted();
    }
    if (cls == "retractedInstalled") {
        return selectable->hasRetractedInstalled();
    }
    if (cls == "ptf") {
        return selectable->hasPtf() || selectable->hasPtfInstalled();
    }

    for (const zypp::PoolItem &item : selectable->picklist()) {
        const zypp::ResStatus &status = item.status();
        if (cls == "recommended" && status.isRecommended()) {
            return true;
        }
        if (cls == "suggested" && status.isSuggested()) {
            return true;
        }
        if (cls == "orphaned" && status.isOrphaned()) {
            return true;
        }
        if (cls == "unneeded" && status.isUnneeded()) {
            return true;
        }
    }
    return false;
}

} // namespace

std::vector<PackageInfo> ZyppManager::getPackagesByClassification(const std::string &cls) const
{
    std::lock_guard<std::mutex> guard(m_mutex);
    std::vector<PackageInfo> result;
    if (!m_initialized) {
        m_lastError = "libzypp not initialized";
        return result;
    }

    const bool needsSolver = (cls == "recommended" || cls == "suggested"
                              || cls == "orphaned" || cls == "unneeded");
    if (needsSolver) {
        try {
            m_zypp->resolver()->resolvePool();
        } catch (const zypp::Exception &e) {
            m_lastError = e.asString();
        }
    }

    const auto pool = m_zypp->poolProxy();
    for (auto it = pool.byKindBegin<zypp::Package>(); it != pool.byKindEnd<zypp::Package>(); ++it) {
        const zypp::ui::Selectable::Ptr &selectable = *it;
        if (matchesClassification(selectable, cls)) {
            result.push_back(makePackageInfo(selectable));
        }
    }
    return result;
}

} // namespace qZypper
