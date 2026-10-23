/**
 * @file PackageControllerTransactions.cpp
 * @brief パッケージ状態の変更、依存関係の解決、コミット、状態保存の実装
 *
 * 状態変更、ロック、一括操作、ソルバー設定、コミット前のライセンス確認、
 * ローカルRPMの取り込みを非同期で実行する
 * 変更後は一覧と変更予定を再取得し、結果をシグナルで通知する
 *
 * 入力 : QMLからの変更要求、DBusClientの完了コールバック
 * 出力 : キャッシュの更新、進捗と完了のシグナル
 * 依存 : Qt Core、DBusClient、LicenseConfirmationResult、LicensePreparation
 */
#include <QDebug>
#include <QFile>
#include <QSettings>
#include <unistd.h>
#include <fcntl.h>
#include <cstring>
#include <cerrno>
#include <QFileInfo>
#include <QUrl>
#include "PackageController.h"
#include "DBusClient.h"
#include "LicenseConfirmationResult.h"
#include "LicensePreparation.h"

namespace qZypper {

namespace {

/**
 * @brief 変更予定の再取得後に通知する操作結果を返す
 *
 * 再取得中にバックエンドから切断した場合は、旧操作の結果を成功として通知しないよう
 * success = false / backendCrashed = trueに変換する (QML側はbackendCrashedで処理を中止する)
 */
QVariantMap resultAfterPendingRefresh(QVariantMap result, bool refreshed, bool connected)
{
    if (!refreshed && !connected) {
        result[QStringLiteral("success")] = false;
        result[QStringLiteral("backendCrashed")] = true;
    }
    return result;
}

} // namespace

// -- 状態変更 --

/**
 * @brief パッケージのステータスを変更し、依存関係を非同期で解決する
 *
 * 変更後はソルバーを実行し、成功時にパッケージ一覧と変更予定を再取得する
 *
 * @param name パッケージ名
 * @param status 新しいステータス (PackageStatus)
 */
void PackageController::setPackageStatus(const QString &name, int status)
{
    if (!m_connected) return;
    if (m_busy) return;

    setBusy(true);
    m_dbus->setPackageStatus(name, status, [this](
                                 bool ok, const QString &, const QString &, const QVariantList &) {
        if (!ok) {
            emit errorOccurred(m_dbus->lastError());
            setBusy(false);
            return;
        }
        m_dbus->resolveDependencies([this](bool rok, const QString &, const QString &, const QVariantList &) {
            if (!rok)
                emit errorOccurred(m_dbus->lastError());
            refreshPackageStatuses();
            refreshPendingChanges();
            setBusy(false);
        });
    });
}

/**
 * @brief パッケージのバージョン/アーキテクチャ/リポジトリを変更する (非同期)
 *
 * 変更後はソルバーを実行し、成功時に一覧と変更予定を再取得する
 *
 * @param name パッケージ名
 * @param version バージョン
 * @param arch アーキテクチャ
 * @param repoAlias リポジトリエイリアス
 */
void PackageController::setPackageVersion(const QString &name,
                                          const QString &version,
                                          const QString &arch,
                                          const QString &repoAlias)
{
    if (!m_connected) return;
    if (m_busy) return;

    setBusy(true);
    m_dbus->setPackageVersion(name, version, arch, repoAlias,
                              [this, name](bool ok, const QString &, const QString &, const QVariantList &) {
        if (!ok) {
            emit errorOccurred(m_dbus->lastError());
            setBusy(false);
            return;
        }
        m_dbus->resolveDependencies([this, name](bool rok, const QString &, const QString &, const QVariantList &) {
            if (!rok)
                emit errorOccurred(m_dbus->lastError());
            refreshPackageStatuses();
            refreshPendingChanges();
            loadPackageDetails(name);
            setBusy(false);
        });
    });
}

/**
 * @brief 複数版対応パッケージの特定版の選択状態を変更する (非同期)
 *
 * 変更後はソルバーを実行し、成功時に一覧・変更予定・詳細を再取得する
 *
 * @param name パッケージ名
 * @param version バージョン
 * @param arch アーキテクチャ
 * @param repoAlias リポジトリエイリアス
 * @param status 新しいステータス (PackageStatus)
 * @note 完了時は状態一覧・保留中変更・パッケージ詳細の順に再読込する
 */
void PackageController::setPackageVersionStatus(const QString &name,
                                                const QString &version,
                                                const QString &arch,
                                                const QString &repoAlias,
                                                int status)
{
    // 未接続または処理中は要求を受け付けない
    if (!m_connected) {
        return;
    }

    if (m_busy) {
        return;
    }

    setBusy(true);

    // DBusClient経由でバックエンドへ版単位の選択状態の変更を要求する
    m_dbus->setPackageVersionStatus(name, version, arch, repoAlias, status,
                                    [this, name](bool ok, const QString &, const QString &, const QVariantList &) {
        if (!ok) {
            emit errorOccurred(m_dbus->lastError());
            setBusy(false);
            return;
        }

        // 選択変更後にソルバーを実行し、依存関係の解決結果を待つ
        m_dbus->resolveDependencies([this, name](bool rok, const QString &, const QString &, const QVariantList &) {
            if (!rok) {
                emit errorOccurred(m_dbus->lastError());
            }

            refreshPackageStatuses();
            refreshPendingChanges();
            loadPackageDetails(name);
            setBusy(false);
        });
    });
}

/**
 * @brief パターンのステータスをトグルし、依存関係を非同期で解決する
 *
 * YaSTのcycleStatus()と同じロジックでステータスを切り替え、ソルバーを実行する
 *
 * @param patternName パターン名
 */
void PackageController::togglePatternStatus(const QString &patternName)
{
    if (!m_connected) return;
    if (m_busy) return;

    // 現在のパターンステータスを取得
    int currentStatus = 0;
    for (const auto &p : std::as_const(m_patterns)) {
        auto map = p.toMap();
        if (map[QStringLiteral("name")].toString() == patternName) {
            currentStatus = map[QStringLiteral("status")].toInt();
            break;
        }
    }

    // YaSTのcycleStatus()と同じトグルロジック
    int newStatus;
    switch (currentStatus) {
        case 0:  // NoInst → Install
            newStatus = 1;
            break;
        case 1:  // Install → NoInst
            newStatus = 0;
            break;
        case 2:  // AutoInstall → NoInst
            newStatus = 0;
            break;
        case 3:  // KeepInstalled → Del
            newStatus = 6;
            break;
        case 6:  // Del → KeepInstalled
            newStatus = 3;
            break;
        default:
            return;
    }

    // ソースが別のパターンへ切り替わる場合は新しい世代として通知する
    const bool sourceChanged = m_lastSource != PackageSource::Pattern || m_lastSourceParam != patternName;
    m_lastSource      = PackageSource::Pattern;
    m_lastSourceParam = patternName;

    setBusy(true);

    m_dbus->setPatternStatus(patternName, newStatus,
                             [this, patternName, newStatus, sourceChanged](
                                 bool ok, const QString &, const QString &, const QVariantList &) {
        if (!ok) {
            emit errorOccurred(m_dbus->lastError());
            setBusy(false);
            return;
        }

        // パターンリストのローカルステータスを即座に更新
        for (int i = 0; i < m_patterns.size(); ++i) {
            auto map = m_patterns[i].toMap();
            if (map[QStringLiteral("name")].toString() == patternName) {
                map[QStringLiteral("status")] = newStatus;
                m_patterns[i] = map;
                break;
            }
        }
        emit patternsChanged();

        // ソルバーを実行する (fork保護下で依存パッケージの自動選択と衝突検出を行う)
        m_dbus->resolveDependencies(
            [this, sourceChanged](bool rok, const QString &, const QString &, const QVariantList &rargs) {
            QVariantMap result = (rok && !rargs.isEmpty()) ? rargs.at(0).toMap() : QVariantMap();

            if (!m_dbus->isConnected()) {
                // バックエンドがクラッシュした場合、パッケージリストのみ再取得
                qWarning() << "Backend disconnected during pattern resolve";
                m_packages = QVariantList();
                if (sourceChanged)
                    ++m_listGeneration;
                emit packagesChanged();
                setBusy(false);
                return;
            }

            if (result.value(QStringLiteral("success")).toBool()) {
                // 成功時は自動選択された依存パッケージの状態を含めて一覧を再取得する
                refreshPackageStatuses(sourceChanged);
            } else {
                // 衝突時は一覧を再取得してからコンフリクトダイアログを表示する
                refreshPackageStatuses(sourceChanged);
                auto problems = result.value(QStringLiteral("problems")).toList();
                if (!problems.isEmpty())
                    emit conflictsDetected(problems);
            }

            // パターン一覧もソルバー結果を反映して再取得
            loadPatterns();
            refreshPendingChanges();
            setBusy(false);
        });
    });
}

/**
 * @brief パッチの選択状態を変更する (非同期)
 * @param patchId パッチID
 * @param status 新ステータス (Install = 1 / NoInst = 0)
 */
void PackageController::setPatchStatus(const QString &patchId, int status)
{
    if (!m_connected) return;

    m_dbus->setPatchStatus(patchId, status, [this](bool ok, const QString &, const QString &errorText, const QVariantList &) {
        if (!ok) {
            emit errorOccurred(errorText);
            return;
        }
        refreshPendingChanges();
        refreshPackageStatuses();
        loadPatches(m_patchCategory);
    });
}

/**
 * @brief 必要なパッチを一括選択する (非同期)
 *
 * 対話型パッチはskipInteractiveで自動的にスキップし、選択対象から除外する
 * 結果はselectPatchesFinishedで通知され、QML側で概要確認フローへ渡す
 *
 * @param securityOnly セキュリティパッチのみ選択する場合true
 * @param includeOptional オプションパッチも選択する場合true
 */
void PackageController::selectAllNeededPatches(bool securityOnly, bool includeOptional)
{
    if (!m_connected) return;
    if (m_busy) return;

    setBusy(true);
    setStatusMessage(tr("Selecting needed patches..."));

    QVariantMap options;
    options[QStringLiteral("securityOnly")] = securityOnly;
    options[QStringLiteral("includeOptional")] = includeOptional;
    options[QStringLiteral("skipInteractive")] = true;

    m_dbus->selectAllNeededPatches(options, [this](bool ok, const QString &, const QString &errorText, const QVariantList &args) {
        setBusy(false);
        if (!ok) {
            emit errorOccurred(errorText);
            return;
        }
        QVariantMap result = args.isEmpty() ? QVariantMap() : args.at(0).toMap();
        refreshPackageStatuses(true);
        cacheTransactionSummary(result);
        loadPatches(m_patchCategory);
        // 変更予定一覧の更新完了後に通知する (古い一覧で判定しないため)
        refreshPendingChangesThen([this, result](bool refreshed) {
            emit selectPatchesFinished(resultAfterPendingRefresh(result, refreshed, m_connected));
        });
    });
}

// -- ロック --

/**
 * @brief パッケージロック一覧を読み込む (非同期)
 */
void PackageController::loadLocks()
{
    if (!m_connected) return;

    m_dbus->getLocks([this](bool ok, const QString &, const QString &errorText, const QVariantList &args) {
        if (!ok) {
            emit errorOccurred(errorText);
            return;
        }
        m_locks = args.isEmpty() ? QVariantList() : args.at(0).toList();
        emit locksChanged();
    });
}

/**
 * @brief パッケージロックを追加する (非同期)
 * @param lock ロック定義マップ (name/kind/match/repoAlias/arch/version/comment)
 */
void PackageController::addLock(const QVariantMap &lock)
{
    if (!m_connected) return;

    m_dbus->addLock(lock, [this](bool ok, const QString &, const QString &errorText, const QVariantList &) {
        if (!ok)
            emit errorOccurred(errorText);
        emit lockFinished(ok);
        if (ok)
            loadLocks();
    });
}

/**
 * @brief パッケージロックを削除する (非同期)
 * @param lock 削除対象のロック定義マップ
 */
void PackageController::removeLock(const QVariantMap &lock)
{
    if (!m_connected) return;

    m_dbus->removeLock(lock, [this](bool ok, const QString &, const QString &errorText, const QVariantList &) {
        if (!ok)
            emit errorOccurred(errorText);
        emit lockFinished(ok);
        if (ok)
            loadLocks();
    });
}

// -- 一括更新・検証 --

/**
 * @brief 全パッケージを非同期で更新する
 *
 * 完了するとupdateAllFinishedシグナルで結果を通知する
 * バックエンドがクラッシュした場合は再接続を試み、backendCrashed=trueを含む結果を通知する
 */
void PackageController::updateAllPackages()
{
    if (!m_connected) return;
    if (m_busy) return;

    setBusy(true);
    setStatusMessage(tr("Updating all packages..."));

    m_dbus->updateAllPackages([this](bool ok, const QString &, const QString &errorText, const QVariantList &args) {
        QVariantMap result = (ok && !args.isEmpty()) ? args.at(0).toMap() : QVariantMap();

        // バックエンドがクラッシュした場合の復旧
        if (!m_dbus->isConnected()) {
            setStatusMessage(tr("Disconnected from backend. Reconnecting..."));
            reconnectBackend([this, result](bool reconnected) mutable {
                if (reconnected)
                    setStatusMessage(tr("Reconnected. Please try again"));
                else
                    setStatusMessage(tr("Failed to reconnect to backend"));
                setBusy(false);
                QVariantMap failed = result;
                failed[QStringLiteral("success")] = false;
                failed[QStringLiteral("backendCrashed")] = true;
                emit updateAllFinished(failed);
            });
            return;
        }

        // D-Bus呼び出し自体が拒否/失敗した場合は、依存関係の問題として扱わない
        if (!ok) {
            const QString message = errorText.isEmpty() ? tr("Update packages failed") : errorText;
            emit errorOccurred(message);
            result[QStringLiteral("success")] = false;
            result[QStringLiteral("busError")] = true;
            setStatusMessage(message);
            setBusy(false);
            refreshPendingChangesThen([this, result](bool refreshed) {
                emit updateAllFinished(resultAfterPendingRefresh(result, refreshed, m_connected));
            });
            return;
        }

        refreshPackageStatuses(true);
        cacheTransactionSummary(result);

        if (result[QStringLiteral("success")].toBool())
            setStatusMessage(tr("Update check completed"));
        else
            setStatusMessage(tr("Dependency problems found"));

        setBusy(false);
        // 変更予定一覧の更新完了後に通知する (古い一覧で判定しないため)
        refreshPendingChangesThen([this, result](bool refreshed) {
            emit updateAllFinished(resultAfterPendingRefresh(result, refreshed, m_connected));
        });
    });
}

/**
 * @brief ディストリビューション更新 (dist-upgrade) を非同期で実行する
 *
 * 結果はupdateAllFinishedと同様にdistUpgradeFinishedで通知され、QML側でコンフリクト/概要確認フローへ同じ経路で渡される
 *
 * @param allowVendorChange ベンダー変更を許可する場合はtrue
 */
void PackageController::distUpgrade(bool allowVendorChange)
{
    if (!m_connected) return;
    if (m_busy) return;

    setBusy(true);
    setStatusMessage(tr("Preparing distribution upgrade..."));

    QVariantMap options;
    options[QStringLiteral("allowVendorChange")] = allowVendorChange;

    m_dbus->distUpgrade(options, [this](bool ok, const QString &, const QString &, const QVariantList &args) {
        QVariantMap result = (ok && !args.isEmpty()) ? args.at(0).toMap() : QVariantMap();

        if (!m_dbus->isConnected()) {
            setStatusMessage(tr("Disconnected from backend. Reconnecting..."));
            reconnectBackend([this, result](bool reconnected) mutable {
                if (reconnected)
                    setStatusMessage(tr("Reconnected. Please try again"));
                else
                    setStatusMessage(tr("Failed to reconnect to backend"));
                setBusy(false);
                QVariantMap failed = result;
                failed[QStringLiteral("success")] = false;
                failed[QStringLiteral("backendCrashed")] = true;
                emit distUpgradeFinished(failed);
            });
            return;
        }

        refreshPackageStatuses(true);
        cacheTransactionSummary(result);

        if (result[QStringLiteral("success")].toBool())
            setStatusMessage(tr("Distribution upgrade check completed"));
        else
            setStatusMessage(tr("Dependency problems found"));

        setBusy(false);
        // 変更予定一覧の更新完了後に通知する (古い一覧で判定しないため)
        refreshPendingChangesThen([this, result](bool refreshed) {
            emit distUpgradeFinished(resultAfterPendingRefresh(result, refreshed, m_connected));
        });
    });
}

/**
 * @brief システムの依存関係整合性検証 (zypper verify相当) を非同期で実行する
 *
 * 結果はverifyFinishedで通知される (problems / toCleanを含む)
 */
void PackageController::verifySystem()
{
    if (!m_connected) return;
    if (m_busy) return;

    setBusy(true);
    setStatusMessage(tr("Verifying system integrity..."));

    m_dbus->verifySystem([this](bool ok, const QString &, const QString &errorText, const QVariantList &args) {
        if (!ok) {
            setBusy(false);
            emit errorOccurred(errorText.isEmpty() ? tr("Verify system failed") : errorText);
            return;
        }
        const QVariantMap result = args.isEmpty() ? QVariantMap() : args.at(0).toMap();
        refreshPackageStatuses(true);
        setStatusMessage(tr("System verification completed"));
        // 変更予定一覧の更新完了後に通知し、その完了を待機してビジーを解除する (途中で解除しないため)
        refreshPendingChangesThen([this, result](bool refreshed) {
            setBusy(false);
            emit verifyFinished(resultAfterPendingRefresh(result, refreshed, m_connected));
        });
    });
}

// -- 一括選択・ソルバー設定・ロケール --

/**
 * @brief 複数パッケージの状態を一括変更する (非同期)
 * @param names 対象パッケージ名の一覧
 * @param action 操作名 (install/remove/update/updateForce/keep/taboo/protect)
 *
 * 対象が空の場合は何もしない
 * D-BusのSetPackagesActionの成功後に依存関係を1回だけ解決し
 * 一覧と変更予定の更新完了後にpackagesActionFinishedを発行する
 */
void PackageController::setPackagesAction(const QStringList &names, const QString &action)
{
    if (!m_connected) {
        const QString message = tr("Package manager is not initialized");
        emit errorOccurred(message);
        QVariantMap result;
        result[QStringLiteral("ok")] = false;
        result[QStringLiteral("success")] = false;
        result[QStringLiteral("busError")] = true;
        result[QStringLiteral("errorMessage")] = message;
        emit packagesActionFinished(result);
        return;
    }
    if (m_busy) {
        return;
    }
    if (names.isEmpty()) {
        return;
    }

    setBusy(true);
    setStatusMessage(tr("Applying package changes..."));
    const quint64 token = beginBatchAction();

    m_dbus->setPackagesAction(names, action, [this, token](bool ok, const QString &,
                                                            const QString &errorText, const QVariantList &args) {
        if (!isBatchActive(token)) {
            return;
        }
        const QVariantMap result = (ok && !args.isEmpty()) ? args.at(0).toMap() : QVariantMap();
        if (!ok) {
            failBatchAction(token, result, errorText);
            return;
        }
        m_batchResult = result;
        resolveBatchAction(token, result);
    });
}

/**
 * @brief 指定サフィックスのサブパッケージを選択する (非同期)
 * @param suffix サフィックス (-devel/-debuginfo/-debugsource)
 *
 * D-BusのSelectSubpackagesの成功後に依存関係を1回だけ解決し
 * 一覧と変更予定の更新完了後にpackagesActionFinishedを発行する
 */
void PackageController::selectSubpackages(const QString &suffix)
{
    if (!m_connected) {
        const QString message = tr("Package manager is not initialized");
        emit errorOccurred(message);
        QVariantMap result;
        result[QStringLiteral("ok")] = false;
        result[QStringLiteral("success")] = false;
        result[QStringLiteral("busError")] = true;
        result[QStringLiteral("errorMessage")] = message;
        emit packagesActionFinished(result);
        return;
    }
    if (m_busy) {
        return;
    }
    if (suffix.isEmpty()) {
        return;
    }

    setBusy(true);
    setStatusMessage(tr("Selecting subpackages..."));
    const quint64 token = beginBatchAction();

    m_dbus->selectSubpackages(suffix, [this, token](bool ok, const QString &,
                                                     const QString &errorText, const QVariantList &args) {
        if (!isBatchActive(token)) {
            return;
        }
        const QVariantMap result = (ok && !args.isEmpty()) ? args.at(0).toMap() : QVariantMap();
        if (!ok) {
            failBatchAction(token, result, errorText);
            return;
        }
        m_batchResult = result;
        resolveBatchAction(token, result);
    });
}

/**
 * @brief 推奨パッケージをすべて選択する (非同期)
 *
 * D-BusのSelectAllRecommendedを呼び出しソルバー結果を受け取る
 * 問題がある場合はconflictsDetectedを発行して競合ダイアログへ通知する
 * 一覧と変更予定の更新完了後にpackagesActionFinishedを発行する
 */
void PackageController::installAllRecommended()
{
    if (!m_connected) {
        const QString message = tr("Package manager is not initialized");
        emit errorOccurred(message);
        QVariantMap result;
        result[QStringLiteral("ok")] = false;
        result[QStringLiteral("success")] = false;
        result[QStringLiteral("busError")] = true;
        result[QStringLiteral("errorMessage")] = message;
        emit packagesActionFinished(result);
        return;
    }
    if (m_busy) {
        return;
    }

    setBusy(true);
    setStatusMessage(tr("Selecting recommended packages..."));
    const quint64 token = beginBatchAction();

    m_dbus->selectAllRecommended([this, token](bool ok, const QString &,
                                                const QString &errorText, const QVariantList &args) {
        if (!isBatchActive(token)) {
            return;
        }
        const QVariantMap result = (ok && !args.isEmpty()) ? args.at(0).toMap() : QVariantMap();
        if (!ok) {
            failBatchAction(token, result, errorText);
            return;
        }
        m_batchResult = result;
        const bool solverOk = applySolverResult(result);
        if (solverOk) {
            setStatusMessage(tr("Recommended packages selected"));
        }
        completeBatchAction(token, result, solverOk);
    });
}

/**
 * @brief ソルバー設定を1項目変更する (非同期)
 * @param key 設定キー (installRecommends/cleandepsOnRemove/allowVendorChange)
 * @param value 新しい値
 *
 * SetSolverSettingsの成功後に依存関係を解決し、一覧と変更予定を更新して
 * 最後にloadSolverSettingsで設定を再読込する
 * installRecommendsはQSettingsのsolver/installRecommendsへ保存する
 * SetSolverSettingsの成功時にsolverOptionAppliedシグナルを発行し、失敗時はerrorOccurredシグナルだけを発行する
 */
void PackageController::setSolverOption(const QString &key, bool value)
{
    if (!m_connected) {
        emit errorOccurred(tr("Package manager is not initialized"));
        return;
    }
    if (m_busy) {
        return;
    }

    setBusy(true);
    setStatusMessage(tr("Updating solver settings..."));

    QVariantMap settings;
    settings[key] = value;

    m_dbus->setSolverSettings(settings, [this, key, value](bool ok, const QString &,
                                                            const QString &errorText, const QVariantList &) {
        if (!ok) {
            emit errorOccurred(errorText.isEmpty() ? tr("Failed to update solver settings") : errorText);
            setBusy(false);
            return;
        }

        // installRecommendsのみ次回起動用に永続化する
        if (key == QStringLiteral("installRecommends")) {
            QSettings store;
            store.setValue(QStringLiteral("solver/installRecommends"), value ? 1 : 0);
        }

        // 適用に成功した場合だけ通知する
        // QML側の保存値は、この通知を受けたときだけ更新する (失敗時は保存値を変えないため)
        emit solverOptionApplied(key, value);

        m_dbus->resolveDependencies([this](bool rok, const QString &, const QString &resolveError,
                                           const QVariantList &args) {
            if (!rok) {
                emit errorOccurred(resolveError.isEmpty()
                                       ? tr("Dependency resolution failed") : resolveError);
            } else {
                const QVariantMap result = args.isEmpty() ? QVariantMap() : args.at(0).toMap();
                applySolverResult(result);
            }
            refreshPackageStatuses();
            refreshPendingChanges();
            loadSolverSettings();
            setBusy(false);
        });
    });
}

/**
 * @brief 言語サポートの要求状態を変更する (非同期)
 * @param code ロケールコード
 * @param requested 要求する場合はtrue
 *
 * SetLocaleRequestedの成功後に依存関係を解決し、一覧と変更予定を更新して
 * 最後にloadLocalesで一覧を再読込する
 */
void PackageController::setLocaleRequested(const QString &code, bool requested)
{
    if (!m_connected) {
        emit errorOccurred(tr("Package manager is not initialized"));
        return;
    }
    if (m_busy) {
        return;
    }
    if (code.isEmpty()) {
        return;
    }

    setBusy(true);
    setStatusMessage(tr("Updating locale selection..."));

    m_dbus->setLocaleRequested(code, requested, [this](bool ok, const QString &,
                                                        const QString &errorText, const QVariantList &) {
        if (!ok) {
            emit errorOccurred(errorText.isEmpty() ? tr("Failed to update locale selection") : errorText);
            setBusy(false);
            return;
        }

        m_dbus->resolveDependencies([this](bool rok, const QString &, const QString &resolveError,
                                           const QVariantList &args) {
            if (!rok) {
                emit errorOccurred(resolveError.isEmpty()
                                       ? tr("Dependency resolution failed") : resolveError);
            } else {
                const QVariantMap result = args.isEmpty() ? QVariantMap() : args.at(0).toMap();
                applySolverResult(result);
            }
            refreshPackageStatuses();
            refreshPendingChanges();
            loadLocales();
            setBusy(false);
        });
    });
}

/**
 * @brief ソルバー応答を共通処理する
 * @param result ソルバー応答のマップ
 * @return ソルバーが成功した場合はtrue
 *
 * success=falseの場合はステータスを更新し、problemsをconflictsDetectedへ流す
 */
bool PackageController::applySolverResult(const QVariantMap &result)
{
    if (result.value(QStringLiteral("success")).toBool()) {
        return true;
    }
    setStatusMessage(tr("Dependency problems found"));
    const QVariantList problems = result.value(QStringLiteral("problems")).toList();
    if (!problems.isEmpty()) {
        emit conflictsDetected(problems);
    }
    return false;
}

/**
 * @brief 一括操作の世代を開始する
 * @return 新しい世代番号
 *
 * 古い応答は世代が一致しないため無視される
 */
quint64 PackageController::beginBatchAction()
{
    ++m_batchGeneration;
    m_batchInFlight = true;
    m_batchResult.clear();
    return m_batchGeneration;
}

/**
 * @brief 一括操作の世代が有効か返す
 * @param token 一括操作の世代番号
 * @return 実行中かつ最新の世代の場合はtrue
 */
bool PackageController::isBatchActive(quint64 token) const
{
    return m_batchInFlight && token == m_batchGeneration;
}

/**
 * @brief 一括変更の成功後に依存関係を1回だけ解決する
 * @param token 一括操作の世代番号
 * @param result D-Busが返した一括変更の結果
 *
 * 解決結果のsuccess=falseは共通の競合通知経路へ流し、その後一覧の再取得へ進む
 */
void PackageController::resolveBatchAction(quint64 token, const QVariantMap &result)
{
    m_dbus->resolveDependencies([this, token, result](bool ok, const QString &,
                                                       const QString &errorText, const QVariantList &args) {
        if (!isBatchActive(token)) {
            return;
        }
        if (!ok) {
            // 変更自体は成功しているため、一覧と変更予定を再取得してから失敗として完了する
            const QString message = errorText.isEmpty() ? tr("Dependency resolution failed") : errorText;
            emit errorOccurred(message);
            QVariantMap out = result;
            out[QStringLiteral("success")] = false;
            out[QStringLiteral("errorMessage")] = message;
            out[QStringLiteral("reason")] = QStringLiteral("resolveFailed");
            setStatusMessage(message);
            completeBatchAction(token, out, false);
            return;
        }
        const QVariantMap solver = args.isEmpty() ? QVariantMap() : args.at(0).toMap();
        const bool solverOk = applySolverResult(solver);
        if (solverOk) {
            setStatusMessage(tr("Package changes applied"));
        }
        completeBatchAction(token, result, solverOk);
    });
}

/**
 * @brief 一覧と変更予定の再取得後に一括操作を完了する
 * @param token 一括操作の世代番号
 * @param result 一括変更の結果 (changed/skipped/failedを保持する)
 * @param solverOk ソルバーが成功した場合はtrue
 *
 * 一覧と変更予定の再取得成否をokへ反映し、失敗理由をreasonへ設定する
 * 新しい一覧要求に置き換えられた場合は最新の取得完了を待つ
 */
void PackageController::completeBatchAction(quint64 token, QVariantMap result, bool solverOk)
{
    result[QStringLiteral("success")] = solverOk;
    requestPackageList(true, [this, token, result, solverOk](bool listOk, const QVariantList &) {
        if (!isBatchActive(token)) {
            return;
        }
        refreshPendingChangesThen([this, token, result, solverOk, listOk](bool pendingOk) {
            if (!isBatchActive(token)) {
                return;
            }
            QVariantMap out = result;
            out[QStringLiteral("ok")] = solverOk && listOk && pendingOk;
            // 解決段階の失敗理由がある場合は再取得の失敗で上書きしない
            if (!listOk) {
                out[QStringLiteral("refreshFailed")] = true;
                if (out.value(QStringLiteral("reason")).toString().isEmpty()) {
                    out[QStringLiteral("reason")] = QStringLiteral("listRefreshFailed");
                    setStatusMessage(tr("Failed to refresh package list"));
                }
            } else if (!pendingOk) {
                out[QStringLiteral("refreshFailed")] = true;
                if (out.value(QStringLiteral("reason")).toString().isEmpty()) {
                    out[QStringLiteral("reason")] = QStringLiteral("pendingChangesRefreshFailed");
                    setStatusMessage(tr("Failed to refresh pending changes"));
                }
            }
            if (!m_connected) {
                out[QStringLiteral("ok")] = false;
                out[QStringLiteral("success")] = false;
                out[QStringLiteral("backendCrashed")] = true;
            }
            finishPackagesAction(token, out);
        });
    });
}

/**
 * @brief 一括操作の失敗を通知する
 * @param token 一括操作の世代番号
 * @param result 失敗時点までの結果
 * @param errorText 失敗時のエラーメッセージ
 *
 * 一覧を変更していないため再取得は行わず、busy解除後に1回だけ通知する
 */
void PackageController::failBatchAction(quint64 token, const QVariantMap &result,
                                        const QString &errorText)
{
    if (!isBatchActive(token)) {
        return;
    }
    const QString message = errorText.isEmpty() ? tr("Package action failed") : errorText;
    emit errorOccurred(message);
    QVariantMap out = result;
    out[QStringLiteral("ok")] = false;
    out[QStringLiteral("success")] = false;
    out[QStringLiteral("busError")] = true;
    out[QStringLiteral("errorMessage")] = message;
    setStatusMessage(message);
    finishPackagesAction(token, out);
}

/**
 * @brief 一括操作の結果を1回だけ通知する
 * @param token 一括操作の世代番号
 * @param result 通知する結果
 *
 * 世代を進めて遅延応答を無効化し、busy解除後に通知する
 */
void PackageController::finishPackagesAction(quint64 token, const QVariantMap &result)
{
    if (!isBatchActive(token)) {
        return;
    }
    m_batchInFlight = false;
    m_batchResult.clear();
    ++m_batchGeneration;
    setBusy(false);
    emit packagesActionFinished(result);
}

/**
 * @brief 切断時に一括操作の結果を収集して世代を無効化する
 * @return 通知用の結果 (実行中でない場合は空)
 *
 * busy解除とシグナル発行は行わず、呼び出し側が最後に一度だけ行う
 * 世代を進めるため、収集後に届いた応答は無視される
 */
QVariantMap PackageController::takeAbortedBatchResult()
{
    if (!m_batchInFlight) {
        return QVariantMap();
    }
    m_batchInFlight = false;
    QVariantMap out = m_batchResult;
    m_batchResult.clear();
    ++m_batchGeneration;
    out[QStringLiteral("ok")] = false;
    out[QStringLiteral("success")] = false;
    out[QStringLiteral("backendCrashed")] = true;
    return out;
}

/**
 * @brief 切断時に実行中の一括操作を失敗として完了させる
 *
 * 実行中の場合のみ結果を収集し、busy解除後に1回だけ通知する
 */
void PackageController::abortBatchAction()
{
    const QVariantMap out = takeAbortedBatchResult();
    if (out.isEmpty()) {
        return;
    }
    setBusy(false);
    emit packagesActionFinished(out);
}

// -- 依存関係の解決 --

/**
 * @brief 依存関係を非同期で解決する
 *
 * 完了するとresolveFinishedシグナルで結果を通知する
 * 成功時にパッケージ一覧と変更予定を再取得する
 */
void PackageController::resolveDependencies()
{
    if (!m_connected) return;
    if (m_busy) return;

    setBusy(true);
    setStatusMessage(tr("Resolving dependencies..."));

    m_dbus->resolveDependencies([this](bool ok, const QString &, const QString &errorText, const QVariantList &args) {
        QVariantMap result = (ok && !args.isEmpty()) ? args.at(0).toMap() : QVariantMap();

        // バックエンドがクラッシュした場合の復旧
        if (!m_dbus->isConnected()) {
            setStatusMessage(tr("Disconnected from backend. Reconnecting..."));
            reconnectBackend([this, result](bool reconnected) mutable {
                if (reconnected)
                    setStatusMessage(tr("Reconnected. Please try again"));
                else
                    setStatusMessage(tr("Failed to reconnect to backend"));
                setBusy(false);
                QVariantMap failed = result;
                failed[QStringLiteral("success")] = false;
                failed[QStringLiteral("backendCrashed")] = true;
                emit resolveFinished(failed);
            });
            return;
        }

        // D-Bus呼び出し自体が拒否/失敗した場合は依存関係の問題として扱わない
        if (!ok) {
            const QString message = errorText.isEmpty() ? tr("Resolve dependencies failed") : errorText;
            emit errorOccurred(message);
            result[QStringLiteral("success")] = false;
            result[QStringLiteral("busError")] = true;
            setStatusMessage(message);
            setBusy(false);
            refreshPendingChangesThen([this, result](bool refreshed) {
                emit resolveFinished(resultAfterPendingRefresh(result, refreshed, m_connected));
            });
            return;
        }

        if (result[QStringLiteral("success")].toBool()) {
            setStatusMessage(tr("Dependencies resolved"));
            refreshPackageStatuses();
        } else {
            setStatusMessage(tr("Dependency problems found"));
        }
        setBusy(false);
        // 変更予定一覧の更新完了後に通知する (古い一覧で判定しないため)
        refreshPendingChangesThen([this, result](bool refreshed) {
            emit resolveFinished(resultAfterPendingRefresh(result, refreshed, m_connected));
        });
    });
}

/**
 * @brief 依存関係の衝突に対する解決策を非同期で適用する
 * @param problemIndex 問題のインデックス
 * @param solutionIndex 解決策のインデックス
 */
void PackageController::applySolution(int problemIndex, int solutionIndex)
{
    if (!m_connected) return;
    if (m_busy) return;

    m_dbus->applySolution(problemIndex, solutionIndex,
                          [this](bool ok, const QString &, const QString &, const QVariantList &args) {
        if (!ok || args.isEmpty() || !args.at(0).toBool())
            emit errorOccurred(m_dbus->lastError());
    });
}

/**
 * @brief ソルバー結果に含まれるトランザクション概要をキャッシュする
 * @param result distUpgrade / updateAll / selectAll等の結果マップ (summaryキー込み)
 */
void PackageController::cacheTransactionSummary(const QVariantMap &result)
{
    const QVariantMap summary = result.value(QStringLiteral("summary")).toMap();
    if (summary.isEmpty())
        return;
    m_transactionSummary = summary;
    emit transactionSummaryChanged();
}

/**
 * @brief サーバー側のトランザクション概要を取得する (非同期)
 *
 * 取得結果はtransactionSummaryプロパティへキャッシュされる
 */
void PackageController::requestTransactionSummary()
{
    if (!m_connected) return;

    m_dbus->getTransactionSummary([this](bool ok, const QString &, const QString &, const QVariantList &args) {
        // 取得失敗時も空マップで通知する (QML側は概要不明として明示承認を要求する)
        m_transactionSummary = (ok && !args.isEmpty()) ? args.at(0).toMap() : QVariantMap();
        emit transactionSummaryChanged();
    });
}

/**
 * @brief ソルバーテストケースを作成してパスを通知する (非同期)
 */
void PackageController::saveSolverTestcase()
{
    if (!m_connected) return;

    m_dbus->createSolverTestcase([this](bool ok, const QString &, const QString &errorText, const QVariantList &args) {
        if (!ok) {
            emit errorOccurred(errorText);
            return;
        }
        emit solverTestcaseCreated(args.isEmpty() ? QString() : args.at(0).toString());
    });
}

// -- コミット --

/**
 * @brief 確認済み選択にCommitを束縛するためのリビジョンを非同期で取得する
 *
 * 取得したリビジョンは、commitAsync()で消費される
 * 完了すると、commitPreparedシグナルで成功可否を通知する
 */
void PackageController::prepareCommit()
{
    // 未接続または処理中には確認対象の選択が安定しないため、コミット準備を開始しない
    if (!m_connected || m_busy) {
        m_commitRevision = 0;
        emit commitPrepared(false);
        return;
    }
    m_commitRevision = 0;
    // LicensePreparation::reintroducedLicensePackagesで拒否候補の再選択を検出し、意図しない再承認を防ぐ
    const auto returned = reintroducedLicensePackages(m_rejectedLicensePackages, m_pendingChanges);
    if (!returned.isEmpty()) {
        emit errorOccurred(tr("Packages whose licenses were declined were selected again after resolving dependencies: %1. Return to editing and change the selection or conflict solutions.")
                               .arg(returned.join(QStringLiteral(", "))));
        resetCommitPreparation();
        emit commitPrepared(false);
        return;
    }
    // 非同期D-Bus応答が編集再開後に到着しても結果を反映しないよう世代を固定する
    const auto generation = m_commitPreparationGeneration;
    setBusy(true);
    setStatusMessage(tr("Confirming package licenses..."));
    // DBusClient::confirmLicensesで所有者の対話結果を待ち、完了後に確認済み選択を準備する
    m_dbus->confirmLicenses([this, generation](bool ok, const QString &, const QString &errorText,
                                               const QVariantList &args) {
        setBusy(false);
        // 編集へ戻った後の回答は新しい選択状態を壊すため、破棄する
        if (generation != m_commitPreparationGeneration) {
            emit commitPrepared(false);
            return;
        }
        // LicenseConfirmationResult::fromVariantMapでD-Bus応答を型付き結果へ復元する
        const auto result = LicenseConfirmationResult::fromVariantMap(
            ok && !args.isEmpty() ? args.first().toMap() : QVariantMap{});
        m_rejectedLicensePackages.append(result.rejectedPackages);
        m_rejectedLicensePackages.removeDuplicates();
        emit rejectedLicensePackagesChanged();
        if (!ok || !result.success) {
            emit errorOccurred(!errorText.isEmpty() ? errorText
                : !result.errorMessage.isEmpty() ? result.errorMessage : tr("License confirmation failed"));
            refreshPackageStatuses();
            refreshPendingChanges();
            emit commitPrepared(false);
            return;
        }
        // 拒否により選択が変わった場合は、古い解決結果を使わず通常の依存関係解決を要求する
        if (result.changed) {
            refreshPackageStatuses();
            emit licenseResolveRequired();
            return;
        }
        // PackageController::fetchCommitRevisionで表示用の変更予定とコミット改訂を同じ選択へ揃える
        fetchCommitRevision(3);
    });
}

/**
 * @brief 選択の再読込や編集再開時に、古いコミット確認と除外履歴を破棄する
 * @note m_commitPreparationGenerationを更新して保留中のD-Bus応答を無効にする
 * @note m_commitRevisionとm_rejectedLicensePackagesを変更し、除外一覧変更を通知する
 */
void PackageController::resetCommitPreparation()
{
    ++m_commitPreparationGeneration;
    m_commitRevision = 0;
    m_rejectedLicensePackages.clear();
    emit rejectedLicensePackagesChanged();
}

/**
 * @brief 確認画面に表示する変更予定とコミットrevisionを同じ選択状態に揃える
 *
 * revision取得 → 変更予定取得 → revision再取得を行い、前後のrevisionが一致した場合のみ確定する
 * 不一致 (途中で選択が変わった) なら再試行する
 *
 * @param attemptsLeft 残り試行回数
 */
void PackageController::fetchCommitRevision(int attemptsLeft)
{
    // 非同期の改訂照合中に編集へ戻った場合の応答を破棄するため、開始世代を保持する
    const auto generation = m_commitPreparationGeneration;
    auto parseRevision = [](bool ok, const QVariantList &args, quint64 &rev) {
        if (!ok || args.isEmpty()) {
            return false;
        }
        bool conv = false;
        rev = args.at(0).toULongLong(&conv);
        return conv;
    };
    // 一貫した選択状態を取得できない場合は、コミット可能な改訂を残さずQMLへ失敗を通知する
    auto fail = [this]() {
        m_commitRevision = 0;
        emit errorOccurred(m_dbus->lastError().isEmpty()
                               ? tr("Failed to obtain a consistent selection state")
                               : m_dbus->lastError());
        emit commitPrepared(false);
    };

    // DBusClient::getSelectionRevisionで照合前の選択改訂を取得する
    m_dbus->getSelectionRevision([this, attemptsLeft, parseRevision, fail, generation](
                                      bool ok, const QString &, const QString &, const QVariantList &args) {
        if (generation != m_commitPreparationGeneration) {
            return;
        }
        quint64 before = 0;
        if (!parseRevision(ok, args, before)) {
            fail();
            return;
        }
        // PackageController::refreshPendingChangesThenで確認画面の一覧を更新してから再照合する
        refreshPendingChangesThen([this, attemptsLeft, before, parseRevision, fail, generation](bool refreshed) {
            if (generation != m_commitPreparationGeneration) {
                return;
            }
            // 変更予定を取得できなければ確認画面の一覧が古いままなので確認を中止する
            if (!refreshed || !m_connected) {
                fail();
                return;
            }
            // DBusClient::getSelectionRevisionで一覧更新後の選択改訂を取得する
            m_dbus->getSelectionRevision([this, attemptsLeft, before, parseRevision, fail, generation](
                                              bool ok2, const QString &, const QString &, const QVariantList &args2) {
                if (generation != m_commitPreparationGeneration) {
                    return;
                }
                quint64 after = 0;
                if (!parseRevision(ok2, args2, after)) {
                    fail();
                    return;
                }
                if (after != before) {
                    if (attemptsLeft > 1) {
                        fetchCommitRevision(attemptsLeft - 1);
                    }
                    else {
                        fail();
                    }
                    return;
                }
                if (m_pendingChanges.isEmpty() && m_pendingLocaleChanges.isEmpty()) {
                    m_commitRevision = 0;
                    emit noChangesLeftToApply();
                    emit commitPrepared(false);
                    return;
                }
                m_commitRevision = after;
                emit commitPrepared(true);
            });
        });
    });
}

/**
 * @brief パッケージの変更を非同期でコミットする
 *
 * m_commitRevisionに束縛してCommitを呼び、呼び出し後は0に戻す
 * リビジョン未確定時は進捗ダイアログ整合のため失敗結果を設定して終了する
 *
 * 結果はcommitResultChangedシグナルで通知される
 */
void PackageController::commitAsync()
{
    // 未接続ではD-Busコミットを開始できないため、確認状態を消費しない
    if (!m_connected) {
        return;
    }

    // 進行中の操作と同時にコミットすると選択改訂が無効になるため、待機する
    if (m_busy) {
        return;
    }
    m_retryAction = [this]() { prepareCommit(); };

    if (m_commitRevision == 0) {
        const QString msg = tr("Changes must be confirmed before applying");
        emit errorOccurred(msg);
        m_commitResult = QVariantMap{
            {QStringLiteral("success"), false},
            {QStringLiteral("errorMessage"), msg},
        };
        emit commitResultChanged();
        setBusy(false);
        return;
    }

    setBusy(true);
    setStatusMessage(tr("Applying changes..."));
    const quint64 rev = m_commitRevision;
    m_commitRevision = 0;

    // 結果表示には今回のライセンス拒否を保持するため、確認状態を破棄する前に複製する
    const auto excluded = m_rejectedLicensePackages;
    // DBusClient::commitAsyncで確認済み改訂をバックエンドへ渡し、完了結果をQMLプロパティへ反映する
    m_dbus->commitAsync(rev, [this, excluded](bool ok, const QString &, const QString &, const QVariantList &args) {
        m_commitResult = (ok && !args.isEmpty()) ? args.at(0).toMap() : QVariantMap();
        m_commitResult[QStringLiteral("rejectedLicensePackages")] = excluded;
        if (!m_commitResult.contains(QStringLiteral("success"))) {
            m_commitResult[QStringLiteral("success")] = false;
        }
        emit commitResultChanged();

        if (m_commitResult[QStringLiteral("success")].toBool()) {
            setStatusMessage(tr("Changes applied successfully"));
        }
        else {
            setStatusMessage(tr("Some operations encountered errors"));
        }

        // コミット後は一覧の内容が大きく変わるため、新しい世代として通知する
        refreshPackageStatuses(true);
        loadPatterns();
        refreshPendingChanges();
        // コミット完了後の更新可能数を解放時に1回だけ取得する
        m_updateCountPending = true;
        setBusy(false);
    });
}

// -- 状態保存・復元 --

/**
 * @brief パッケージの選択状態を保存する (非同期・fire-and-forget)
 */
void PackageController::saveState()
{
    if (m_busy) return;
    m_dbus->saveState();
}

/**
 * @brief 保存された選択状態を非同期で復元し、パッケージリストを再取得する
 */
void PackageController::restoreState()
{
    if (m_busy) return;

    m_dbus->restoreState([this](bool ok, const QString &, const QString &, const QVariantList &) {
        if (!ok && !m_dbus->lastError().isEmpty())
            emit errorOccurred(m_dbus->lastError());
        refreshPackageStatuses();
        refreshPendingChanges();
    });
}

// -- ローカルRPM --

/**
 * @brief ローカルRPMファイルをインストール対象として選択し、依存関係を解決する
 *
 * 結果はlocalRpmsFinishedで通知される (success / problems / summary / unsignedPackages)
 *
 * @param fileUrls ファイルダイアログで選択されたfile:// URLの一覧
 */
void PackageController::installLocalRpms(const QVariantList &fileUrls)
{
    if (!m_connected) return;
    if (m_busy) return;

    // ファイルはユーザ権限でUIが開き、記述子だけをバックエンドへ渡す (rootプロセスにユーザ指定パスを開かせない)
    // O_NONBLOCKでFIFOによる停止を防ぐ
    QList<QDBusUnixFileDescriptor> files;
    QStringList fileNames;
    for (const QVariant &value : fileUrls) {
        const QUrl url = value.toUrl();
        const QString path = url.isLocalFile() ? url.toLocalFile() : value.toString();
        if (path.isEmpty())
            continue;
        const int fd = ::open(QFile::encodeName(path).constData(), O_RDONLY | O_CLOEXEC | O_NONBLOCK);
        if (fd < 0) {
            QVariantMap failed;
            failed[QStringLiteral("success")] = false;
            failed[QStringLiteral("errorMessage")] =
                tr("Cannot open file: %1").arg(path) + QStringLiteral(" (") + QString::fromLocal8Bit(std::strerror(errno)) + QStringLiteral(")");
            emit localRpmsFinished(failed);
            return;
        }
        files.append(QDBusUnixFileDescriptor(fd));   // 記述子は複製される
        ::close(fd);
        fileNames.append(QFileInfo(path).fileName());
    }
    if (files.isEmpty())
        return;

    setBusy(true);
    setStatusMessage(tr("Adding local RPM files..."));

    m_dbus->installLocalRpms(files, fileNames, [this](bool ok, const QString &, const QString &message,
                                           const QVariantList &args) {
        QVariantMap result = (ok && !args.isEmpty()) ? args.at(0).toMap() : QVariantMap();
        if (!ok) {
            result[QStringLiteral("success")] = false;
            result[QStringLiteral("errorMessage")] = message;
        }

        refreshPackageStatuses(true);
        cacheTransactionSummary(result);

        if (result.value(QStringLiteral("success")).toBool())
            setStatusMessage(tr("Local RPM files selected for installation"));
        else if (!ok)
            setStatusMessage(tr("Failed to add local RPM files"));
        else
            setStatusMessage(tr("Dependency problems found"));

        setBusy(false);
        // 変更予定一覧の更新完了後に通知する (古い一覧で判定しないため)
        refreshPendingChangesThen([this, result](bool refreshed) {
            emit localRpmsFinished(resultAfterPendingRefresh(result, refreshed, m_connected));
        });
    });
}

} // namespace qZypper
