#include "PackageManagerAdaptor.h"

#include "ZyppHistory.h"
#include "ZyppLocks.h"
#include "ZyppManager.h"

#include <QDBusError>
#include <QSet>
#include <QString>
#include <QVariantList>
#include <QVariantMap>

#include <string>

namespace qZypper {

namespace {

/// 永続ロック操作の Polkit アクション ID。
constexpr char kManageLocksAction[] = "org.presire.qzypper.manage-locks";

/// GetPackagesByClassification で許可する分類名。
const QSet<QString> &allowedClassifications()
{
    static const QSet<QString> values = {
        QStringLiteral("recommended"), QStringLiteral("suggested"),
        QStringLiteral("orphaned"), QStringLiteral("unneeded"),
        QStringLiteral("multiversion"), QStringLiteral("retracted"),
        QStringLiteral("retractedinstalled"), QStringLiteral("ptf"),
        QStringLiteral("locked")
    };
    return values;
}

/**
 * @brief SolverResult を D-Bus 転送用マップへ変換する。
 * @param result ソルバー結果
 * @return success/problems/summary を持つマップ
 */
QVariantMap solverResultMap(const ZyppManager::SolverResult &result)
{
    QVariantList problems;
    for (const ConflictInfo &problem : result.problems) {
        problems.append(problem.toVariantMap());
    }
    QVariantMap map;
    map.insert(QStringLiteral("success"), result.success);
    map.insert(QStringLiteral("problems"), problems);
    map.insert(QStringLiteral("summary"), result.summary);
    return map;
}

} // namespace

QVariantList PackageManagerAdaptor::GetLocks()
{
    return runRead([]() -> QVariant {
        const std::vector<LockInfo> locks = ZyppManager::instance().getLocks();
        QVariantList list;
        list.reserve(static_cast<int>(locks.size()));
        for (const LockInfo &info : locks) {
            list.append(info.toVariantMap());
        }
        return list;
    }).toList();
}

bool PackageManagerAdaptor::AddLock(const QVariantMap &lock)
{
    LockInfo info;
    std::string error;
    if (!validateLockMap(lock, info, error)) {
        if (checkRead()) {
            sendErrorReply(QDBusError::InvalidArgs, QString::fromStdString(error));
        }
        return false;
    }
    return runPrivilegedBool(QString::fromLatin1(kManageLocksAction),
                             [info]() -> bool {
        return ZyppManager::instance().addLock(info);
    });
}

bool PackageManagerAdaptor::RemoveLock(const QVariantMap &lock)
{
    LockInfo info;
    std::string error;
    if (!validateLockMap(lock, info, error)) {
        if (checkRead()) {
            sendErrorReply(QDBusError::InvalidArgs, QString::fromStdString(error));
        }
        return false;
    }
    return runPrivilegedBool(QString::fromLatin1(kManageLocksAction),
                             [info]() -> bool {
        return ZyppManager::instance().removeLock(info);
    });
}

QVariantList PackageManagerAdaptor::GetPackagesByClassification(const QString &cls)
{
    const QString norm = cls.trimmed().toLower();
    if (!allowedClassifications().contains(norm)) {
        if (checkRead()) {
            sendErrorReply(QDBusError::InvalidArgs,
                           QStringLiteral("unknown classification: %1").arg(cls));
        }
        return {};
    }
    const std::string classification = norm.toStdString();
    return runRead([classification]() -> QVariant {
        const std::vector<PackageInfo> packages =
            ZyppManager::instance().getPackagesByClassification(classification);
        QVariantList list;
        list.reserve(static_cast<int>(packages.size()));
        for (const PackageInfo &info : packages) {
            list.append(info.toVariantMap());
        }
        return list;
    }).toList();
}

QVariantList PackageManagerAdaptor::GetHistory(const QVariantMap &options)
{
    HistoryOptions parsed;
    std::string error;
    if (!parseHistoryOptions(options, parsed, error)) {
        if (checkRead()) {
            sendErrorReply(QDBusError::InvalidArgs, QString::fromStdString(error));
        }
        return {};
    }
    // 上限超過などの失敗を空の履歴と区別できるよう、D-Bus エラーとして返す。
    if (!checkRead()) return {};
    startWorker(message(), [parsed] {
        AsyncOutcome result;
        std::vector<HistoryEntry> entries;
        std::string historyError;
        if (!ZyppManager::instance().getHistory(parsed, entries, historyError)) {
            const bool cancelled = historyError == "Operation cancelled";
            result.errorName = cancelled ? QStringLiteral("org.presire.qzypper.Error.Cancelled")
                                         : QStringLiteral("org.freedesktop.DBus.Error.Failed");
            result.errorText = QString::fromStdString(historyError);
            return result;
        }
        QVariantList list;
        list.reserve(static_cast<int>(entries.size()));
        for (const HistoryEntry &entry : entries) {
            list.append(entry.toVariantMap());
        }
        result.value = list;
        return result;
    });
    return {};
}

QVariantMap PackageManagerAdaptor::VerifySystem()
{
    return runOwnerSync([]() -> QVariant {
        return solverResultMap(ZyppManager::instance().verifySystem());
    }, true, true).toMap();
}

QString PackageManagerAdaptor::CreateSolverTestcase()
{
    return runOwnerSync([]() -> QVariant {
        return QString::fromStdString(ZyppManager::instance().createSolverTestcase());
    }, false, true).toString();
}

} // namespace qZypper
