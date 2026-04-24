#ifndef QZYPPER_BACKEND_ZYPPCALLBACKRECEIVER_H
#define QZYPPER_BACKEND_ZYPPCALLBACKRECEIVER_H

#include <functional>
#include <string>
#include <atomic>
#include <zypp/Callback.h>
#include <zypp/ZYppCallbacks.h>
#include <zypp/KeyRing.h>
#include <zypp/Digest.h>


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

    static constexpr int kMaxRetries = 3;   // 自動リトライ回数

private:
    ProgressCallbackFn m_callback;
    StateEventCallbackFn m_stateCallback;
    int &m_completedSteps;
    int m_totalSteps;
    const std::atomic<bool> &m_cancelFlag;
    std::string &m_problemDetail;          // エラー詳細を格納する参照
    std::string m_currentPkg;
    int m_retryCount = 0;                  // 現在パッケージのリトライ回数
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
 * @brief GPG鍵信頼確認コールバック。
 *
 * libzyppがパッケージ署名検証時にGPG鍵の信頼を確認する。
 * パッケージマネージャとして、既知の鍵は自動的に信頼・インポートする。
 */
class KeyRingReceiver
    : public zypp::callback::ReceiveReport<zypp::KeyRingReport>
{
public:
    /** @brief GPG鍵を信頼するか確認 — 常にインポートして信頼する。 */
    KeyTrust askUserToAcceptKey(const zypp::PublicKey &key,
                                const zypp::KeyContext &keycontext) override;

    /** @brief 未署名ファイルを受け入れるか — 受け入れる。 */
    bool askUserToAcceptUnsignedFile(const std::string &file,
                                     const zypp::KeyContext &keycontext) override;

    /** @brief 未知の鍵を受け入れるか — 受け入れる。 */
    bool askUserToAcceptUnknownKey(const std::string &file,
                                   const std::string &id,
                                   const zypp::KeyContext &keycontext) override;

    /** @brief 検証失敗を受け入れるか — 受け入れる。 */
    bool askUserToAcceptVerificationFailed(const std::string &file,
                                           const zypp::PublicKey &key,
                                           const zypp::KeyContext &keycontext) override;
};

/**
 * @brief ダイジェスト検証コールバック。
 *
 * チェックサム不一致時にlibzyppが確認を求める。
 */
class DigestReceiver
    : public zypp::callback::ReceiveReport<zypp::DigestReport>
{
public:
    /** @brief ダイジェスト不一致を受け入れるか — 受け入れる。 */
    bool askUserToAcceptNoDigest(const zypp::Pathname &file) override;
    bool askUserToAccepUnknownDigest(const zypp::Pathname &file,
                                     const std::string &name) override;
    bool askUserToAcceptWrongDigest(const zypp::Pathname &file,
                                    const std::string &requested,
                                    const std::string &found) override;
};

} // namespace qZypper

#endif // QZYPPER_BACKEND_ZYPPCALLBACKRECEIVER_H
