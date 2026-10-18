#include "TransactionSummary.h"

#include "ZyppManager.h"

#include <zypp/Edition.h>
#include <zypp/PoolItem.h>

#include <QVariantList>

namespace qZypper {

ChangeRisk classifyChange(const ChangeTuple &tuple, const VersionCompareFn &cmp)
{
    ChangeRisk risk;

    if (!tuple.hasOld && tuple.hasNew) {
        risk.install = true;
    } else if (tuple.hasOld && !tuple.hasNew) {
        risk.remove = true;
    } else if (tuple.hasOld && tuple.hasNew) {
        const int comparison = cmp(tuple.oldVersion, tuple.newVersion);
        if (comparison > 0)
            risk.downgrade = true;
        else
            risk.update = true;

        risk.vendorChange = (tuple.oldVendor != tuple.newVendor);
        risk.archChange = (tuple.oldArch != tuple.newArch);
    }

    return risk;
}

void accumulateChange(TransactionTally &tally, const ChangeEntry &entry)
{
    if (entry.risk.remove) {
        ++tally.removeCount;
        tally.removals.push_back(entry);
    } else if (entry.risk.downgrade) {
        ++tally.downgradeCount;
        tally.downgrades.push_back(entry);
    } else if (entry.risk.install) {
        ++tally.installCount;
    } else if (entry.risk.update) {
        ++tally.updateCount;
    }

    if (entry.risk.vendorChange) {
        ++tally.vendorChangeCount;
        tally.vendorChanges.push_back(entry);
    }

    if (entry.risk.archChange) {
        ++tally.archChangeCount;
        tally.archChanges.push_back(entry);
    }
}

QVariantMap changeEntryToVariantMap(const ChangeEntry &entry)
{
    return {
        {"name",       entry.name},
        {"oldVersion", entry.oldVersion},
        {"newVersion", entry.newVersion},
        {"oldVendor",  entry.oldVendor},
        {"newVendor",  entry.newVendor},
        {"oldArch",    entry.oldArch},
        {"newArch",    entry.newArch},
        {"repoAlias",  entry.repoAlias}
    };
}

QVariantMap tallyToVariantMap(const TransactionTally &tally)
{
    auto toList = [](const std::vector<ChangeEntry> &entries) {
        QVariantList list;
        for (const auto &entry : entries)
            list.append(changeEntryToVariantMap(entry));
        return list;
    };

    return {
        {"installCount",        tally.installCount},
        {"updateCount",         tally.updateCount},
        {"removeCount",         tally.removeCount},
        {"downgradeCount",      tally.downgradeCount},
        {"vendorChangeCount",   tally.vendorChangeCount},
        {"archChangeCount",     tally.archChangeCount},
        {"downloadSize",        tally.downloadSize},
        {"installedSizeChange", tally.installedSizeChange},
        {"removals",            toList(tally.removals)},
        {"downgrades",          toList(tally.downgrades)},
        {"vendorChanges",       toList(tally.vendorChanges)},
        {"archChanges",         toList(tally.archChanges)}
    };
}

/**
 * @brief 現在のプール状態からトランザクションのリスクサマリを計算する。
 *
 * 導入予定/削除予定の Selectable を走査し、新規・更新・削除・ダウングレード・
 * ベンダ変更・アーキテクチャ変更を集計する。ResolveDependencies、
 * UpdateAllPackages、DistUpgrade、SelectAllNeededPatches、Commit の各結果に
 * 添付するサマリと同じ形状を返す。
 * @return サマリの a{sv}
 */
QVariantMap ZyppManager::computeTransactionSummary() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return computeTransactionSummaryLocked();
}

/**
 * @brief 現在のプール状態からトランザクションのリスクサマリを計算する (m_mutex 保持中)。
 * @return サマリの a{sv}
 */
QVariantMap ZyppManager::computeTransactionSummaryLocked() const
{
    TransactionTally tally;

    if (!m_initialized)
        return tallyToVariantMap(tally);

    const VersionCompareFn compare = [](const QString &oldVersion, const QString &newVersion) {
        return zypp::Edition::Compare()(zypp::Edition(oldVersion.toStdString()),
                                        zypp::Edition(newVersion.toStdString()));
    };

    try {
        auto pool = m_zypp->poolProxy();
        for (auto it = pool.byKindBegin<zypp::Package>();
             it != pool.byKindEnd<zypp::Package>(); ++it) {

            const zypp::ui::Selectable::Ptr &sel = *it;
            if (!sel)
                continue;

            const zypp::ui::Status status = sel->status();
            const bool wantInstall = status == zypp::ui::S_Install
                                     || status == zypp::ui::S_AutoInstall
                                     || status == zypp::ui::S_Update
                                     || status == zypp::ui::S_AutoUpdate;
            const bool wantRemove = status == zypp::ui::S_Del
                                    || status == zypp::ui::S_AutoDel;
            if (!wantInstall && !wantRemove)
                continue;

            const zypp::PoolItem oldItem = sel->installedObj();
            zypp::PoolItem newItem;
            if (wantInstall)
                newItem = sel->candidateObj();
            if (!oldItem && !newItem)
                continue;

            ChangeTuple tuple;
            tuple.hasOld = static_cast<bool>(oldItem);
            tuple.hasNew = static_cast<bool>(newItem);
            if (oldItem) {
                tuple.oldVersion = QString::fromStdString(oldItem.edition().asString());
                tuple.oldVendor  = QString::fromStdString(oldItem.vendor().asString());
                tuple.oldArch    = QString::fromStdString(oldItem.arch().asString());
            }
            if (newItem) {
                tuple.newVersion = QString::fromStdString(newItem.edition().asString());
                tuple.newVendor  = QString::fromStdString(newItem.vendor().asString());
                tuple.newArch    = QString::fromStdString(newItem.arch().asString());
            }

            ChangeEntry entry;
            entry.name      = QString::fromStdString(sel->name());
            entry.oldVersion = tuple.oldVersion;
            entry.newVersion = tuple.newVersion;
            entry.oldVendor = tuple.oldVendor;
            entry.newVendor = tuple.newVendor;
            entry.oldArch   = tuple.oldArch;
            entry.newArch   = tuple.newArch;
            entry.repoAlias = newItem
                                  ? QString::fromStdString(newItem.repoInfo().alias())
                                  : QString::fromStdString(oldItem.repoInfo().alias());
            entry.risk      = classifyChange(tuple, compare);

            qlonglong download = 0;
            if (newItem)
                download = static_cast<qlonglong>(newItem.downloadSize());
            qlonglong newSize = 0;
            if (newItem)
                newSize = static_cast<qlonglong>(newItem.installSize());
            qlonglong oldSize = 0;
            if (oldItem)
                oldSize = static_cast<qlonglong>(oldItem.installSize());

            tally.downloadSize += download;
            tally.installedSizeChange += newSize - oldSize;

            accumulateChange(tally, entry);
        }
    } catch (const zypp::Exception &e) {
        m_lastError = "Transaction summary error: " + e.asString();
        return {};   // 途中集計を返すと GUI が「危険な変更なし」と誤判定するため空を返す (fail-closed)
    } catch (const std::exception &e) {
        m_lastError = std::string("Transaction summary error: ") + e.what();
        return {};
    }

    return tallyToVariantMap(tally);
}

/**
 * @brief 現在のプール状態のリスクサマリを取得する (D-Bus 公開用)。
 * @return サマリの a{sv}
 */
QVariantMap ZyppManager::getTransactionSummary()
{
    return computeTransactionSummary();
}

} // namespace qZypper
