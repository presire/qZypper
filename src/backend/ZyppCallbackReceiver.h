#ifndef QZYPPER_BACKEND_ZYPPCALLBACKRECEIVER_H
#define QZYPPER_BACKEND_ZYPPCALLBACKRECEIVER_H

#include <cstdint>
#include <functional>
#include <set>
#include <string>
#include <vector>
#include <atomic>
#include <zypp/Callback.h>
#include <zypp/ZYppCallbacks.h>
#include <zypp/sat/Solvable.h>
#include "CommitInteraction.h"

namespace qZypper {

/**
 * @brief コミット進捗の詳細情報
 */
struct CommitProgressInfo {
    std::string packageName;        // 現在処理中のパッケージ名
    int percentage = 0;             // 個別パッケージ進捗 (0〜100)
    std::string stage;              // "downloading" / "installing" / "removing"
    int totalSteps = 0;             // 全アクションステップ数
    int completedSteps = 0;         // 完了済みステップ数
    int overallPercentage = 0;      // 全体進捗 (0〜100)
};

/**
 * @brief コミット進捗のコールバック型
 */
using ProgressCallbackFn = std::function<void(const CommitProgressInfo&)>;

/**
 * @brief パッケージ状態遷移イベントのコールバック型
 *
 * download_start / download_end / cached / install_start / install_end /
 * remove_start / remove_endのイベントをGUIへ伝達する
 */
using StateEventCallbackFn = std::function<void(const std::string& packageName,
                                                 const std::string& event)>;

/**
 * @class InstallReceiver
 * @brief libzypp InstallResolvableReportの受信クラス
 *
 * パッケージインストール進捗をコールバック経由で通知する
 */
class InstallReceiver
    : public zypp::callback::ReceiveReport<zypp::target::rpm::InstallResolvableReport>
{
public:
    InstallReceiver(ProgressCallbackFn cb, StateEventCallbackFn stateCb,        // コンストラクタ
                    int &completedSteps,
                    int totalSteps, const std::atomic<bool> &cancelFlag,
                    PromptAskFn ask, std::string &problemDetail,
                    std::vector<std::string> &ignoredPackages);

    void start(zypp::Resolvable::constPtr resolvable) override;                 // 処理開始の通知
    bool progress(int value, zypp::Resolvable::constPtr resolvable) override;   // 進捗の通知
    Action problem(zypp::Resolvable::constPtr resolvable,                       // エラー発生時の対処方法の決定
                   Error error, const std::string &description,
                   RpmLevel level) override;
    void finish(zypp::Resolvable::constPtr resolvable,                          // 処理完了の通知
                Error error, const std::string &reason, RpmLevel level) override;

private:
    ProgressCallbackFn m_callback;          // 進捗通知コールバック
    StateEventCallbackFn m_stateCallback;   // 状態遷移イベント通知コールバック
    int &m_completedSteps;                  // 完了済みステップ数 (呼び出し元と共有)
    int m_totalSteps;                       // 全アクションステップ数
    const std::atomic<bool> &m_cancelFlag;  // キャンセル要求フラグ
    PromptAskFn m_ask;                      // エラー時の問い合わせコールバック
    std::string &m_problemDetail;           // エラー詳細を格納する参照
    std::set<std::string> m_ignoredPkgs;    // [Ignore]したパッケージ名 (完了扱いにしない)
    std::string m_currentPkg;               // 処理中のパッケージ名

    std::vector<std::string> &m_ignoredPackages;  // [Ignore]したパッケージの記録先
};

/**
 * @class RemoveReceiver
 * @brief libzypp RemoveResolvableReportの受信クラス
 *
 * パッケージ削除進捗をコールバック経由で通知する
 */
class RemoveReceiver : public zypp::callback::ReceiveReport<zypp::target::rpm::RemoveResolvableReport>
{
public:
    RemoveReceiver(ProgressCallbackFn cb, StateEventCallbackFn stateCb,         // コンストラクタ
                   int &completedSteps,
                   int totalSteps, const std::atomic<bool> &cancelFlag,
                   PromptAskFn ask, std::string &problemDetail,
                   std::vector<std::string> &ignoredPackages);

    void start(zypp::Resolvable::constPtr resolvable) override;                 // 処理開始の通知
    bool progress(int value, zypp::Resolvable::constPtr resolvable) override;   // 進捗の通知
    Action problem(zypp::Resolvable::constPtr resolvable,                       // エラー発生時の対処方法の決定
                   Error error, const std::string &description) override;
    void finish(zypp::Resolvable::constPtr resolvable,                          // 処理完了の通知
                Error error, const std::string &reason) override;

private:
    ProgressCallbackFn m_callback;          // 進捗通知コールバック
    StateEventCallbackFn m_stateCallback;   // 状態遷移イベント通知コールバック
    int &m_completedSteps;                  // 完了済みステップ数 (呼び出し元と共有)
    int m_totalSteps;                       // 全アクションステップ数
    const std::atomic<bool> &m_cancelFlag;  // キャンセル要求フラグ
    PromptAskFn m_ask;                      // エラー時の問い合わせコールバック
    std::string &m_problemDetail;           // エラー詳細を格納する参照
    std::set<std::string> m_ignoredPkgs;    // [Ignore]したパッケージ名 (完了扱いにしない)
    std::string m_currentPkg;               // 処理中のパッケージ名

