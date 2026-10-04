#include "ZyppCallbackReceiver.h"

#include <algorithm>
#include <cctype>
#include <QLoggingCategory>


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
 * IO エラー (一時的な障害の可能性がある) に限り、パッケージごとに最大
 * kMaxRetries 回まで自動リトライする。NOT_FOUND / INVALID は再試行しても
 * 解消しない (INVALID は改ざんの可能性もある) ため、即座に ABORT を返して通知する。
 * @param resolvable 対象パッケージ
 * @param error エラー種別
 * @param description エラー詳細
 * @return RETRY または ABORT
 */
DownloadReceiver::Action DownloadReceiver::problem(
    zypp::Resolvable::constPtr resolvable,
    Error error, const std::string &description)
{
    if (m_cancelFlag.load())
        return ABORT;

    // start() を経ずに別パッケージの problem() が届いた場合も回数を引き継がない
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
    // リトライ回数は対象パッケージが変わったときだけリセットする。
    // RETRY 後の再ダウンロードで start() が再度呼ばれても回数を保持し、無限リトライを防ぐ。
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

// ─── KeyRingReceiver (fail-closed) ────────────────────────

/**
 * @brief フィンガープリントを正規化する。
 *
 * ASCII 空白を除去して大文字化する。結果が40桁または64桁の16進数で
 * なければ "" を返す (不正な指紋は承認集合に載せないため)。
 * @param raw 生フィンガープリント文字列
 * @return 正規化済みフィンガープリント、または ""
 */
std::string KeyRingReceiver::normalizeFingerprint(const std::string &raw)
{
    std::string out;
    out.reserve(raw.size());
    for (unsigned char c : raw) {
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v')
            continue;
        out += static_cast<char>(std::toupper(c));
    }
    if (out.size() != 40 && out.size() != 64)
        return "";
    for (unsigned char c : out) {
        if (!std::isxdigit(c))
            return "";
    }
    return out;
}

/**
 * @brief フィンガープリントを承認済み集合に登録する (mutex 保護)。
 * @param normalizedFingerprint 正規化済みフィンガープリント
 */
void KeyRingReceiver::approveFingerprint(const std::string &normalizedFingerprint)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_approved.insert(normalizedFingerprint);
}

/**
 * @brief 未信頼鍵通知コールバックを設定する (mutex 保護)。
 * @param cb 通知コールバック
 */
void KeyRingReceiver::setUntrustedKeyCallback(UntrustedKeyCallbackFn cb)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_untrustedCb = std::move(cb);
}

/**
 * @brief GPG鍵を信頼するか確認する (fail-closed)。
 *
 * 事前に approveFingerprint で承認されたフィンガープリントと一致した
 *場合のみ KEY_TRUST_AND_IMPORT を返す (承認はワンショット消費)。
 * それ以外の鍵は UntrustedKeyInfo をコールバック通知して KEY_DONT_TRUST を返す。
 * @param key 対象の公開鍵
 * @param keycontext 鍵のコンテキスト (リポジトリ情報)
 * @return 承認済みの場合 KEY_TRUST_AND_IMPORT、それ以外は KEY_DONT_TRUST
 */
KeyRingReceiver::KeyTrust KeyRingReceiver::askUserToAcceptKey(
    const zypp::PublicKey &key,
    const zypp::KeyContext &keycontext)
{
    const std::string rawFp = key.fingerprint();
    const std::string fp = normalizeFingerprint(rawFp);

    UntrustedKeyCallbackFn cb;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (!fp.empty()) {
            auto it = m_approved.find(fp);
            if (it != m_approved.end()) {
                m_approved.erase(it);
                return KEY_TRUST_AND_IMPORT;
            }
        }
        cb = m_untrustedCb;
    }

    UntrustedKeyInfo info;
    info.fingerprint = fp.empty() ? rawFp : fp;
    info.id = key.id();
    info.name = key.name();
    info.created = static_cast<std::int64_t>(key.created());
    info.expires = static_cast<std::int64_t>(key.expires());
    info.repoAlias = keycontext.repoInfo().alias();
    info.repoName = keycontext.repoInfo().name();

    qWarning("Refusing to trust GPG key id=%s fingerprint=%s repo=%s (no explicit approval)",
             info.id.c_str(), info.fingerprint.c_str(), info.repoAlias.c_str());

    // ロック外で通知する (デッドロック回避)
    if (cb)
        cb(info);

    return KEY_DONT_TRUST;
}

/**
 * @brief 未署名ファイルを受け入れるか確認する — 常に拒否する。
 * @param file 対象ファイル名
 * @param keycontext 鍵のコンテキスト
 * @return 常に false (libzypp の安全なデフォルトと同様)
 */
bool KeyRingReceiver::askUserToAcceptUnsignedFile(
    const std::string &file,
    const zypp::KeyContext &/*keycontext*/)
{
    qWarning("Refusing unsigned file: %s", file.c_str());
    return false;
}

/**
 * @brief 未知の鍵IDを受け入れるか確認する — 常に拒否する。
 * @param file 対象ファイル名
 * @param id 未知の鍵ID
 * @param keycontext 鍵のコンテキスト
 * @return 常に false (libzypp の安全なデフォルトと同様)
 */
bool KeyRingReceiver::askUserToAcceptUnknownKey(
    const std::string &file,
    const std::string &id,
    const zypp::KeyContext &/*keycontext*/)
{
    qWarning("Refusing unknown key id=%s for file: %s", id.c_str(), file.c_str());
    return false;
}

/**
 * @brief 署名検証失敗を受け入れるか確認する — 常に拒否する。
 * @param file 対象ファイル名
 * @param key 検証に使われた公開鍵
 * @param keycontext 鍵のコンテキスト
 * @return 常に false (libzypp の安全なデフォルトと同様)
 */
bool KeyRingReceiver::askUserToAcceptVerificationFailed(
    const std::string &file,
    const zypp::PublicKey &key,
    const zypp::KeyContext &/*keycontext*/)
{
    qWarning("Refusing file with failed verification: %s key id=%s",
             file.c_str(), key.id().c_str());
    return false;
}

// ─── DigestReceiver (fail-closed) ─────────────────────────

/**
 * @brief ダイジェストなしファイルを受け入れるか — 常に拒否する。
 * @param file 対象ファイルパス
 * @return 常に false
 */
bool DigestReceiver::askUserToAcceptNoDigest(const zypp::Pathname &file)
{
    qWarning("Refusing file without digest: %s", file.c_str());
    return false;
}

/**
 * @brief 未知のダイジェスト種別を受け入れるか — 常に拒否する。
 * @param file 対象ファイルパス
 * @param name ダイジェスト名
 * @return 常に false
 */
bool DigestReceiver::askUserToAccepUnknownDigest(
    const zypp::Pathname &file,
    const std::string &name)
{
    qWarning("Refusing file with unknown digest %s: %s", name.c_str(), file.c_str());
    return false;
}

/**
 * @brief 不一致ダイジェストを受け入れるか — 常に拒否する。
 * @param file 対象ファイルパス
 * @param requested 要求されたダイジェスト
 * @param found 実際のダイジェスト
 * @return 常に false
 */
bool DigestReceiver::askUserToAcceptWrongDigest(
    const zypp::Pathname &file,
    const std::string &/*requested*/,
    const std::string &/*found*/)
{
    qWarning("Refusing file with wrong digest: %s", file.c_str());
    return false;
}

} // namespace qZypper
