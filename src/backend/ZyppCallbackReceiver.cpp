#include <QLoggingCategory>
#include <algorithm>
#include <cctype>
#include "ZyppCallbackReceiver.h"

namespace qZypper {

namespace {

/**
 * @brief 全体進捗率を計算するヘルパー
 * @param completedSteps 完了済みステップ数
 * @param currentPercent 現在パッケージの進捗率 (0〜100)
 * @param totalSteps 全ステップ数
 * @return 全体進捗率 (0〜100)
 */
int calcOverall(int completedSteps, int currentPercent, int totalSteps)
{
    if (totalSteps <= 0) return 0;

    return std::min(100, (completedSteps * 100 + currentPercent) / totalSteps);
}

} // anonymous namespace

// --- InstallReceiver ---

/**
 * @brief InstallReceiverを構築する
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
 * @brief パッケージインストール中のエラー対応コールバック
 *
 * libzyppは問題発生時にこのコールバックで対応を問い合わせる
 *
 * デフォルト実装はABORTを返すためoverrideしないと、軽微なエラーでもコミット全体が中断される
 *
 * インストールエラーはリトライしても解消しないためABORTを返すが、ユーザに分かりやすいエラーメッセージを保存する
 */
InstallReceiver::Action InstallReceiver::problem(zypp::Resolvable::constPtr resolvable,
                                                 Error /*error*/,
                                                 const std::string &description,
                                                 RpmLevel /*level*/)
{
    std::string pkg = resolvable ? resolvable->name() : m_currentPkg;
    m_problemDetail = "Install failed: " + pkg + ": " + description;
    return ABORT;
}

/**
 * @brief パッケージインストール開始時のコールバック
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
 * @brief パッケージインストール進捗コールバック
 * @param value 進捗率 (0〜100)
 * @param resolvable インストール対象パッケージ
 * @return falseでキャンセル
 */
bool InstallReceiver::progress(int value, zypp::Resolvable::constPtr /*resolvable*/)
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

    return true;
}

/**
 * @brief パッケージインストール完了コールバック
 */
void InstallReceiver::finish(zypp::Resolvable::constPtr /*resolvable*/,
                             Error /*error*/,
                             const std::string &/*reason*/,
                             RpmLevel /*level*/)
{
    if (m_stateCallback)
        m_stateCallback(m_currentPkg, "install_end");

    ++m_completedSteps;
}

// --- RemoveReceiver ---─

/**
 * @brief RemoveReceiverを構築する
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
 * @brief パッケージ削除中のエラー対応コールバック
 *
 * 削除エラーはリトライでは解消しないためABORTを返すが、エラー詳細を保存してユーザに提示する
 */
RemoveReceiver::Action RemoveReceiver::problem(zypp::Resolvable::constPtr resolvable,
                                               Error /*error*/,
                                               const std::string &description)
{
    std::string pkg = resolvable ? resolvable->name() : m_currentPkg;
    m_problemDetail = "Remove failed: " + pkg + ": " + description;
    return ABORT;
}

/**
 * @brief パッケージ削除開始時のコールバック
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
 * @brief パッケージ削除進捗コールバック
 * @param value 進捗率 (0〜100)
 * @param resolvable 削除対象パッケージ
 * @return falseでキャンセル
 */
bool RemoveReceiver::progress(int value, zypp::Resolvable::constPtr /*resolvable*/)
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
    return true;
}

/**
 * @brief パッケージ削除完了コールバック
 */
void RemoveReceiver::finish(zypp::Resolvable::constPtr /*resolvable*/,
                            Error /*error*/,
                            const std::string &/*reason*/)
{
    if (m_stateCallback)
        m_stateCallback(m_currentPkg, "remove_end");

    ++m_completedSteps;
}

// --- DownloadReceiver ---

/**
 * @brief DownloadReceiverを構築する
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
 * @brief パッケージダウンロード中のエラー対応コールバック
 *
 * libzyppのデフォルト実装はABORTを返すため、ネットワーク瞬断やIOエラーで即座にコミット全体が中断されてしまう
 *
 * IOエラー (一時的な障害の可能性がある) に限り、パッケージごとに最大kMaxRetries回まで自動リトライする
 *
 * NOT_FOUND / INVALIDは、再試行しても解消しない (INVALIDは改ざんの可能性もある) ため、即座にABORTを返して通知する
 *
 * @param resolvable 対象パッケージ
 * @param error エラー種別
 * @param description エラー詳細
 * @return RETRYまたはABORT
 */
