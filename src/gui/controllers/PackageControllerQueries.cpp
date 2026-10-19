#include <utility>
#include "PackageController.h"
#include "DBusClient.h"
#include "PackageListModel.h"

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

    requestPackageList(true, [this](bool ok, const QVariantList &list) {
        setBusy(false);
        if (ok)
            setStatusMessage(tr("%1 packages found").arg(list.size()));
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
 */
void PackageController::searchPackagesEx(const QString &query, int flags,
                                          const QString &match, bool installedOnly)
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
    m_useExSearch = true;
    setSearchError(QString());

    requestPackageList(true, [this](bool ok, const QVariantList &list) {
        setBusy(false);
        if (ok)
            setStatusMessage(tr("%1 packages found").arg(list.size()));
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
 * リクエストシリアルを進め、古い応答は破棄する
 * 要求の連打は、最新の要求のみが適用されるためUIへの反映が乱れない
 *
 * @param newGeneration 取得後に世代カウンタを進める場合true
 * @param onDone 完了コールバック (成功可否と取得済みリスト)
 */
void PackageController::requestPackageList(bool newGeneration,
                                           std::function<void(bool, const QVariantList &)> onDone)
{
    ++m_listRequestSerial;
    const quint64 serial = m_listRequestSerial;

    auto apply = [this, serial, newGeneration, onDone](
                     bool ok, const QString &errorName, const QString &errorText, const QVariantList &args) {
        if (serial != m_listRequestSerial) {
            if (onDone) onDone(false, QVariantList());
            return;
        }
        if (!ok) {
            if (m_lastSource == PackageSource::Search
                && errorName.contains(QStringLiteral("InvalidArgs")))
                setSearchError(errorText);
            if (onDone) onDone(false, QVariantList());
            return;
        }
        setSearchError(QString());
        m_packages = args.isEmpty() ? QVariantList() : args.at(0).toList();
        m_packageModel->applyPackages(m_packages);
        if (newGeneration)
            ++m_listGeneration;
        emit packagesChanged();
        if (onDone) onDone(true, m_packages);
    };

    switch (m_lastSource) {
    case PackageSource::Search:
        if (m_useExSearch) {
            QVariantMap opts;
            opts[QStringLiteral("query")] = m_lastSourceParam;
            opts[QStringLiteral("flags")] = m_lastSearchFlags;
            opts[QStringLiteral("match")] = m_lastSearchMatch;
            opts[QStringLiteral("installedOnly")] = m_lastSearchInstalledOnly;
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
    case PackageSource::None:
        break;
    }
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

    requestPackageList(true, [this](bool ok, const QVariantList &list) {
        setBusy(false);
        if (ok)
            setStatusMessage(tr("%1 packages loaded").arg(list.size()));
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

    requestPackageList(true, [this](bool ok, const QVariantList &list) {
        setBusy(false);
        if (ok)
            setStatusMessage(tr("%1 packages loaded").arg(list.size()));
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

    requestPackageList(true, [this](bool ok, const QVariantList &list) {
        setBusy(false);
        if (ok)
            setStatusMessage(tr("%1 packages loaded").arg(list.size()));
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
            return;   // 古い応答は破棄
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
 * 実行中に再度呼ばれた場合は合体 (coalesce) し、2重要求を抑止する
 * hasPendingChangesは本処理でのみ更新され、ゲッターはI/Oを行わない
 */
void PackageController::refreshPendingChanges()
{
    if (!m_connected) {
        m_pendingRefreshInFlight = false;
        m_pendingRefreshQueued = false;
        if (!m_pendingChanges.isEmpty() || m_hasPendingChanges) {
            m_pendingChanges.clear();
            m_hasPendingChanges = false;
            emit pendingChangesChanged();
        }
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
        m_pendingRefreshInFlight = false;

        if (ok) {
            m_pendingChanges = args.isEmpty() ? QVariantList() : args.at(0).toList();
            m_hasPendingChanges = !m_pendingChanges.isEmpty();
            emit pendingChangesChanged();
        }

        if (m_pendingRefreshQueued) {
            m_pendingRefreshQueued = false;
            refreshPendingChanges();
            return;
        }

        // 最新の要求まで反映済み — 待機者へ取得成否を通知
        auto waiters = std::move(m_pendingRefreshWaiters);
        m_pendingRefreshWaiters.clear();
        for (auto &waiter : waiters)
            waiter(ok);
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

} // namespace qZypper
