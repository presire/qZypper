#include <QLoggingCategory>
#include <QDebug>
#include <algorithm>
#include <cctype>
#include "ZyppCallbackReceiver.h"

namespace qZypper {

namespace {

/**
 * @brief 全体進捗率を計算するヘルパー
 * @param completedSteps 完了済みステップ数
 * @param currentPercent 現在パッケージの進捗率(0〜100)
 * @param totalSteps 全ステップ数
 * @return 全体進捗率(0〜100)
 */
int calcOverall(int completedSteps, int currentPercent, int totalSteps)
{
    if (totalSteps <= 0) return 0;

    return std::min(100, (completedSteps * 100 + currentPercent) / totalSteps);
}

/**
 * @brief libzyppのError列挙値をGUI向けの種別文字列へ変換する
 * @param error Error値
 * @return 種別文字列
 * @note 全レポートで[NO_ERROR]/[NOT_FOUND]/[IO]/[INVALID]の順に定義されている
 */
QString problemErrorKind(int error)
{
    // libzyppの各レポートで共通の列挙順を種別文字列へ変換する
    switch (error) {
    case 0: {
        return QStringLiteral("none");
    }
    case 1: {
        return QStringLiteral("not-found");
    }
    case 2: {
        return QStringLiteral("io");
    }
    case 3: {
        return QStringLiteral("invalid");
    }
    default: {
        return QStringLiteral("unknown");
    }
    }
}

/**
 * @brief パッケージ操作エラーへの対応をユーザへ問い合わせる
 * @param ask プロンプト要求コールバック
 * @param cancelFlag キャンセルフラグ
 * @param operation 操作種別 [download]/[install]/[remove]
 * @param package 対象パッケージ名
 * @param error libzyppのError値
 * @param description エラー詳細
 * @param allowIgnore [Ignore]を提示してよいか
 * @return 選択されたアクション [abort]/[retry]/[ignore]
 * @note 回答が得られるまでワーカースレッドをブロックする
 */
QString askProblem(const PromptAskFn &ask, const std::atomic<bool> &cancelFlag,
                   const QString &operation, const std::string &package,
                   int error, const std::string &description, bool allowIgnore)
{
    // キャンセル済み、または問い合わせ手段がないときはプロンプトを出さずに中断する
    if (!ask || cancelFlag.load()) {
        return QStringLiteral("abort");
    }

    const QStringList actions = allowIgnore
        ? QStringList{QStringLiteral("abort"), QStringLiteral("retry"), QStringLiteral("ignore")}
        : QStringList{QStringLiteral("abort"), QStringLiteral("retry")};

    PromptRequest request;
    request.type = operation + QStringLiteral("-error");
    request.data = buildProblemPromptData(operation, QString::fromStdString(package),
                                          problemErrorKind(error),
                                          QString::fromStdString(description), actions);
    request.timeout = std::chrono::milliseconds(300000);
    request.safeDefault = QVariantMap{{QStringLiteral("action"), QStringLiteral("abort")}};
    request.actions = actions;

    // 回答が得られるまでワーカースレッドをブロックする
    const QVariantMap answer = ask(request);

    return problemAnswerAction(answer, actions);
}

/**
 * @brief [Ignore]したパッケージを結果リストへ重複なく記録する
 * @param pkg パッケージ名
 * @param ignoredPackages 結果リスト
 * @param ignoredPkgs 受信クラス内の記録セット
 * @note 同じパッケージを複数回無視しても結果へは1回だけ追加する
 */
void recordIgnored(const std::string &pkg, std::vector<std::string> &ignoredPackages,
                   std::set<std::string> &ignoredPkgs)
{
    ignoredPkgs.insert(pkg);

    // 結果リストに未登録の場合だけ追加する
    if (std::find(ignoredPackages.begin(), ignoredPackages.end(), pkg) == ignoredPackages.end()) {
        ignoredPackages.push_back(pkg);
    }
}

} // anonymous namespace

// --- InstallReceiver ---

/**
 * @brief InstallReceiverを構築する
 * @param cb 進捗コールバック
 * @param stateCb 状態遷移イベント通知コールバック
 * @param completedSteps 完了ステップ数への参照
 * @param totalSteps 全ステップ数
 * @param cancelFlag キャンセルフラグ
 * @param ask エラー時の問い合わせコールバック
 * @param problemDetail エラー詳細の格納先
 * @param ignoredPackages [Ignore]したパッケージの記録先
 */
InstallReceiver::InstallReceiver(ProgressCallbackFn cb, StateEventCallbackFn stateCb,
                                 int &completedSteps,
                                 int totalSteps, const std::atomic<bool> &cancelFlag,
                                 PromptAskFn ask, std::string &problemDetail,
                                 std::vector<std::string> &ignoredPackages)
    : m_callback(std::move(cb))
    , m_stateCallback(std::move(stateCb))
    , m_completedSteps(completedSteps)
    , m_totalSteps(totalSteps)
    , m_cancelFlag(cancelFlag)
    , m_ask(std::move(ask))
    , m_problemDetail(problemDetail)
    , m_ignoredPackages(ignoredPackages)
{
}

/**
 * @brief パッケージインストール中のエラー対応コールバック
 * @param resolvable エラーが発生したパッケージ
 * @param error libzyppのError値
 * @param description エラー詳細
 * @return 選択された対応 [RETRY]/[IGNORE]/[ABORT]
 *
 * libzyppは問題発生時にこのコールバックで対応を問い合わせる
 *
 * 既定実装は[ABORT]を返すため、overrideしない場合は軽微なエラーでもコミット全体が中断される
 *
 * ユーザには[Abort]/[Retry]/[Ignore]を提示し、[Ignore]したパッケージは成功として数えない
 */
InstallReceiver::Action InstallReceiver::problem(zypp::Resolvable::constPtr resolvable,
                                                 Error error,
                                                 const std::string &description,
                                                 RpmLevel /*level*/)
{
    const std::string pkg = resolvable ? resolvable->name() : m_currentPkg;

    // ユーザへ[Abort]/[Retry]/[Ignore]を問い合わせる
    // キャンセル済みならプロンプトを出さず[ABORT]を返す
    const QString answer = askProblem(m_ask, m_cancelFlag, QStringLiteral("install"), pkg,
                                      static_cast<int>(error), description, true);

    // ユーザが[Retry]を選択した
    if (answer == QStringLiteral("retry")) {
        return RETRY;
    }

    // [Ignore]したパッケージは成功として扱わず、結果へ記録する
    if (answer == QStringLiteral("ignore")) {
        recordIgnored(pkg, m_ignoredPackages, m_ignoredPkgs);
        m_problemDetail = "Install ignored: " + pkg + ": " + description;
        return IGNORE;
    }

    // ユーザが[Abort]を選択した場合、または回答できない場合
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
 * @param value 進捗率(0〜100)
 * @param resolvable インストール対象パッケージ
 * @return falseを返すとキャンセル
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
void InstallReceiver::finish(zypp::Resolvable::constPtr resolvable,
                             Error error,
                             const std::string &/*reason*/,
                             RpmLevel /*level*/)
{
    if (m_stateCallback)
        m_stateCallback(m_currentPkg, "install_end");

    const std::string pkg = resolvable ? resolvable->name() : m_currentPkg;

    // 失敗したパッケージと、ユーザが[Ignore]したパッケージは完了ステップに計上しない
    if (error != NO_ERROR || m_ignoredPkgs.count(pkg) > 0) {
        // 完了ステップへ計上せず戻る
        return;
    }

    ++m_completedSteps;
}

// --- RemoveReceiver ---

/**
 * @brief RemoveReceiverを構築する
 * @param cb 進捗コールバック
 * @param stateCb 状態遷移イベント通知コールバック
 * @param completedSteps 完了ステップ数への参照
 * @param totalSteps 全ステップ数
 * @param cancelFlag キャンセルフラグ
 * @param ask エラー時の問い合わせコールバック
 * @param problemDetail エラー詳細の格納先
 * @param ignoredPackages [Ignore]したパッケージの記録先
 */
RemoveReceiver::RemoveReceiver(ProgressCallbackFn cb, StateEventCallbackFn stateCb,
                               int &completedSteps,
                               int totalSteps, const std::atomic<bool> &cancelFlag,
                               PromptAskFn ask, std::string &problemDetail,
                               std::vector<std::string> &ignoredPackages)
    : m_callback(std::move(cb))
    , m_stateCallback(std::move(stateCb))
    , m_completedSteps(completedSteps)
    , m_totalSteps(totalSteps)
    , m_cancelFlag(cancelFlag)
    , m_ask(std::move(ask))
    , m_problemDetail(problemDetail)
    , m_ignoredPackages(ignoredPackages)
{
}

/**
 * @brief パッケージ削除中のエラー対応コールバック
 * @param resolvable エラーが発生したパッケージ
 * @param error libzyppのError値
 * @param description エラー詳細
 * @return 選択された対応 [RETRY]/[IGNORE]/[ABORT]
 *
 * ユーザには[Abort]/[Retry]/[Ignore]を提示し、[Ignore]したパッケージは成功として数えない
 */
RemoveReceiver::Action RemoveReceiver::problem(zypp::Resolvable::constPtr resolvable,
                                               Error error,
                                               const std::string &description)
{
    const std::string pkg = resolvable ? resolvable->name() : m_currentPkg;

    // ユーザへ[Abort]/[Retry]/[Ignore]を問い合わせる
    // キャンセル済みならプロンプトを出さず[ABORT]を返す
    const QString answer = askProblem(m_ask, m_cancelFlag, QStringLiteral("remove"), pkg,
                                      static_cast<int>(error), description, true);

    // ユーザが[Retry]を選択した
    if (answer == QStringLiteral("retry")) {
        return RETRY;
    }

    // [Ignore]したパッケージは成功として扱わず、結果へ記録する
    if (answer == QStringLiteral("ignore")) {
        recordIgnored(pkg, m_ignoredPackages, m_ignoredPkgs);
        m_problemDetail = "Remove ignored: " + pkg + ": " + description;
        return IGNORE;
    }

    // ユーザが[Abort]を選択した場合、または回答できない場合
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
 * @param value 進捗率(0〜100)
 * @param resolvable 削除対象パッケージ
 * @return falseを返すとキャンセル
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
void RemoveReceiver::finish(zypp::Resolvable::constPtr resolvable,
                            Error error,
                            const std::string &/*reason*/)
{
    if (m_stateCallback)
        m_stateCallback(m_currentPkg, "remove_end");

    const std::string pkg = resolvable ? resolvable->name() : m_currentPkg;

    // 失敗したパッケージと、ユーザが[Ignore]したパッケージは完了ステップに計上しない
    if (error != NO_ERROR || m_ignoredPkgs.count(pkg) > 0) {
        // 完了ステップへ計上せず戻る
        return;
    }

    ++m_completedSteps;
}

// --- DownloadReceiver ---

/**
 * @brief DownloadReceiverを構築する
 * @param cb 進捗コールバック
 * @param stateCb 状態遷移イベント通知コールバック
 * @param completedSteps 完了ステップ数への参照
 * @param totalSteps 全ステップ数
 * @param cancelFlag キャンセルフラグ
 * @param ask エラー時の問い合わせコールバック
 * @param problemDetail エラー詳細の格納先
 * @param ignoredPackages [Ignore]したパッケージの記録先
 */
DownloadReceiver::DownloadReceiver(ProgressCallbackFn cb, StateEventCallbackFn stateCb,
                                   int &completedSteps,
                                   int totalSteps, const std::atomic<bool> &cancelFlag,
                                   PromptAskFn ask, std::string &problemDetail,
                                   std::vector<std::string> &ignoredPackages)
    : m_callback(std::move(cb))
    , m_stateCallback(std::move(stateCb))
    , m_completedSteps(completedSteps)
    , m_totalSteps(totalSteps)
    , m_cancelFlag(cancelFlag)
    , m_ask(std::move(ask))
    , m_problemDetail(problemDetail)
    , m_ignoredPackages(ignoredPackages)
{
}

/**
 * @brief パッケージダウンロード中のエラー対応コールバック
 * @param resolvable 対象パッケージ
 * @param error エラー種別
 * @param description エラー詳細
 * @return 選択された対応 [RETRY]/[IGNORE]/[ABORT]
 *
 * libzyppの既定実装は[ABORT]を返し、ネットワーク瞬断やIOエラーでコミット全体が中断される
 *
 * [IO]エラー(一時的障害)に限り、パッケージごとに最大kMaxRetries回まで自動リトライする
 *
 * 自動リトライで解消しない場合はユーザへ問い合わせる
 *
 * 検証系エラー[INVALID]は改ざんの可能性もあるため[Ignore]を提示せず、[Abort]/[Retry]のみ提示する
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

    const std::string pkg = resolvable ? resolvable->name() : m_currentPkg;
    if (error == IO && m_retryCount < kMaxRetries) {
        ++m_retryCount;
        return RETRY;
    }

    // 検証系エラー[INVALID]は[Ignore]しても安全に継続できないため[Abort]/[Retry]のみ提示する
    const bool allowIgnore = (error != INVALID);

    // 自動リトライで解消しないためユーザへ問い合わせる
    // キャンセル済みならプロンプトを出さず[ABORT]を返す
    const QString answer = askProblem(m_ask, m_cancelFlag, QStringLiteral("download"), pkg,
                                      static_cast<int>(error), description, allowIgnore);

    // ユーザが[Retry]を選択した
    // ユーザ選択のリトライ回数に上限は設けない
    if (answer == QStringLiteral("retry")) {
        return RETRY;
    }

    // [Ignore]したパッケージは成功として扱わず、結果へ記録する
    if (allowIgnore && answer == QStringLiteral("ignore")) {
        recordIgnored(pkg, m_ignoredPackages, m_ignoredPkgs);
        m_problemDetail = "Download ignored: " + pkg + ": " + description;
        return IGNORE;
    }

    // ユーザが[Abort]を選択した場合、または回答できない場合
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
 * @param value 進捗率(0〜100)
 * @param resolvable ダウンロード対象パッケージ
 * @return falseを返すとキャンセル
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
void DownloadReceiver::finish(zypp::Resolvable::constPtr resolvable,
                              Error error,
                              const std::string &/*reason*/)
{
    if (m_stateCallback)
        m_stateCallback(m_currentPkg, "download_end");

    const std::string pkg = resolvable ? resolvable->name() : m_currentPkg;

    // 失敗したパッケージと、ユーザが[Ignore]したパッケージは完了ステップに計上しない
    if (error != NO_ERROR || m_ignoredPkgs.count(pkg) > 0) {
        // 完了ステップへ計上せず戻る
        return;
    }

    ++m_completedSteps;
}

// --- InstallReceiverSA(SingleTransaction版) ---

/**
 * @brief InstallReceiverSAを構築する
 * @param cb 進捗コールバック
 * @param stateCb 状態遷移イベント通知コールバック
 * @param completedSteps 完了ステップ数への参照
 * @param totalSteps 全ステップ数
 * @param cancelFlag キャンセルフラグ
 * @param problemDetail エラー詳細の格納先
 */
InstallReceiverSA::InstallReceiverSA(ProgressCallbackFn cb, StateEventCallbackFn stateCb,
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
 * @brief パッケージインストール開始(SA版)
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
 * @brief パッケージインストール進捗(SA版)
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
 * @brief パッケージインストール完了(SA版)
 * @param resolvable 対象パッケージ
 * @param error libzyppのError値
 *
 * SA版にはproblem()が無いため、失敗は結果へ記録して完了ステップに計上しない
 */
void InstallReceiverSA::finish(zypp::Resolvable::constPtr resolvable, Error error, const zypp::callback::UserData &/*userData*/)
{
    if (m_stateCallback)
        m_stateCallback(m_currentPkg, "install_end");

    // [ABORT]以外の選択肢が無いため、失敗は結果へ記録する
    if (error != NO_ERROR) {
        const std::string pkg = resolvable ? resolvable->name() : m_currentPkg;
        m_problemDetail = "Install failed: " + pkg + ": "
                          + problemErrorKind(static_cast<int>(error)).toStdString();
        qWarning("qzypper: install failed for %s (error %d)", pkg.c_str(), static_cast<int>(error));

        // 失敗したパッケージは完了ステップに計上せず戻る
        return;
    }

    ++m_completedSteps;
}

// --- RemoveReceiverSA(SingleTransaction版) ---

/**
 * @brief RemoveReceiverSAを構築する
 * @param cb 進捗コールバック
 * @param stateCb 状態遷移イベント通知コールバック
 * @param completedSteps 完了ステップ数への参照
 * @param totalSteps 全ステップ数
 * @param cancelFlag キャンセルフラグ
 * @param problemDetail エラー詳細の格納先
 */
RemoveReceiverSA::RemoveReceiverSA(ProgressCallbackFn cb, StateEventCallbackFn stateCb,
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
 * @brief パッケージ削除開始(SA版)
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
 * @brief パッケージ削除進捗(SA版)
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
 * @brief パッケージ削除完了(SA版)
 * @param resolvable 対象パッケージ
 * @param error libzyppのError値
 *
 * SA版にはproblem()が無いため、失敗は結果へ記録して完了ステップに計上しない
 */
void RemoveReceiverSA::finish(zypp::Resolvable::constPtr resolvable, Error error, const zypp::callback::UserData &/*userData*/)
{
    if (m_stateCallback)
        m_stateCallback(m_currentPkg, "remove_end");

    // [ABORT]以外の選択肢が無いため、失敗は結果へ記録する
    if (error != NO_ERROR) {
        const std::string pkg = resolvable ? resolvable->name() : m_currentPkg;
        m_problemDetail = "Remove failed: " + pkg + ": "
                          + problemErrorKind(static_cast<int>(error)).toStdString();
        qWarning("qzypper: remove failed for %s (error %d)", pkg.c_str(), static_cast<int>(error));

        // 失敗したパッケージは完了ステップに計上せず戻る
        return;
    }

    ++m_completedSteps;
}

} // namespace qZypper