DownloadReceiver::Action DownloadReceiver::problem(zypp::Resolvable::constPtr resolvable, Error error, const std::string &description)
{
    if (m_cancelFlag.load())
        return ABORT;

    // start()を経ずに別パッケージのproblem()が届いた場合も回数を引き継がない
    if (resolvable && resolvable->satSolvable() != m_retrySolvable) {
        m_retrySolvable = resolvable->satSolvable();
        m_retryCount = 0;
    }

    std::string pkg = resolvable ? resolvable->name() : m_currentPkg;
    if (error == IO && m_retryCount < kMaxRetries) {
        ++m_retryCount;
        return RETRY;
    }

    m_problemDetail = "Download failed: " + pkg + ": " + description;
    return ABORT;
}

/**
 * @brief キャッシュ済みパッケージの通知コールバック
 *
 * ダウンロード不要の場合、start/progress/finishの代わりにこれだけが呼ばれる
 * ダウンロードステップを完了としてカウントする
 *
 * @param resolvable キャッシュ済みパッケージ
 * @param localfile ローカルファイルパス
 */
void DownloadReceiver::infoInCache(zypp::Resolvable::constPtr resolvable, const zypp::Pathname &/*localfile*/)
{
    if (m_stateCallback && resolvable)
        m_stateCallback(resolvable->name(), "cached");

    ++m_completedSteps;
}

/**
 * @brief パッケージダウンロード開始時のコールバック
 * @param resolvable ダウンロード対象パッケージ
 * @param url ダウンロードURL
 */
void DownloadReceiver::start(zypp::Resolvable::constPtr resolvable, const zypp::Url &/*url*/)
{
    if (resolvable)
        m_currentPkg = resolvable->name();

    // リトライ回数は対象パッケージが変わったときだけリセットする
    // RETRY後の再ダウンロードでstart()が再度呼ばれても回数を保持し、無限リトライを防ぐ
    const zypp::sat::Solvable solvable = resolvable ? resolvable->satSolvable() : zypp::sat::Solvable();
    if (solvable != m_retrySolvable) {
        m_retrySolvable = solvable;
        m_retryCount = 0;
    }

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
 * @brief パッケージダウンロード進捗コールバック
 * @param value 進捗率 (0〜100)
 * @param resolvable ダウンロード対象パッケージ
 * @return falseでキャンセル
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
 * @brief パッケージダウンロード完了コールバック
 */
void DownloadReceiver::finish(zypp::Resolvable::constPtr /*resolvable*/,
                              Error /*error*/,
                              const std::string &/*reason*/)
{
    if (m_stateCallback)
        m_stateCallback(m_currentPkg, "download_end");

    ++m_completedSteps;
}

// --- InstallReceiverSA (SingleTransaction版) ---──

/**
 * @brief InstallReceiverSAを構築する
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
 * @brief パッケージインストール開始 (SA版)
 */
void InstallReceiverSA::start(zypp::Resolvable::constPtr resolvable, const zypp::callback::UserData &/*userData*/)
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
 * @brief パッケージインストール進捗 (SA版)
 */
void InstallReceiverSA::progress(int value, zypp::Resolvable::constPtr /*resolvable*/, const zypp::callback::UserData &/*userData*/)
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
 * @brief パッケージインストール完了 (SA版)
 */
void InstallReceiverSA::finish(zypp::Resolvable::constPtr /*resolvable*/, Error /*error*/, const zypp::callback::UserData &/*userData*/)
{
    if (m_stateCallback)
        m_stateCallback(m_currentPkg, "install_end");

    ++m_completedSteps;
}

// --- RemoveReceiverSA (SingleTransaction版) ------

/**
 * @brief RemoveReceiverSAを構築する
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
 * @brief パッケージ削除開始 (SA版)
 */
void RemoveReceiverSA::start(zypp::Resolvable::constPtr resolvable, const zypp::callback::UserData &/*userData*/)
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
 * @brief パッケージ削除進捗 (SA版)
 */
void RemoveReceiverSA::progress(int value, zypp::Resolvable::constPtr /*resolvable*/, const zypp::callback::UserData &/*userData*/)
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
 * @brief パッケージ削除完了 (SA版)
 */
void RemoveReceiverSA::finish(zypp::Resolvable::constPtr /*resolvable*/, Error /*error*/, const zypp::callback::UserData &/*userData*/)
{
    if (m_stateCallback)
        m_stateCallback(m_currentPkg, "remove_end");

    ++m_completedSteps;
}

} // namespace qZypper
