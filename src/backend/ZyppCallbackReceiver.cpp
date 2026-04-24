#include "ZyppCallbackReceiver.h"

#include <algorithm>


namespace qZypper {

namespace {

/**
 * @brief 全体進捗率を計算するヘルパー。
 * @param completedSteps 完了済みステップ数
 * @param currentPercent 現在パッケージの進捗率 (0-100)
 * @param totalSteps 全ステップ数
 * @return 全体進捗率 (0-100)
 */
int calcOverall(int completedSteps, int currentPercent, int totalSteps)
{
    if (totalSteps <= 0) return 0;
    return std::min(100, (completedSteps * 100 + currentPercent) / totalSteps);
}

} // anonymous namespace

// ─── InstallReceiver ────────────────────────

/**
 * @brief InstallReceiver を構築する。
 * @param cb 進捗コールバック
 * @param completedSteps 完了ステップ数への参照
 * @param totalSteps 全ステップ数
 * @param cancelFlag キャンセルフラグ
 */
InstallReceiver::InstallReceiver(ProgressCallbackFn cb, StateEventCallbackFn stateCb,
                                 int &completedSteps,
                                 int totalSteps, const std::atomic<bool> &cancelFlag,
                                 std::string &problemDetail)
    : m_callback(std::move(cb))
    , m_stateCallback(std::move(stateCb))
    , m_completedSteps(completedSteps)
    , m_totalSteps(totalSteps)
    , m_cancelFlag(cancelFlag)
    , m_problemDetail(problemDetail)
{
}

/**
 * @brief パッケージインストール中のエラー対応コールバック。
 *
 * libzypp は問題発生時にこのコールバックで対応を問い合わせる。
 * デフォルト実装は ABORT を返すため override しないと
 * 軽微なエラーでもコミット全体が中断される。
 * インストールエラーはリトライしても解消しないため ABORT を返すが、
 * ユーザーに分かりやすいエラーメッセージを保存する。
 */
InstallReceiver::Action InstallReceiver::problem(
    zypp::Resolvable::constPtr resolvable,
    Error /*error*/, const std::string &description, RpmLevel /*level*/)
{
    std::string pkg = resolvable ? resolvable->name() : m_currentPkg;
    m_problemDetail = "Install failed: " + pkg + ": " + description;
    return ABORT;
}

/**
 * @brief パッケージインストール開始時のコールバック。
 * @param resolvable インストール対象パッケージ
 */
void InstallReceiver::start(zypp::Resolvable::constPtr resolvable)
{
    if (resolvable)
        m_currentPkg = resolvable->name();

    if (m_stateCallback)
        m_stateCallback(m_currentPkg, "install_start");

    if (m_callback) {
        CommitProgressInfo info;
        info.packageName     = m_currentPkg;
        info.percentage      = 0;
        info.stage           = "installing";
        info.totalSteps      = m_totalSteps;
        info.completedSteps  = m_completedSteps;
        info.overallPercentage = calcOverall(m_completedSteps, 0, m_totalSteps);
        m_callback(info);
    }
}

/**
 * @brief パッケージインストール進捗コールバック。
 * @param value 進捗率 (0-100)
 * @param resolvable インストール対象パッケージ
 * @return false でキャンセル
 */
bool InstallReceiver::progress(int value, zypp::Resolvable::constPtr /*resolvable*/)
{
    if (m_cancelFlag.load()) return false;

    if (m_callback) {
        CommitProgressInfo info;
        info.packageName     = m_currentPkg;
        info.percentage      = value;
        info.stage           = "installing";
        info.totalSteps      = m_totalSteps;
        info.completedSteps  = m_completedSteps;
        info.overallPercentage = calcOverall(m_completedSteps, value, m_totalSteps);
        m_callback(info);
    }
    return true;
}

/**
 * @brief パッケージインストール完了コールバック。
 */
void InstallReceiver::finish(zypp::Resolvable::constPtr /*resolvable*/,
                             Error /*error*/, const std::string &/*reason*/,
                             RpmLevel /*level*/)
{
    if (m_stateCallback)
        m_stateCallback(m_currentPkg, "install_end");
    ++m_completedSteps;
}

// ─── RemoveReceiver ─────────────────────────

/**
 * @brief RemoveReceiver を構築する。
 */
RemoveReceiver::RemoveReceiver(ProgressCallbackFn cb, StateEventCallbackFn stateCb,
                               int &completedSteps,
                               int totalSteps, const std::atomic<bool> &cancelFlag,
                               std::string &problemDetail)
    : m_callback(std::move(cb))
    , m_stateCallback(std::move(stateCb))
    , m_completedSteps(completedSteps)
    , m_totalSteps(totalSteps)
    , m_cancelFlag(cancelFlag)
    , m_problemDetail(problemDetail)
{
}

/**
 * @brief パッケージ削除中のエラー対応コールバック。
 *
 * 削除エラーはリトライでは解消しないため ABORT を返すが、
 * エラー詳細を保存してユーザーに提示する。
 */
RemoveReceiver::Action RemoveReceiver::problem(
    zypp::Resolvable::constPtr resolvable,
    Error /*error*/, const std::string &description)
{
    std::string pkg = resolvable ? resolvable->name() : m_currentPkg;
    m_problemDetail = "Remove failed: " + pkg + ": " + description;
    return ABORT;
}

/**
 * @brief パッケージ削除開始時のコールバック。
 * @param resolvable 削除対象パッケージ
 */
void RemoveReceiver::start(zypp::Resolvable::constPtr resolvable)
{
    if (resolvable)
        m_currentPkg = resolvable->name();

    if (m_stateCallback)
        m_stateCallback(m_currentPkg, "remove_start");

    if (m_callback) {
        CommitProgressInfo info;
        info.packageName     = m_currentPkg;
        info.percentage      = 0;
        info.stage           = "removing";
        info.totalSteps      = m_totalSteps;
        info.completedSteps  = m_completedSteps;
        info.overallPercentage = calcOverall(m_completedSteps, 0, m_totalSteps);
        m_callback(info);
    }
}

/**
 * @brief パッケージ削除進捗コールバック。
 * @param value 進捗率 (0-100)
 * @param resolvable 削除対象パッケージ
 * @return false でキャンセル
 */
bool RemoveReceiver::progress(int value, zypp::Resolvable::constPtr /*resolvable*/)
{
    if (m_cancelFlag.load()) return false;

    if (m_callback) {
        CommitProgressInfo info;
        info.packageName     = m_currentPkg;
        info.percentage      = value;
        info.stage           = "removing";
        info.totalSteps      = m_totalSteps;
        info.completedSteps  = m_completedSteps;
        info.overallPercentage = calcOverall(m_completedSteps, value, m_totalSteps);
        m_callback(info);
    }
    return true;
}

/**
 * @brief パッケージ削除完了コールバック。
 */
void RemoveReceiver::finish(zypp::Resolvable::constPtr /*resolvable*/,
                            Error /*error*/, const std::string &/*reason*/)
{
    if (m_stateCallback)
        m_stateCallback(m_currentPkg, "remove_end");
    ++m_completedSteps;
}

// ─── DownloadReceiver ───────────────────────

/**
 * @brief DownloadReceiver を構築する。
 */
DownloadReceiver::DownloadReceiver(ProgressCallbackFn cb, StateEventCallbackFn stateCb,
                                   int &completedSteps,
                                   int totalSteps, const std::atomic<bool> &cancelFlag,
                                   std::string &problemDetail)
    : m_callback(std::move(cb))
    , m_stateCallback(std::move(stateCb))
    , m_completedSteps(completedSteps)
    , m_totalSteps(totalSteps)
    , m_cancelFlag(cancelFlag)
    , m_problemDetail(problemDetail)
{
}

/**
 * @brief パッケージダウンロード中のエラー対応コールバック。
 *
 * libzypp のデフォルト実装は ABORT を返すため、
 * ネットワーク瞬断やIOエラーで即座にコミット全体が中断されてしまう。
 * パッケージごとに最大 kMaxRetries 回まで自動リトライし、
 * それでも失敗したら ABORT を返してユーザーに通知する。
 */
DownloadReceiver::Action DownloadReceiver::problem(
    zypp::Resolvable::constPtr resolvable,
    Error /*error*/, const std::string &description)
{
    if (m_cancelFlag.load())
        return ABORT;

    std::string pkg = resolvable ? resolvable->name() : m_currentPkg;
    if (m_retryCount < kMaxRetries) {
        ++m_retryCount;
        return RETRY;
    }
    m_problemDetail = "Download failed: " + pkg + ": " + description;
    m_retryCount = 0;
    return ABORT;
}

/**
 * @brief キャッシュ済みパッケージの通知コールバック。
 *
 * ダウンロード不要の場合、start/progress/finish の代わりにこれだけが呼ばれる。
 * ダウンロードステップを完了としてカウントする。
 * @param resolvable キャッシュ済みパッケージ
 * @param localfile ローカルファイルパス
 */
void DownloadReceiver::infoInCache(zypp::Resolvable::constPtr resolvable,
                                   const zypp::Pathname &/*localfile*/)
{
    if (m_stateCallback && resolvable)
        m_stateCallback(resolvable->name(), "cached");
    ++m_completedSteps;
}

/**
 * @brief パッケージダウンロード開始時のコールバック。
 * @param resolvable ダウンロード対象パッケージ
 * @param url ダウンロードURL
 */
void DownloadReceiver::start(zypp::Resolvable::constPtr resolvable,
                             const zypp::Url &/*url*/)
{
    if (resolvable)
        m_currentPkg = resolvable->name();
    m_retryCount = 0;  // パッケージ切替でリトライ回数をリセット

    if (m_stateCallback)
        m_stateCallback(m_currentPkg, "download_start");

    if (m_callback) {
        CommitProgressInfo info;
        info.packageName     = m_currentPkg;
        info.percentage      = 0;
        info.stage           = "downloading";
        info.totalSteps      = m_totalSteps;
        info.completedSteps  = m_completedSteps;
        info.overallPercentage = calcOverall(m_completedSteps, 0, m_totalSteps);
        m_callback(info);
    }
}

/**
 * @brief パッケージダウンロード進捗コールバック。
 * @param value 進捗率 (0-100)
 * @param resolvable ダウンロード対象パッケージ
 * @return false でキャンセル
 */
bool DownloadReceiver::progress(int value, zypp::Resolvable::constPtr /*resolvable*/)
{
    if (m_cancelFlag.load()) return false;

    if (m_callback) {
        CommitProgressInfo info;
        info.packageName     = m_currentPkg;
        info.percentage      = value;
        info.stage           = "downloading";
        info.totalSteps      = m_totalSteps;
        info.completedSteps  = m_completedSteps;
        info.overallPercentage = calcOverall(m_completedSteps, value, m_totalSteps);
        m_callback(info);
    }
    return true;
}

/**
 * @brief パッケージダウンロード完了コールバック。
 */
void DownloadReceiver::finish(zypp::Resolvable::constPtr /*resolvable*/,
                              Error /*error*/, const std::string &/*reason*/)
{
    if (m_stateCallback)
        m_stateCallback(m_currentPkg, "download_end");
    ++m_completedSteps;
}

// ─── InstallReceiverSA (SingleTransaction版) ─────

/**
 * @brief InstallReceiverSA を構築する。
 */
InstallReceiverSA::InstallReceiverSA(ProgressCallbackFn cb, StateEventCallbackFn stateCb,
                                     int &completedSteps,
                                     int totalSteps, const std::atomic<bool> &cancelFlag)
    : m_callback(std::move(cb))
    , m_stateCallback(std::move(stateCb))
    , m_completedSteps(completedSteps)
    , m_totalSteps(totalSteps)
    , m_cancelFlag(cancelFlag)
{
}

/**
 * @brief パッケージインストール開始 (SA版)。
 */
void InstallReceiverSA::start(zypp::Resolvable::constPtr resolvable,
                              const zypp::callback::UserData &/*userData*/)
{
    if (resolvable)
        m_currentPkg = resolvable->name();

    if (m_stateCallback)
        m_stateCallback(m_currentPkg, "install_start");

    if (m_callback) {
        CommitProgressInfo info;
        info.packageName     = m_currentPkg;
        info.percentage      = 0;
        info.stage           = "installing";
        info.totalSteps      = m_totalSteps;
        info.completedSteps  = m_completedSteps;
        info.overallPercentage = calcOverall(m_completedSteps, 0, m_totalSteps);
        m_callback(info);
    }
}

/**
 * @brief パッケージインストール進捗 (SA版)。
 */
void InstallReceiverSA::progress(int value, zypp::Resolvable::constPtr /*resolvable*/,
                                 const zypp::callback::UserData &/*userData*/)
{
    if (m_callback) {
        CommitProgressInfo info;
        info.packageName     = m_currentPkg;
        info.percentage      = value;
        info.stage           = "installing";
        info.totalSteps      = m_totalSteps;
        info.completedSteps  = m_completedSteps;
        info.overallPercentage = calcOverall(m_completedSteps, value, m_totalSteps);
        m_callback(info);
    }
}

/**
 * @brief パッケージインストール完了 (SA版)。
 */
void InstallReceiverSA::finish(zypp::Resolvable::constPtr /*resolvable*/,
                               Error /*error*/, const zypp::callback::UserData &/*userData*/)
{
    if (m_stateCallback)
        m_stateCallback(m_currentPkg, "install_end");
    ++m_completedSteps;
}

// ─── RemoveReceiverSA (SingleTransaction版) ──────

/**
 * @brief RemoveReceiverSA を構築する。
 */
RemoveReceiverSA::RemoveReceiverSA(ProgressCallbackFn cb, StateEventCallbackFn stateCb,
                                   int &completedSteps,
                                   int totalSteps, const std::atomic<bool> &cancelFlag)
    : m_callback(std::move(cb))
    , m_stateCallback(std::move(stateCb))
    , m_completedSteps(completedSteps)
    , m_totalSteps(totalSteps)
    , m_cancelFlag(cancelFlag)
{
}

/**
 * @brief パッケージ削除開始 (SA版)。
 */
void RemoveReceiverSA::start(zypp::Resolvable::constPtr resolvable,
                             const zypp::callback::UserData &/*userData*/)
{
    if (resolvable)
        m_currentPkg = resolvable->name();

    if (m_stateCallback)
        m_stateCallback(m_currentPkg, "remove_start");

    if (m_callback) {
        CommitProgressInfo info;
        info.packageName     = m_currentPkg;
        info.percentage      = 0;
        info.stage           = "removing";
        info.totalSteps      = m_totalSteps;
        info.completedSteps  = m_completedSteps;
        info.overallPercentage = calcOverall(m_completedSteps, 0, m_totalSteps);
        m_callback(info);
    }
}

/**
 * @brief パッケージ削除進捗 (SA版)。
 */
void RemoveReceiverSA::progress(int value, zypp::Resolvable::constPtr /*resolvable*/,
                                const zypp::callback::UserData &/*userData*/)
{
    if (m_callback) {
        CommitProgressInfo info;
        info.packageName     = m_currentPkg;
        info.percentage      = value;
        info.stage           = "removing";
        info.totalSteps      = m_totalSteps;
        info.completedSteps  = m_completedSteps;
        info.overallPercentage = calcOverall(m_completedSteps, value, m_totalSteps);
        m_callback(info);
    }
}

/**
 * @brief パッケージ削除完了 (SA版)。
 */
void RemoveReceiverSA::finish(zypp::Resolvable::constPtr /*resolvable*/,
                              Error /*error*/, const zypp::callback::UserData &/*userData*/)
{
    if (m_stateCallback)
        m_stateCallback(m_currentPkg, "remove_end");
    ++m_completedSteps;
}

// ─── KeyRingReceiver ────────────────────────

/**
 * @brief GPG鍵を信頼するか確認する。
 *
 * パッケージマネージャとして既知リポジトリの鍵は自動的に信頼・インポートする。
 * これはYaST/zypperと同等の動作である。
 */
KeyRingReceiver::KeyTrust KeyRingReceiver::askUserToAcceptKey(
    const zypp::PublicKey &/*key*/,
    const zypp::KeyContext &/*keycontext*/)
{
    return KEY_TRUST_AND_IMPORT;
}

/**
 * @brief 未署名ファイルを受け入れるか確認する — 受け入れる。
 */
bool KeyRingReceiver::askUserToAcceptUnsignedFile(
    const std::string &/*file*/,
    const zypp::KeyContext &/*keycontext*/)
{
    return true;
}

/**
 * @brief 未知の鍵IDを受け入れるか確認する — 受け入れる。
 */
bool KeyRingReceiver::askUserToAcceptUnknownKey(
    const std::string &/*file*/,
    const std::string &/*id*/,
    const zypp::KeyContext &/*keycontext*/)
{
    return true;
}

/**
 * @brief 署名検証失敗を受け入れるか確認する — 受け入れる。
 */
bool KeyRingReceiver::askUserToAcceptVerificationFailed(
    const std::string &/*file*/,
    const zypp::PublicKey &/*key*/,
    const zypp::KeyContext &/*keycontext*/)
{
    return true;
}

// ─── DigestReceiver ─────────────────────────

/**
 * @brief ダイジェストなしファイルを受け入れる。
 */
bool DigestReceiver::askUserToAcceptNoDigest(const zypp::Pathname &/*file*/)
{
    return true;
}

/**
 * @brief 未知のダイジェストタイプを受け入れる。
 */
bool DigestReceiver::askUserToAccepUnknownDigest(
    const zypp::Pathname &/*file*/,
    const std::string &/*name*/)
{
    return true;
}

/**
 * @brief 不一致ダイジェストを受け入れる。
 */
bool DigestReceiver::askUserToAcceptWrongDigest(
    const zypp::Pathname &/*file*/,
    const std::string &/*requested*/,
    const std::string &/*found*/)
{
    return true;
}

} // namespace qZypper
