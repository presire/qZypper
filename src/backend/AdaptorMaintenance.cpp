#include <QDBusError>
#include <QSet>
#include <QString>
#include <QVariantList>
#include <QVariantMap>
#include <string>
#include "PackageManagerAdaptor.h"
#include "ZyppHistory.h"
#include "ZyppLocks.h"
#include "ZyppManager.h"

namespace qZypper {

namespace {

/// 永続ロック操作のPolkitアクションID
constexpr char kManageLocksAction[] = "org.presire.qzypper.manage-locks";

/**
 * @brief GetPackagesByClassificationで許可する分類名の集合を返す
 * @return 許可された分類名の集合 (初回呼び出し時に生成する)
 */
const QSet<QString> &allowedClassifications()
{
    static const QSet<QString> values = {
        QStringLiteral("recommended"), QStringLiteral("suggested"),
        QStringLiteral("orphaned"), QStringLiteral("unneeded"),
        QStringLiteral("multiversion"), QStringLiteral("retracted"),
        QStringLiteral("retractedinstalled"), QStringLiteral("ptf"),
        QStringLiteral("locked"), QStringLiteral("updatable")
    };
    return values;
}

/**
 * @brief SolverResultをD-Bus転送用マップへ変換する
 * @param result ソルバー結果
 * @return success/problems/summaryを持つマップ
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

/**
 * @brief 永続パッケージロックの一覧を取得する
 * @return 各ロックをa{sv}へ変換したリスト
 */
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

/**
 * @brief 検証済みロックを認可後に追加する
 * @param lock 追加するロックの属性マップ
 * @return 追加要求を開始できた場合はtrue
 */
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

/**
 * @brief 検証済みロックを認可後に削除する
 * @param lock 削除するロックの属性マップ
 * @return 削除要求を開始できた場合はtrue
 */
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

/**
 * @brief 許可済み分類に属するパッケージを取得する
 * @param cls 問い合わせる分類名
 * @return パッケージ情報をa{sv}へ変換したリスト
 */
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

/**
 * @brief 検証済みページング条件で操作履歴を取得する
 * @param options 履歴の絞込みとページング条件
 * @return 履歴項目をa{sv}へ変換したリスト
 */
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
    // 上限超過などの失敗を空の履歴と区別できるよう、D-Busエラーとして返す
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

/**
 * @brief インストール済みシステムの依存関係を検証する
 * @return 成否と問題一覧を持つソルバー結果
 */
QVariantMap PackageManagerAdaptor::VerifySystem()
{
    return runOwnerSync([]() -> QVariant {
        return solverResultMap(ZyppManager::instance().verifySystem());
    }, true, true).toMap();
}

/**
 * @brief 現在のソルバー状態を再現できるテストケースを作成する
 * @return 作成したテストケースのパスまたは空文字列
 */
QString PackageManagerAdaptor::CreateSolverTestcase()
{
    return runOwnerSync([]() -> QVariant {
        return QString::fromStdString(ZyppManager::instance().createSolverTestcase());
    }, false, true).toString();
}

} // namespace qZypper
