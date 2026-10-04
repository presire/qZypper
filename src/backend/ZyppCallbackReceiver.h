#ifndef QZYPPER_BACKEND_ZYPPCALLBACKRECEIVER_H
#define QZYPPER_BACKEND_ZYPPCALLBACKRECEIVER_H

#include <cstdint>
#include <functional>
#include <string>
#include <atomic>
#include <mutex>
#include <set>
#include <zypp/Callback.h>
#include <zypp/ZYppCallbacks.h>
#include <zypp/KeyRing.h>
#include <zypp/Digest.h>
#include <zypp/sat/Solvable.h>


namespace qZypper {

/**
 * @brief コミット進捗の詳細情報。
 */
struct CommitProgressInfo {
    std::string packageName;        // 現在処理中のパッケージ名
    int percentage = 0;             // 個別パッケージ進捗 (0-100)
    std::string stage;              // "downloading" / "installing" / "removing"
    int totalSteps = 0;             // 全アクションステップ数
    int completedSteps = 0;         // 完了済みステップ数
    int overallPercentage = 0;      // 全体進捗 (0-100)
};

using ProgressCallbackFn = std::function<void(const CommitProgressInfo&)>;

/**
 * @brief パッケージ状態遷移イベントのコールバック型。
 *
 * download_start / download_end / cached / install_start / install_end /
 * remove_start / remove_end のイベントを GUI へ伝達する。
 */
using StateEventCallbackFn = std::function<void(const std::string& packageName,
                                                 const std::string& event)>;

/**
 * @brief libzypp InstallResolvableReport の受信クラス。
 *
 * パッケージインストール進捗をコールバック経由で通知する。
 */
class InstallReceiver
    : public zypp::callback::ReceiveReport<zypp::target::rpm::InstallResolvableReport>
{
public:
    InstallReceiver(ProgressCallbackFn cb, StateEventCallbackFn stateCb,
                    int &completedSteps,
                    int totalSteps, const std::atomic<bool> &cancelFlag,
                    std::string &problemDetail);

    void start(zypp::Resolvable::constPtr resolvable) override;
    bool progress(int value, zypp::Resolvable::constPtr resolvable) override;
    Action problem(zypp::Resolvable::constPtr resolvable,
                   Error error, const std::string &description,
                   RpmLevel level) override;
    void finish(zypp::Resolvable::constPtr resolvable,
                Error error, const std::string &reason, RpmLevel level) override;

private:
    ProgressCallbackFn m_callback;
    StateEventCallbackFn m_stateCallback;
    int &m_completedSteps;
    int m_totalSteps;
    const std::atomic<bool> &m_cancelFlag;
    std::string &m_problemDetail;          // エラー詳細を格納する参照
    std::string m_currentPkg;
};

/**
 * @brief libzypp RemoveResolvableReport の受信クラス。
 *
 * パッケージ削除進捗をコールバック経由で通知する。
 */
class RemoveReceiver
    : public zypp::callback::ReceiveReport<zypp::target::rpm::RemoveResolvableReport>
{
public:
    RemoveReceiver(ProgressCallbackFn cb, StateEventCallbackFn stateCb,
                   int &completedSteps,
                   int totalSteps, const std::atomic<bool> &cancelFlag,
                   std::string &problemDetail);

    void start(zypp::Resolvable::constPtr resolvable) override;
    bool progress(int value, zypp::Resolvable::constPtr resolvable) override;
    Action problem(zypp::Resolvable::constPtr resolvable,
                   Error error, const std::string &description) override;
    void finish(zypp::Resolvable::constPtr resolvable,
                Error error, const std::string &reason) override;

private:
    ProgressCallbackFn m_callback;
    StateEventCallbackFn m_stateCallback;
    int &m_completedSteps;
    int m_totalSteps;
    const std::atomic<bool> &m_cancelFlag;
    std::string &m_problemDetail;          // エラー詳細を格納する参照
    std::string m_currentPkg;
};

/**
 * @brief libzypp DownloadResolvableReport の受信クラス。
 *
 * パッケージダウンロード進捗をコールバック経由で通知する。
 */
class DownloadReceiver
    : public zypp::callback::ReceiveReport<zypp::repo::DownloadResolvableReport>
{
public:
    DownloadReceiver(ProgressCallbackFn cb, StateEventCallbackFn stateCb,
                     int &completedSteps,
                     int totalSteps, const std::atomic<bool> &cancelFlag,
                     std::string &problemDetail);

    void infoInCache(zypp::Resolvable::constPtr resolvable,
                     const zypp::Pathname &localfile) override;
    void start(zypp::Resolvable::constPtr resolvable, const zypp::Url &url) override;
    bool progress(int value, zypp::Resolvable::constPtr resolvable) override;
    Action problem(zypp::Resolvable::constPtr resolvable,
                   Error error, const std::string &description) override;
    void finish(zypp::Resolvable::constPtr resolvable,
                Error error, const std::string &reason) override;

    static constexpr int kMaxRetries = 3;   // IO エラー時の自動リトライ回数

private:
    ProgressCallbackFn m_callback;
    StateEventCallbackFn m_stateCallback;
    int &m_completedSteps;
    int m_totalSteps;
    const std::atomic<bool> &m_cancelFlag;
    std::string &m_problemDetail;          // エラー詳細を格納する参照
    std::string m_currentPkg;
    int m_retryCount = 0;                  // 現在パッケージのリトライ回数
    zypp::sat::Solvable m_retrySolvable;   // リトライ回数を数えている対象パッケージ
};

/**
 * @brief libzypp InstallResolvableReportSA の受信クラス (SingleTransaction版)。
 */
class InstallReceiverSA
    : public zypp::callback::ReceiveReport<zypp::target::rpm::InstallResolvableReportSA>
{
public:
    InstallReceiverSA(ProgressCallbackFn cb, StateEventCallbackFn stateCb,
                      int &completedSteps,
                      int totalSteps, const std::atomic<bool> &cancelFlag);

    void start(zypp::Resolvable::constPtr resolvable,
               const zypp::callback::UserData &userData) override;
    void progress(int value, zypp::Resolvable::constPtr resolvable,
                  const zypp::callback::UserData &userData) override;
    void finish(zypp::Resolvable::constPtr resolvable,
                Error error, const zypp::callback::UserData &userData) override;

private:
    ProgressCallbackFn m_callback;
    StateEventCallbackFn m_stateCallback;
    int &m_completedSteps;
    int m_totalSteps;
    const std::atomic<bool> &m_cancelFlag;
    std::string m_currentPkg;
};

/**
 * @brief libzypp RemoveResolvableReportSA の受信クラス (SingleTransaction版)。
 */
class RemoveReceiverSA
    : public zypp::callback::ReceiveReport<zypp::target::rpm::RemoveResolvableReportSA>
{
public:
    RemoveReceiverSA(ProgressCallbackFn cb, StateEventCallbackFn stateCb,
                     int &completedSteps,
                     int totalSteps, const std::atomic<bool> &cancelFlag);

    void start(zypp::Resolvable::constPtr resolvable,
               const zypp::callback::UserData &userData) override;
    void progress(int value, zypp::Resolvable::constPtr resolvable,
                  const zypp::callback::UserData &userData) override;
    void finish(zypp::Resolvable::constPtr resolvable,
                Error error, const zypp::callback::UserData &userData) override;

private:
    ProgressCallbackFn m_callback;
    StateEventCallbackFn m_stateCallback;
    int &m_completedSteps;
    int m_totalSteps;
    const std::atomic<bool> &m_cancelFlag;
    std::string m_currentPkg;
};

/**
 * @brief 信頼されていないGPG鍵の情報。
 *
 * fail-closed 動作のため、askUserToAcceptKey で信頼されなかった鍵の
 * 詳細を GUI 層 (D-Bus 経由) へ通知するための構造体。
 */
struct UntrustedKeyInfo {
    std::string fingerprint;   // normalized: uppercase hex, no whitespace
    std::string id;            // key id
    std::string name;          // key user id / name
    std::int64_t created = 0;  // epoch seconds
    std::int64_t expires = 0;  // epoch seconds, 0 = never expires
    std::string repoAlias;     // from KeyContext repoInfo (may be empty)
    std::string repoName;
};

/**
 * @brief 未信頼鍵通知コールバック型。
 * @param info 信頼されなかった鍵の情報
 */
using UntrustedKeyCallbackFn = std::function<void(const UntrustedKeyInfo&)>;

/**
 * @brief GPG鍵信頼確認コールバック (fail-closed)。
 *
 * 未承認の鍵・未署名ファイル・検証失敗は全て拒否する。
 * 明示的に承認されたフィンガープリント (approveFingerprint で登録、
 * ワンショット消費) のみ KEY_TRUST_AND_IMPORT を返す。
 */
class KeyRingReceiver
    : public zypp::callback::ReceiveReport<zypp::KeyRingReport>
{
public:
    /**
     * @brief フィンガープリントを正規化する。
     * @param raw 生フィンガープリント文字列
     * @return ASCII 空白を除去し大文字化した結果。40桁または64桁の16進数でなければ "" を返す。
     */
    static std::string normalizeFingerprint(const std::string &raw);

    /**
     * @brief フィンガープリントを承認済み集合に登録する (mutex 保護)。
     * @param normalizedFingerprint 正規化済みフィンガープリント
     */
    void approveFingerprint(const std::string &normalizedFingerprint);

    /**
     * @brief 未信頼鍵通知コールバックを設定する (mutex 保護)。
     * @param cb 通知コールバック
     */
    void setUntrustedKeyCallback(UntrustedKeyCallbackFn cb);

    /**
     * @brief GPG鍵を信頼するか確認 — 承認済みフィンガープリントのみ信頼する。
     * @param key 対象の公開鍵
     * @param keycontext 鍵のコンテキスト (リポジトリ情報)
     * @return 承認済みの場合 KEY_TRUST_AND_IMPORT、それ以外は KEY_DONT_TRUST
     */
    KeyTrust askUserToAcceptKey(const zypp::PublicKey &key,
                                const zypp::KeyContext &keycontext) override;

    /**
     * @brief 未署名ファイルを受け入れるか — 常に拒否する (fail-closed)。
     * @param file 対象ファイル名
     * @param keycontext 鍵のコンテキスト
     * @return 常に false
     */
    bool askUserToAcceptUnsignedFile(const std::string &file,
                                     const zypp::KeyContext &keycontext) override;

    /**
     * @brief 未知の鍵を受け入れるか — 常に拒否する (fail-closed)。
     * @param file 対象ファイル名
     * @param id 未知の鍵ID
     * @param keycontext 鍵のコンテキスト
     * @return 常に false
     */
    bool askUserToAcceptUnknownKey(const std::string &file,
                                   const std::string &id,
                                   const zypp::KeyContext &keycontext) override;

    /**
     * @brief 検証失敗を受け入れるか — 常に拒否する (fail-closed)。
     * @param file 対象ファイル名
     * @param key 検証に使われた公開鍵
     * @param keycontext 鍵のコンテキスト
     * @return 常に false
     */
    bool askUserToAcceptVerificationFailed(const std::string &file,
                                           const zypp::PublicKey &key,
                                           const zypp::KeyContext &keycontext) override;

private:
    std::mutex m_mutex;
    std::set<std::string> m_approved;
    UntrustedKeyCallbackFn m_untrustedCb;
};

/**
 * @brief ダイジェスト検証コールバック (fail-closed)。
 *
 * ダイジェストなし・未知・不一致は全て拒否する。
 */
class DigestReceiver
    : public zypp::callback::ReceiveReport<zypp::DigestReport>
{
public:
    /**
     * @brief ダイジェストなしファイルを受け入れるか — 常に拒否する。
     * @param file 対象ファイルパス
     * @return 常に false
     */
    bool askUserToAcceptNoDigest(const zypp::Pathname &file) override;
    /**
     * @brief 未知のダイジェスト種別を受け入れるか — 常に拒否する。
     * @param file 対象ファイルパス
     * @param name ダイジェスト名
     * @return 常に false
     */
    bool askUserToAccepUnknownDigest(const zypp::Pathname &file,
                                     const std::string &name) override;
    /**
     * @brief 不一致ダイジェストを受け入れるか — 常に拒否する。
     * @param file 対象ファイルパス
     * @param requested 要求されたダイジェスト
     * @param found 実際のダイジェスト
     * @return 常に false
     */
    bool askUserToAcceptWrongDigest(const zypp::Pathname &file,
                                    const std::string &requested,
                                    const std::string &found) override;
};

} // namespace qZypper

#endif // QZYPPER_BACKEND_ZYPPCALLBACKRECEIVER_H
