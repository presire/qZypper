#include <QDBusError>
#include <QMetaType>
#include "PackageManagerAdaptor.h"
#include "SearchOptions.h"
#include "ZyppManager.h"

namespace qZypper {
    namespace {
        /** @brief SearchFlagに定義された全ビットの和 */
        constexpr int kMaxSearchFlags = 127;

        /**
        * @brief PackageInfo等のベクタをQVariantListへ変換する
        * @param items toVariantMap()を持つ要素のベクタ
        * @return QVariantList
        */
        template<typename T>
        QVariantList maps(const std::vector<T> &items)
        {
            QVariantList list;
            for (const auto &item : items)
                list.append(item.toVariantMap());
            return list;
        }

        /**
        * @brief SolverResultを既存のマップ形式へ変換する
        *
        * AdaptorSelectionと同じ形状 (success, problems, summary) を返す
        *
        * @param solver ソルバ結果
        * @return 結果マップ
        */
        QVariant solverMap(const ZyppManager::SolverResult &solver)
        {
            QVariantList problems;
            for (const auto &problem : solver.problems)
                problems.append(problem.toVariantMap());
            return QVariantMap{{"success", solver.success}, {"problems", problems}, {"summary", solver.summary}};
        }

    } // namespace

    /** @brief 拡張検索オプションを厳格に検証し、PoolQuery検索を実行する */
    QVariantList PackageManagerAdaptor::SearchPackagesEx(const QVariantMap &options)
    {
        // 正規表現のコンパイル検証は主スレッドで行うため、所有者確認を先に済ませる
        if (!checkRead())
            return {};

        SearchOptions parsed;
        QString error;
        if (!parseSearchOptions(options, parsed, error)) {
            sendErrorReply(QDBusError::InvalidArgs, error);
            return {};
        }

        return runRead([parsed] {
            return QVariant(maps(ZyppManager::instance().searchPackagesEx(parsed)));
        }).toList();
    }

    /** @brief ディストリビューションアップグレード (dup) を実行する */
    QVariantMap PackageManagerAdaptor::DistUpgrade(const QVariantMap &options)
    {
        bool allowVendorChange = false;
        for (auto it = options.constBegin(); it != options.constEnd(); ++it) {
            if (it.key() == QStringLiteral("allowVendorChange")) {
                if (it.value().typeId() != QMetaType::Bool) {
                    if (checkNotBusy() && checkOwner())
                        sendErrorReply(QDBusError::InvalidArgs,
                                       QStringLiteral("Option 'allowVendorChange' must be a boolean"));
                    return {};
                }
                allowVendorChange = it.value().toBool();
            }
            else {
                if (checkNotBusy() && checkOwner())
                    sendErrorReply(QDBusError::InvalidArgs,
                                   QStringLiteral("Unknown distribution upgrade option: %1").arg(it.key()));
                return {};
            }
        }

        return runOwnerSync([allowVendorChange] {
            return solverMap(ZyppManager::instance().distUpgrade(allowVendorChange));
        }, true, true).toMap();
    }

    /** @brief 列挙値を検証して、パッチ選択状態を変更する */
    bool PackageManagerAdaptor::SetPatchStatus(const QString &id, int status)
    {
        if (!ZyppManager::isValidPackageStatus(status)) {
            if (checkNotBusy() && checkOwner())
                sendErrorReply(QDBusError::InvalidArgs, QStringLiteral("Invalid patch status"));
            return false;
        }

        return runOwnerSync([id, status] {
            return QVariant(ZyppManager::instance().setPatchStatus(id.toStdString(), status));
        }).toBool();
    }

    /** @brief 必要なパッチを一括選択して、依存解決する */
    QVariantMap PackageManagerAdaptor::SelectAllNeededPatches(const QVariantMap &options)
    {
        bool securityOnly = false;
        bool includeOptional = false;
        bool skipInteractive = true;

        for (auto it = options.constBegin(); it != options.constEnd(); ++it) {
            if (it.value().typeId() != QMetaType::Bool) {
                if (checkNotBusy() && checkOwner())
                    sendErrorReply(QDBusError::InvalidArgs, QStringLiteral("Patch selection options must be booleans"));
                return {};
            }

            if (it.key() == QStringLiteral("securityOnly")) {
                securityOnly = it.value().toBool();
            }
            else if (it.key() == QStringLiteral("includeOptional")) {
                includeOptional = it.value().toBool();
            }
            else if (it.key() == QStringLiteral("skipInteractive")) {
                skipInteractive = it.value().toBool();
            }
            else {
                if (checkNotBusy() && checkOwner())
                    sendErrorReply(QDBusError::InvalidArgs, QStringLiteral("Unknown patch selection option: %1").arg(it.key()));
                return {};
            }
        }

        return runOwnerSync([securityOnly, includeOptional, skipInteractive] {
            return solverMap(ZyppManager::instance().selectAllNeededPatches(
                securityOnly, includeOptional, skipInteractive));
        }, true, true).toMap();
    }

    /** @brief 現在のプール状態のリスクサマリを取得する */
    QVariantMap PackageManagerAdaptor::GetTransactionSummary()
    {
        return runRead([] {
            return QVariant(ZyppManager::instance().getTransactionSummary());
        }).toMap();
    }
} // namespace qZypper
