#include <QDBusError>
#include <QMetaType>
#include "PackageManagerAdaptor.h"
#include "RepoPolicy.h"
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
        * @brief 検索オプションに従う検索を実行する
        * @param options 検証済みの検索オプション
        * @return 検索結果 (旧形式とV2で共有)
        */
        std::vector<PackageInfo> exSearchResults(const SearchOptions &options)
        {
            return ZyppManager::instance().searchPackagesEx(options);
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

    /**
     * @brief エイリアスを検証して、所有者のリポジトリ切り替え要求を保持する
     * @param alias 切り替え対象のリポジトリエイリアス
     * @param enabled trueで切り替え対象に追加、falseで解除
     * @return 常にfalse (結果はワーカー完了後の遅延応答で返す)
     * @note 不正なエイリアスは InvalidArgs、受理されなかった要求 (未登録・システム・ローカルRPM等)
     *       は ZyppManager::lastError() の理由付きで org.freedesktop.DBus.Error.Failed を返す
     */
    bool PackageManagerAdaptor::SetRepoUpgrade(const QString &alias, bool enabled)
    {
        // 他のエイリアス入力と判定を揃えるため、長さ上限も含めて RepoPolicy に一本化する
        std::string aliasError;
        if (!RepoPolicy::validateAlias(alias.toStdString(), aliasError)) {
            if (checkNotBusy() && checkOwner())
                sendErrorReply(QDBusError::InvalidArgs, QStringLiteral("Invalid repository alias"));
            return false;
        }
        if (!checkNotBusy() || !checkOwner() || !checkArguments())
            return false;

        // runOwnerSync は拒否理由を返信に載せられないため、失敗時にエラー名と理由を
        // 設定できる startWorker を直接使い、GUIが原因を表示できるようにする
        startWorker(message(), [alias, enabled] {
            AsyncOutcome result;
            auto &manager = ZyppManager::instance();
            result.value = manager.setRepoUpgrade(alias.toStdString(), enabled);
            if (!result.value.toBool()) {
                result.errorName = QStringLiteral("org.freedesktop.DBus.Error.Failed");
                result.errorText = QString::fromStdString(manager.lastError());
            }
            return result;
        });
        return false;
    }

    /** @brief 所有者の切り替え対象一覧をワーカーから取得する */
    QStringList PackageManagerAdaptor::GetUpgradeRepos()
    {
        return runRead([] {
            QStringList aliases;
            for (const auto &alias : ZyppManager::instance().upgradeRepos())
                aliases.append(QString::fromStdString(alias));
            return QVariant(aliases);
        }).toStringList();
    }

    /**
     * @brief 拡張検索オプションを検証し、旧GUI互換の一覧を返す
     * @param options 検索オプション
     * @return パッケージ情報のQVariantMap一覧
     */
    QVariantList PackageManagerAdaptor::SearchPackagesEx(const QVariantMap &options)
    {
        SearchOptions parsed;
        if (!prepareExSearch(options, &parsed))
            return {};
        return runRead([parsed] {
            return QVariant(maps(exSearchResults(parsed)));
        }).toList();
    }

    /**
     * @brief 拡張検索オプションを検証し、結果を構造化した一覧で返す
     * @param options 検索オプション
     * @return リポジトリ表とパッケージ行の一覧応答
     */
    PackageListReply PackageManagerAdaptor::SearchPackagesExV2(const QVariantMap &options)
    {
        SearchOptions parsed;
        if (!prepareExSearch(options, &parsed))
            return {};
        return runRead([parsed] {
            return QVariant::fromValue(PackageListReply{toPackageRows(exSearchResults(parsed))});
        }).value<PackageListReply>();
    }

    /**
     * @brief 所有権を確認し、拡張検索オプションを検証する (旧形式とV2で共有)
     * @param options 呼び出し元が渡したオプションマップ
     * @param parsed 検証済みオプションの格納先
     * @return 検証を通った場合はtrue (失敗時は応答済み)
     */
    bool PackageManagerAdaptor::prepareExSearch(const QVariantMap &options, SearchOptions *parsed)
    {
        // 正規表現のコンパイル検証は主スレッドで行うため、所有者確認を先に済ませる
        if (!checkRead())
            return false;
        QString error;
        if (parseSearchOptions(options, *parsed, error))
            return true;
        // 不正な正規表現やオプションはワーカーに渡さず呼び出し元へ返す
        sendErrorReply(QDBusError::InvalidArgs, error);
        return false;
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