    std::vector<std::string> &m_ignoredPackages;  // [Ignore]したパッケージの記録先
};

/**
 * @class DownloadReceiver
 * @brief libzypp DownloadResolvableReportの受信クラス
 *
 * パッケージダウンロード進捗をコールバック経由で通知する
 */
class DownloadReceiver : public zypp::callback::ReceiveReport<zypp::repo::DownloadResolvableReport>
{
public:
    DownloadReceiver(ProgressCallbackFn cb, StateEventCallbackFn stateCb,              // コンストラクタ
                     int &completedSteps,
                     int totalSteps, const std::atomic<bool> &cancelFlag,
                     PromptAskFn ask, std::string &problemDetail,
                     std::vector<std::string> &ignoredPackages);

    void infoInCache(zypp::Resolvable::constPtr resolvable,                            // キャッシュ済みパッケージの通知
                     const zypp::Pathname &localfile) override;
    void start(zypp::Resolvable::constPtr resolvable, const zypp::Url &url) override;  // 処理開始の通知
    bool progress(int value, zypp::Resolvable::constPtr resolvable) override;          // 進捗の通知
    Action problem(zypp::Resolvable::constPtr resolvable,                              // エラー発生時の対処方法の決定
                   Error error, const std::string &description) override;
    void finish(zypp::Resolvable::constPtr resolvable,                                 // 処理完了の通知
                Error error, const std::string &reason) override;

    static constexpr int kMaxRetries = 3;   // IOエラー時の自動リトライ回数

private:
    ProgressCallbackFn m_callback;          // 進捗通知コールバック
    StateEventCallbackFn m_stateCallback;   // 状態遷移イベント通知コールバック
    int &m_completedSteps;                  // 完了済みステップ数 (呼び出し元と共有)
    int m_totalSteps;                       // 全アクションステップ数
    const std::atomic<bool> &m_cancelFlag;  // キャンセル要求フラグ
    PromptAskFn m_ask;                      // エラー時の問い合わせコールバック
    std::string &m_problemDetail;           // エラー詳細を格納する参照
    std::set<std::string> m_ignoredPkgs;    // [Ignore]したパッケージ名 (完了扱いにしない)
    std::string m_currentPkg;               // 処理中のパッケージ名

    std::vector<std::string> &m_ignoredPackages;  // [Ignore]したパッケージの記録先
    int m_retryCount = 0;                   // 現在パッケージのリトライ回数
    zypp::sat::Solvable m_retrySolvable;    // リトライ回数を数えている対象パッケージ
};

/**
 * @class InstallReceiverSA
 * @brief libzypp InstallResolvableReportSAの受信クラス(SingleTransaction版)
 */
class InstallReceiverSA : public zypp::callback::ReceiveReport<zypp::target::rpm::InstallResolvableReportSA>
{
public:
    InstallReceiverSA(ProgressCallbackFn cb, StateEventCallbackFn stateCb,      // コンストラクタ
                      int &completedSteps,
                      int totalSteps, const std::atomic<bool> &cancelFlag,
                      std::string &problemDetail);

    void start(zypp::Resolvable::constPtr resolvable,                           // 処理開始の通知
               const zypp::callback::UserData &userData) override;
    void progress(int value, zypp::Resolvable::constPtr resolvable,             // 進捗の通知
                  const zypp::callback::UserData &userData) override;
    void finish(zypp::Resolvable::constPtr resolvable,                          // 処理完了の通知
                Error error, const zypp::callback::UserData &userData) override;

private:
    ProgressCallbackFn m_callback;          // 進捗通知コールバック
    StateEventCallbackFn m_stateCallback;   // 状態遷移イベント通知コールバック
    int &m_completedSteps;                  // 完了済みステップ数 (呼び出し元と共有)
    int m_totalSteps;                       // 全アクションステップ数
    const std::atomic<bool> &m_cancelFlag;  // キャンセル要求フラグ
    std::string &m_problemDetail;           // エラー詳細を格納する参照
    std::string m_currentPkg;               // 処理中のパッケージ名
};

/**
 * @class RemoveReceiverSA
 * @brief libzypp RemoveResolvableReportSAの受信クラス(SingleTransaction版)
 */
class RemoveReceiverSA
    : public zypp::callback::ReceiveReport<zypp::target::rpm::RemoveResolvableReportSA>
{
public:
    RemoveReceiverSA(ProgressCallbackFn cb, StateEventCallbackFn stateCb,       // コンストラクタ
                     int &completedSteps,
                     int totalSteps, const std::atomic<bool> &cancelFlag,
                     std::string &problemDetail);

    void start(zypp::Resolvable::constPtr resolvable,                           // 処理開始の通知
               const zypp::callback::UserData &userData) override;
    void progress(int value, zypp::Resolvable::constPtr resolvable,             // 進捗の通知
                  const zypp::callback::UserData &userData) override;
    void finish(zypp::Resolvable::constPtr resolvable,                          // 処理完了の通知
                Error error, const zypp::callback::UserData &userData) override;

private:
    ProgressCallbackFn m_callback;          // 進捗通知コールバック
    StateEventCallbackFn m_stateCallback;   // 状態遷移イベント通知コールバック
    int &m_completedSteps;                  // 完了済みステップ数 (呼び出し元と共有)
    int m_totalSteps;                       // 全アクションステップ数
    const std::atomic<bool> &m_cancelFlag;  // キャンセル要求フラグ
    std::string &m_problemDetail;           // エラー詳細を格納する参照
    std::string m_currentPkg;               // 処理中のパッケージ名
};

} // namespace qZypper

#endif // QZYPPER_BACKEND_ZYPPCALLBACKRECEIVER_H
