/**
 * @file PackageControllerQueries.cpp
 * @brief パッケージ一覧、詳細、変更予定、履歴などの読み取り処理の実装
 *
 * 検索、リポジトリ別・パターン別・分類別の一覧取得、変更予定とディスク使用量、
 * 履歴、ソルバー設定、RPMグループ、言語サポートを非同期で取得する
 * 取得結果はキャッシュとQMLプロパティへ反映する
 *
 * 入力 : QMLからの読み取り要求、DBusClientの完了コールバック
 * 出力 : 一覧キャッシュの更新と対応する変更シグナル
 * 依存 : Qt Core、DBusClient、PackageListModel、PackageFilterProxyModel、SearchModeDetect
 */
#include <utility>
#include <QDebug>
#include "PackageController.h"
#include "DBusClient.h"
#include "PackageListModel.h"
#include "PackageFilterProxyModel.h"
#include "SearchModeDetect.h"

namespace qZypper {

// -- パッケージ検索・一覧 --

/**
 * @brief パッケージを検索する (非同期)
 * @param query 検索クエリ文字列
 * @param flags 検索フラグ (SearchFlagのビットマスク)
 */
void PackageController::searchPackages(const QString &query, int flags)
{
    if (!m_connected) {
        emit errorOccurred(tr("Package manager is not initialized"));
        return;
    }
    if (m_busy) return;

    setBusy(true);
    setStatusMessage(tr("Searching packages..."));

    m_lastSource = PackageSource::Search;
    m_lastSourceParam = query;
    m_lastSearchFlags = flags;
    m_useExSearch = false;
    m_lastSearchMatch = QStringLiteral("substring");
    m_lastSearchInstalledOnly = false;
    setSearchError(QString());

    requestPackageList(true, [this](bool ok, const QVariantList &) {
        setBusy(false);
        if (ok) {
            // 表に表示中の行数を反映する (-devel/-debuginfo等の非表示行は除外される)
            setStatusMessage(tr("%1 packages found").arg(m_packageProxy->rowCount()));
        } else {
            // 検索失敗時に「Searching packages...」が残らないよう、失敗を通知する
            setStatusMessage(tr("Search failed"));
        }
    });
}

/**
 * @brief 高度なパッケージ検索を行う (非同期)
 *
 * マッチ方式 (substring/exact/glob/regex) とインストール済み限定を指定できる
 * 正則表現エラー等のInvalidArgsはsearchErrorプロパティへ通知される
 *
 * @param query 検索クエリ文字列
 * @param flags 検索フラグ (SearchFlagのビットマスク)
 * @param match マッチ方式
 * @param installedOnly trueでインストール済みパッケージのみ
 * @param caseSensitive trueで大文字小文字を区別する
 */
void PackageController::searchPackagesEx(const QString &query, int flags,
                                          const QString &match, bool installedOnly,
                                          bool caseSensitive)
{
    if (!m_connected) {
        emit errorOccurred(tr("Package manager is not initialized"));
        return;
    }
    if (m_busy) return;

    setBusy(true);
    setStatusMessage(tr("Searching packages..."));

    m_lastSource = PackageSource::Search;
    m_lastSourceParam = query;
    m_lastSearchFlags = flags;
    m_lastSearchMatch = match;
    m_lastSearchInstalledOnly = installedOnly;
    m_lastSearchCaseSensitive = caseSensitive;
    m_useExSearch = true;
    setSearchError(QString());

    requestPackageList(true, [this](bool ok, const QVariantList &) {
        setBusy(false);
        if (ok) {
            // 表に表示中の行数を反映する (-devel/-debuginfo等の非表示行は除外される)
            setStatusMessage(tr("%1 packages found").arg(m_packageProxy->rowCount()));
        } else {
            // 検索失敗時に「Searching packages...」が残らないよう、失敗を通知する
            setStatusMessage(tr("Search failed"));
        }
    });
}

/**
 * @brief 検索パラメータエラーを設定して通知する
 * @param err エラー文言 (空文字列でクリア)
 */
void PackageController::setSearchError(const QString &err)
{
    if (m_searchError == err)
        return;
    m_searchError = err;
    emit searchErrorChanged();
}

/**
 * @brief 現在のソースに応じて、パッケージ一覧を非同期で再取得する
 *
 * リクエストシリアルを進め、後続要求がある場合は最新の応答のみを適用する
 * 置き換えられた要求の完了通知は、最新の取得結果でまとめて行う
 *
 * @param newGeneration 取得後に世代カウンタを進める場合はtrue
 * @param onDone 完了コールバック (成功可否と取得済みリスト)
 */
void PackageController::requestPackageList(bool newGeneration,
                                           std::function<void(bool, const QVariantList &)> onDone)
{
    ++m_listRequestSerial;
    const quint64 serial = m_listRequestSerial;
    if (onDone) {
        m_listWaiters.emplace_back(serial, std::move(onDone));
    }

    // 取得元が未確定の場合は再取得する一覧が無いため、待機者へ現状を通知して終える
    if (m_lastSource == PackageSource::None) {
        finishListRequest(serial, true, m_packages);
        return;
    }

    auto apply = [this, serial, newGeneration](bool ok, const QString &errorName,
                                               const QString &errorText, const QVariantList &args) {
        // 後続の要求がある場合は最新の応答を待ち、その完了時にまとめて通知する
        if (serial != m_listRequestSerial) {
            return;
        }
        if (!ok) {
            if (m_lastSource == PackageSource::Search
                && errorName.contains(QStringLiteral("InvalidArgs"))) {
                setSearchError(errorText);
            }
            finishListRequest(serial, false, QVariantList());
            return;
        }
        setSearchError(QString());
        m_packages = args.isEmpty() ? QVariantList() : args.at(0).toList();
        m_packageModel->applyPackages(m_packages);
        if (newGeneration) {
            ++m_listGeneration;
        }
        emit packagesChanged();
        // 更新可能一覧を表示した場合は一覧件数を更新可能数として反映する
        if (m_lastSource == PackageSource::Classification
            && m_lastSourceParam == QStringLiteral("updatable")) {
            setUpdateCount(m_packages.size());
        }
        finishListRequest(serial, true, m_packages);
    };

    switch (m_lastSource) {
    case PackageSource::Search:
        if (m_useExSearch) {
            QVariantMap opts;
            opts[QStringLiteral("query")] = m_lastSourceParam;
            opts[QStringLiteral("flags")] = m_lastSearchFlags;
            opts[QStringLiteral("match")] = m_lastSearchMatch;
            opts[QStringLiteral("installedOnly")] = m_lastSearchInstalledOnly;
            // 検索時の大文字小文字区別を再取得でも維持する
            opts[QStringLiteral("caseSensitive")] = m_lastSearchCaseSensitive;
            m_dbus->searchPackagesEx(opts, apply);
        } else {
            m_dbus->searchPackages(m_lastSourceParam, m_lastSearchFlags, apply);
        }
        break;
    case PackageSource::Repo:
        m_dbus->getPackagesByRepo(m_lastSourceParam, apply);
        break;
    case PackageSource::Pattern:
        m_dbus->getPackagesByPattern(m_lastSourceParam, apply);
        break;
    case PackageSource::Patches:
        m_dbus->getPatches(m_lastSourceParam.toInt(), apply);
        break;
    case PackageSource::Classification:
        m_dbus->getPackagesByClassification(m_lastSourceParam, apply);
        break;
    case PackageSource::RpmGroup:
        m_dbus->getPackagesByRpmGroup(m_lastSourceParam, apply);
        break;
    case PackageSource::Locale:
        m_dbus->getPackagesByLocale(m_lastSourceParam, apply);
        break;
    case PackageSource::None:
        break;
    }
}

/**
 * @brief 応答番号以前の待機者へ一覧取得の完了を通知する
 * @param replySerial 応答に対応する要求番号
 * @param ok 取得に成功した場合はtrue
 * @param list 取得したパッケージ一覧
 *
 * 通知中に発生した新しい要求の待機者は、その応答が届くまで残す
 * 通知後に最新の要求番号を確認し、追い越された待機者だけを解放する
 */
void PackageController::finishListRequest(quint64 replySerial, bool ok, const QVariantList &list)
{
    // 通知中に追加される可能性があるため、対象の待機者を先に取り出す
    std::vector<std::pair<quint64, std::function<void(bool, const QVariantList &)>>> ready;
    auto it = m_listWaiters.begin();
    while (it != m_listWaiters.end()) {
        if (it->first <= replySerial) {
            ready.push_back(std::move(*it));
            it = m_listWaiters.erase(it);
            continue;
        }
        ++it;
    }
    for (auto &waiter : ready) {
        if (waiter.second) {
            waiter.second(ok, list);
        }
    }
}

/**
 * @brief 切断時に一覧取得の待機者を通知せず破棄する
 *
 * 待機者の完了処理はbusy解除を行うため、ここで通知すると切断処理の解放と二重になる
 * 送信済みの応答を無効化するためシリアルだけを進め、解放は切断処理が1回だけ行う
 */
void PackageController::abortListRequests()
{
    ++m_listRequestSerial;
    m_listWaiters.clear();
}

/**
 * @brief パッケージ一覧を現在のソースで再取得する (サイレント)
 * @param newGeneration 取得後に世代カウンタを進める場合はtrue
 */
void PackageController::refreshPackageStatuses(bool newGeneration)
{
    requestPackageList(newGeneration, nullptr);
}

/**
 * @brief 指定リポジトリのパッケージ一覧を読み込む (非同期)
 * @param repoAlias リポジトリエイリアス
 */
void PackageController::loadPackagesByRepo(const QString &repoAlias)
{
    if (!m_connected) return;
    if (m_busy) return;

    setBusy(true);
    setStatusMessage(tr("Loading packages..."));

    m_lastSource = PackageSource::Repo;
    m_lastSourceParam = repoAlias;

    requestPackageList(true, [this](bool ok, const QVariantList &) {
        setBusy(false);
        if (ok) {
            // 表に表示中の行数を反映する (-devel/-debuginfo等の非表示行は除外される)
            setStatusMessage(tr("%1 packages loaded").arg(m_packageProxy->rowCount()));
        }
    });
}

/**
 * @brief パッケージの詳細情報を読み込む (非同期)
 * @param name パッケージ名
 */
void PackageController::loadPackageDetails(const QString &name)
{
    if (!m_connected) return;

    ++m_detailsRequestSerial;
    const quint64 serial = m_detailsRequestSerial;

    m_dbus->getPackageDetails(name, [this, serial](
                                   bool ok, const QString &, const QString &, const QVariantList &args) {
        if (serial != m_detailsRequestSerial || !ok)
            return;
        m_currentDetails = args.isEmpty() ? QVariantMap() : args.at(0).toMap();
        emit currentPackageDetailsChanged();
    });
}

/**
 * @brief 選択中パッケージの詳細表示をクリアする
 *
 * パッケージ一覧から選択中のパッケージが無くなった場合に使用する
 * 保留中の詳細応答は無効化する
 */
void PackageController::clearPackageDetails()
{
    ++m_detailsRequestSerial;
    if (m_currentDetails.isEmpty()) return;

    m_currentDetails.clear();
    emit currentPackageDetailsChanged();
}

/**
 * @brief 分類別パッケージ一覧を読み込む (非同期)
 * @param classification 分類名 (recommended/suggested/orphaned/unneeded/multiversion/retracted/retractedInstalled/ptf/locked)
 */
void PackageController::loadClassification(const QString &classification)
{
    if (!m_connected) return;
    if (m_busy) return;

    setBusy(true);
    setStatusMessage(tr("Loading packages..."));

    m_lastSource = PackageSource::Classification;
    m_lastSourceParam = classification;
    m_useExSearch = false;

    requestPackageList(true, [this](bool ok, const QVariantList &) {
        setBusy(false);
        if (ok) {
            // 表に表示中の行数を反映する (-devel/-debuginfo等の非表示行は除外される)
            setStatusMessage(tr("%1 packages loaded").arg(m_packageProxy->rowCount()));
        }
    });
}

// -- パターン・パッチ一覧 --

/**
 * @brief パターン一覧を読み込む (非同期)
 */
void PackageController::loadPatterns()
{
    if (!m_connected) return;

    m_dbus->getPatterns([this](bool ok, const QString &, const QString &, const QVariantList &args) {
        if (!ok)
            return;
        m_patterns = args.isEmpty() ? QVariantList() : args.at(0).toList();
        emit patternsChanged();
    });
}

/**
 * @brief 製品一覧を読み込む (非同期、読み取り専用)
 *
 * Productsタブを初めて表示したときに呼ばれ、以降はプール再読込時にのみ更新する
 *
 * @note 応答はシグナル経由で反映し、この関数は待機しない
 */
void PackageController::loadProducts()
{
    // 未接続時はD-Busを呼べないため何もしない
    if (!m_connected) {
        return;
    }

    // 応答前に再呼び出しされても二重取得しないよう読み込み済みとして記録する
    m_productsLoaded = true;

    // DBusClient::getProductsでバックエンドへ非同期に問い合わせる
    m_dbus->getProducts([this](bool ok, const QString &, const QString &, const QVariantList &args) {
        // 失敗時は直前のキャッシュを維持する
        if (!ok) {
            return;
        }

        m_products = args.isEmpty() ? QVariantList() : args.at(0).toList();

        // 製品一覧が更新されたことをQMLへ通知する
        emit productsChanged();
    });
}

/**
 * @brief 指定パターンに属するパッケージ一覧を読み込む (非同期)
 * @param patternName パターン名
 */
void PackageController::loadPackagesByPattern(const QString &patternName)
{
    if (!m_connected) return;
    if (m_busy) return;

    setBusy(true);
    setStatusMessage(tr("Loading pattern packages..."));

    m_lastSource = PackageSource::Pattern;
    m_lastSourceParam = patternName;

    requestPackageList(true, [this](bool ok, const QVariantList &) {
        setBusy(false);
        if (ok) {
            // 表に表示中の行数を反映する (-devel/-debuginfo等の非表示行は除外される)
            setStatusMessage(tr("%1 packages loaded").arg(m_packageProxy->rowCount()));
        }
    });
}

/**
 * @brief パッチ一覧を読み込む (非同期)
 * @param category パッチカテゴリ (PatchCategory)
 */
void PackageController::loadPatches(int category)
{
    if (!m_connected) return;

    // パッチはパッケージ一覧モデルとは別に保持する (パッケージ表へ混入させない)
    m_patchCategory = category;
    m_dbus->getPatches(category, [this, category](bool ok, const QString &, const QString &errorText,
                                                  const QVariantList &args) {
        if (category != m_patchCategory)
            return;  // 古い応答は破棄
        if (!ok) {
            emit errorOccurred(errorText);
            return;
        }
        m_patches = args.isEmpty() ? QVariantList() : args.at(0).toList();
        emit patchesChanged();
    });
}

// -- 変更予定・ディスク使用量 --

/**
 * @brief 変更予定のパッケージ一覧を非同期で再取得する (キャッシュ更新)
 *
 * 実行中に再度呼ばれた場合は要求を合体し、二重要求を抑止する
 * hasPendingChangesは本処理でのみ更新され、ゲッターはI/Oを行わない
 */
void PackageController::refreshPendingChanges()
{
    if (!m_connected) {
        m_pendingRefreshInFlight = false;
        m_pendingRefreshQueued = false;
        const bool localeChangesChanged = !m_pendingLocaleChanges.isEmpty();
        if (!m_pendingChanges.isEmpty() || localeChangesChanged || m_hasPendingChanges) {
            m_pendingChanges.clear();
            m_pendingLocaleChanges.clear();
            m_hasPendingChanges = false;
            emit pendingChangesChanged();
        }
        if (localeChangesChanged)
            emit pendingLocaleChangesChanged();
        auto waiters = std::move(m_pendingRefreshWaiters);
        m_pendingRefreshWaiters.clear();
        for (auto &waiter : waiters)
            waiter(false);
        return;
    }

    if (m_pendingRefreshInFlight) {
        m_pendingRefreshQueued = true;
        return;
    }

    m_pendingRefreshInFlight = true;
    m_dbus->getPendingChanges([this](bool ok, const QString &, const QString &, const QVariantList &args) {
        if (!ok) {
            m_pendingRefreshInFlight = false;

            if (m_pendingRefreshQueued) {
                m_pendingRefreshQueued = false;
                refreshPendingChanges();
                return;
            }

            auto waiters = std::move(m_pendingRefreshWaiters);
            m_pendingRefreshWaiters.clear();
            for (auto &waiter : waiters)
                waiter(false);
            return;
        }

        const QVariantList pendingChanges = args.isEmpty() ? QVariantList() : args.at(0).toList();
        m_dbus->getPendingLocaleChanges([this, pendingChanges](bool localeOk, const QString &localeErrorName,
                                                                 const QString &, const QVariantList &localeArgs) {
            m_pendingRefreshInFlight = false;

            const bool localeMethodUnavailable = localeErrorName
                    == QStringLiteral("org.freedesktop.DBus.Error.UnknownMethod");
            const bool refreshOk = localeOk || localeMethodUnavailable;
            bool localeChangesChanged = false;
            if (refreshOk) {
                const QVariantList pendingLocaleChanges = localeOk && !localeArgs.isEmpty()
                                                           ? localeArgs.at(0).toList() : QVariantList();
                localeChangesChanged = m_pendingLocaleChanges != pendingLocaleChanges;
                m_pendingLocaleChanges = pendingLocaleChanges;
            }
            m_pendingChanges = pendingChanges;
            m_hasPendingChanges = !m_pendingChanges.isEmpty() || !m_pendingLocaleChanges.isEmpty();
            emit pendingChangesChanged();
            if (localeChangesChanged)
                emit pendingLocaleChangesChanged();

            if (m_pendingRefreshQueued) {
                m_pendingRefreshQueued = false;
                refreshPendingChanges();
                return;
            }

            // 最新の要求まで反映したため、待機者へ取得成否を通知する
            auto waiters = std::move(m_pendingRefreshWaiters);
            m_pendingRefreshWaiters.clear();
            for (auto &waiter : waiters)
                waiter(refreshOk);
        });
    });
}

/**
 * @brief 変更予定を再取得し、呼出し時点以降の状態が反映された後にonDoneを呼ぶ
 *
 * 取得中の要求は呼出し前の選択状態を返す可能性があるため、その場合は再取得を予約し、予約分の完了まで待機する
 *
 * @param onDone 完了時に呼ばれるコールバック (引数は取得成否、失敗・切断時はfalse)
 */
void PackageController::refreshPendingChangesThen(std::function<void(bool)> onDone)
{
    if (onDone)
        m_pendingRefreshWaiters.push_back(std::move(onDone));
    if (m_pendingRefreshInFlight)
        m_pendingRefreshQueued = true;
    else
        refreshPendingChanges();
}

/**
 * @brief ディスク使用量を非同期で再取得する (キャッシュ更新)
 *
 * 完了後、diskUsageChangedシグナルが発行される
 */
void PackageController::requestDiskUsage()
{
    if (!m_connected) return;

    m_dbus->getDiskUsage([this](bool ok, const QString &, const QString &, const QVariantList &args) {
        if (!ok)
            return;
        m_diskUsage = args.isEmpty() ? QVariantList() : args.at(0).toList();
        emit diskUsageChanged();
    });
}

// -- ヒストリ --

/**
 * @brief zyppインストール履歴を読み込む (非同期)
 * @param filter フィルタマップ (since/until/limit/offset/actions)
 */
void PackageController::loadHistory(const QVariantMap &filter)
{
    if (!m_connected) return;

    m_dbus->getHistory(filter, [this](bool ok, const QString &, const QString &errorText, const QVariantList &args) {
        if (!ok) {
            emit errorOccurred(errorText);
            emit historyResultReady(QVariantList());
            return;
        }
        emit historyResultReady(args.isEmpty() ? QVariantList() : args.at(0).toList());
    });
}

/**
 * @brief zypp履歴をコマンド単位のグループで読み込む (非同期)
 * @param options 絞り込みマップ (since/until: エポックミリ秒、limit、offset、actions、text)
 * @param requestId 呼び出し側の世代番号 (結果シグナルへそのまま返す)
 * @note 完了はhistoryGroupsReadyシグナルで通知する (失敗時は空マップ、errorOccurredも発行する)
 */
void PackageController::loadHistoryGroups(const QVariantMap &options, int requestId)
{
    // バックエンド未接続の間は呼び出せないため、失敗として即座に通知する
    if (!m_connected) {
        emit historyGroupsReady(requestId, QVariantMap());
        return;
    }

    // DBusClient::getHistoryGroupsを呼び出し、完了は非同期コールバックで受け取ってhistoryGroupsReadyで通知する
    m_dbus->getHistoryGroups(options, [this, requestId](bool ok, const QString &, const QString &errorText,
                                                        const QVariantList &args) {
        // 上限超過・取消し・通信失敗は、エラー通知と空マップで呼び出し側に失敗を知らせる
        if (!ok) {
            emit errorOccurred(errorText);
            emit historyGroupsReady(requestId, QVariantMap());
            return;
        }

        emit historyGroupsReady(requestId, args.isEmpty() ? QVariantMap() : args.at(0).toMap());
    });
}

// -- ソルバー設定・RPMグループ・ロケール --

/**
 * @brief ソルバー設定を読み込む (非同期)
 *
 * D-BusのGetSolverSettingsを呼び出す
 * 成功時はsolverSettingsを更新してsolverSettingsChangedを発行する
 * 未対応バックエンドでは設定を空にしてエラーを通知しない
 */
void PackageController::loadSolverSettings()
{
    if (!m_connected) {
        return;
    }

    m_dbus->getSolverSettings([this](bool ok, const QString &errorName,
                                      const QString &errorText, const QVariantList &args) {
        if (!ok) {
            if (errorName == QLatin1String("org.freedesktop.DBus.Error.UnknownMethod")) {
                m_solverSettings.clear();
                emit solverSettingsChanged();
                return;
            }
            emit errorOccurred(errorText);
            return;
        }
        m_solverSettings = args.isEmpty() ? QVariantMap() : args.at(0).toMap();
        emit solverSettingsChanged();
    });
}

/**
 * @brief RPMグループのツリーを読み込む (非同期)
 *
 * D-BusのGetRpmGroupsを呼び出す
 * 成功時はrpmGroupsを更新してrpmGroupsChangedを発行する
 * 未対応バックエンドでは空の一覧にしてエラーを通知しない
 */
void PackageController::loadRpmGroups()
{
    if (!m_connected) {
        return;
    }

    m_dbus->getRpmGroups([this](bool ok, const QString &errorName,
                                 const QString &errorText, const QVariantList &args) {
        if (!ok) {
            if (errorName == QLatin1String("org.freedesktop.DBus.Error.UnknownMethod")) {
                m_rpmGroups.clear();
                emit rpmGroupsChanged();
                return;
            }
            emit errorOccurred(errorText);
            return;
        }
        m_rpmGroups = args.isEmpty() ? QVariantList() : args.at(0).toList();
        emit rpmGroupsChanged();
    });
}

/**
 * @brief 指定RPMグループのパッケージ一覧を読み込む (非同期)
 * @param path RPMグループのパス
 *
 * D-BusのGetPackagesByRpmGroupを呼び出す
 * 一覧取得後にstatusMessageとbusyを更新する
 */
void PackageController::loadPackagesByRpmGroup(const QString &path)
{
    if (!m_connected) {
        return;
    }
    if (m_busy) {
        return;
    }

    setBusy(true);
    setStatusMessage(tr("Loading packages..."));

    m_lastSource = PackageSource::RpmGroup;
    m_lastSourceParam = path;
    m_useExSearch = false;

    requestPackageList(true, [this](bool ok, const QVariantList &) {
        setBusy(false);
        if (ok) {
            // 表に表示中の行数を反映する (-devel/-debuginfo等の非表示行は除外される)
            setStatusMessage(tr("%1 packages loaded").arg(m_packageProxy->rowCount()));
        }
    });
}

/**
 * @brief 言語サポート一覧を読み込む (非同期)
 *
 * D-BusのGetLocalesを呼び出す
 * 成功時はlocalesを更新してlocalesChangedを発行する
 * 未対応バックエンドでは空の一覧にしてエラーを通知しない
 */
void PackageController::loadLocales()
{
    if (!m_connected) {
        return;
    }

    m_dbus->getLocales([this](bool ok, const QString &errorName,
                               const QString &errorText, const QVariantList &args) {
        if (!ok) {
            if (errorName == QLatin1String("org.freedesktop.DBus.Error.UnknownMethod")) {
                m_locales.clear();
                emit localesChanged();
                return;
            }
            emit errorOccurred(errorText);
            return;
        }
        m_locales = args.isEmpty() ? QVariantList() : args.at(0).toList();
        emit localesChanged();
    });
}

/**
 * @brief 指定ロケールのパッケージ一覧を読み込む (非同期)
 * @param code ロケールコード
 *
 * D-BusのGetPackagesByLocaleを呼び出す
 * 一覧取得後にstatusMessageとbusyを更新する
 */
void PackageController::loadPackagesByLocale(const QString &code)
{
    if (!m_connected) {
        return;
    }
    if (m_busy) {
        return;
    }

    setBusy(true);
    setStatusMessage(tr("Loading packages..."));

    m_lastSource = PackageSource::Locale;
    m_lastSourceParam = code;
    m_useExSearch = false;

    requestPackageList(true, [this](bool ok, const QVariantList &) {
        setBusy(false);
        if (ok) {
            // 表に表示中の行数を反映する (-devel/-debuginfo等の非表示行は除外される)
            setStatusMessage(tr("%1 packages loaded").arg(m_packageProxy->rowCount()));
        }
    });
}

/**
 * @brief 更新可能パッケージ数を再取得する
 *
 * ビジー中は実行を保留してm_updateCountPendingを立てる
 * busy解除後に一度だけrequestUpdateCountが実行される
 * 一覧モデルには触れずupdateCountのみを更新する
 */
void PackageController::refreshUpdateCount()
{
    if (!m_connected) {
        setUpdateCount(-1);
        return;
    }
    if (m_busy) {
        m_updateCountPending = true;
        qDebug() << "[PackageController] refreshUpdateCount deferred (busy)";
        return;
    }

    requestUpdateCount();
}

/**
 * @brief 更新可能パッケージ数を取得する (非同期)
 *
 * ビジーガードを持たない内部処理でbusy解除後に直接呼び出す
 * D-BusのGetPackagesByClassification("updatable")を呼び出し
 * 結果件数のみをupdateCountへ反映する
 */
void PackageController::requestUpdateCount()
{
    m_updateCountPending = false;

    m_dbus->getPackagesByClassification(QStringLiteral("updatable"),
                                        [this](bool ok, const QString &errorName,
                                               const QString &, const QVariantList &args) {
        applyUpdateCountResult(ok, errorName, args);
    });
}

/**
 * @brief 更新可能数の取得結果を共通処理する
 * @param ok 取得に成功した場合はtrue
 * @param errorName D-Busエラー名
 * @param args 応答引数
 *
 * 未対応メソッドは不明 (-1) とし、その他の失敗はerrorOccurredを発行する
 */
void PackageController::applyUpdateCountResult(bool ok, const QString &errorName,
                                               const QVariantList &args)
{
    if (!ok) {
        if (errorName == QLatin1String("org.freedesktop.DBus.Error.UnknownMethod")) {
            setUpdateCount(-1);
            return;
        }
        emit errorOccurred(m_dbus->lastError());
        return;
    }
    setUpdateCount(args.isEmpty() ? 0 : args.at(0).toList().size());
}

/**
 * @brief 更新可能パッケージ数を設定する
 * @param count 更新可能パッケージ数 (-1は不明)
 *
 * 値が変化した場合のみupdateCountChangedを発行する
 */
void PackageController::setUpdateCount(int count)
{
    if (m_updateCount == count) {
        return;
    }

    m_updateCount = count;
    emit updateCountChanged();
}

/**
 * @brief 検索文字列から検索モードを自動判定し、QMLへ返す
 * @param text 検索欄に入力された文字列
 * @return [mode]と[query]を持つマップ
 *         [mode]はnone / exact / regex / glob / substringのいずれか
 *         [query]は検索へ渡す問い合わせ文字列 (判定用の接頭辞を除去したもの)
 * @note 判定はqZypper::detectSearchModeに委譲し、D-Bus呼び出しやメンバ変数の変更は行わない
 */
QVariantMap PackageController::detectSearchMode(const QString &text) const
{
    const DetectedSearch detected = qZypper::detectSearchMode(text);
    QVariantMap result;
    result.insert(QStringLiteral("mode"), detected.mode);
    result.insert(QStringLiteral("query"), detected.query);
    return result;
}

} // namespace qZypper
