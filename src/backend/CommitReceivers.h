#ifndef QZYPPER_BACKEND_COMMITRECEIVERS_H
#define QZYPPER_BACKEND_COMMITRECEIVERS_H

#include <QString>
#include <atomic>
#include <mutex>
#include <string>
#include <zypp/ZYppCallbacks.h>
#include <zypp/ProgressData.h>
#include <zypp/Url.h>
#include <zypp/Resolvable.h>
#include <zypp/sat/FileConflicts.h>
#include <zypp/sat/Queue.h>
#include "CommitInteraction.h"

namespace qZypper {

/**
 * @class MediaChangeReceiver
 * @brief メディア交換要求をPromptRegistry経由で所有者へ問い合わせる
 */
class MediaChangeReceiver
    : public zypp::callback::ReceiveReport<zypp::media::MediaChangeReport>
{
public:
    /**
     * @brief 構築する
     * @param ask プロンプト問い合わせコールバック
     */
    explicit MediaChangeReceiver(PromptAskFn ask);

    /**
     * @brief メディア交換要求を処理する
     * @param url 対象URL (I/O)
     * @param mediumNr メディア番号
     * @param label ラベル
     * @param error エラー種別
     * @param description 説明
     * @param devices 利用可能デバイス
     * @param dev_current デバイスインデックス (I/O)
     * @return RETRY / IGNORE / ABORT
     */
    Action requestMedia(zypp::Url &url, unsigned int mediumNr, const std::string &label,
                        Error error, const std::string &description,
                        const std::vector<std::string> &devices,
                        unsigned int &dev_current) override;

    /**
     * @brief 直近の回答が中断だったか
     * @return ABORTを返した場合はtrue
     */
    bool aborted() const { return m_aborted.load(); }

private:
    PromptAskFn m_ask;                      // プロンプト問い合わせコールバック
    std::atomic<bool> m_aborted{false};     // 中断が選択されたか
};

/**
 * @class FileConflictReceiver
 * @brief ファイル競合検出をPromptRegistry経由で所有者へ問い合わせる
 */
class FileConflictReceiver
    : public zypp::callback::ReceiveReport<zypp::target::FindFileConflictstReport>
{
public:
    /**
     * @brief 構築する
     * @param ask プロンプト問い合わせコールバック
     */
    explicit FileConflictReceiver(PromptAskFn ask);

    /**
     * @brief 競合検出結果を処理する
     * @param progress 進捗
     * @param noFilelist ファイルリスト未取得のsolvable
     * @param conflicts 検出された競合
     * @return 続行ならtrue、中断ならfalse
     */
    bool result(const zypp::ProgressData &progress, const zypp::sat::Queue &noFilelist,
                const zypp::sat::FileConflicts &conflicts) override;

    /**
     * @brief ユーザーが中断を選択したか
     * @return 中断の場合true
     */
    bool aborted() const { return m_aborted.load(); }

    /**
     * @brief 中断時のメッセージ
     * @return メッセージ
     */
    QString abortMessage() const;

private:
    PromptAskFn m_ask;                      // プロンプト問い合わせコールバック
    std::atomic<bool> m_aborted{false};     // 中断が選択されたか
    mutable std::mutex m_mutex;             // 中断メッセージの排他制御
    QString m_abortMessage;                 // 中断時のメッセージ
};

/**
 * @class CommitScriptReceiver
 * @brief %pre/%post等スクリプトレット出力を受信する
 */
class CommitScriptReceiver
    : public zypp::callback::ReceiveReport<zypp::target::rpm::CommitScriptReportSA>
{
public:
    /**
     * @brief 構築する
     * @param aggregator 出力集約器
     */
    explicit CommitScriptReceiver(ScriptOutputAggregator &aggregator);

    /**
     * @brief 汎用受信 (rpm出力行) を処理する
     * @param userData 受信データ
     */
    void report(const zypp::callback::UserData &userData) override;

    /**
     * @brief スクリプト開始を処理する
     * @param scriptType スクリプト種別
     * @param packageName パッケージ名 (空可)
     * @param resolvable 対象 (null可)
     * @param userData 受信データ
     */
    void start(const std::string &scriptType, const std::string &packageName,
               zypp::Resolvable::constPtr resolvable,
               const zypp::callback::UserData &userData) override;

    /**
     * @brief スクリプト終了を処理する
     * @param resolvable 対象 (null可)
     * @param error 終了ステータス
     * @param userData 受信データ
     */
    void finish(zypp::Resolvable::constPtr resolvable, Error error,
                const zypp::callback::UserData &userData) override;

private:
    /**
     * @brief userDataからrpm出力行を取り出す
     * @param userData 受信データ
     * @param isError エラー相当かどうか
     * @return 行 (無ければ空)
     */
    QString extractLine(const zypp::callback::UserData &userData, bool *isError) const;

    ScriptOutputAggregator &m_aggregator;   // 出力集約器
    mutable std::mutex m_mutex;             // 実行中スクリプト情報の排他制御
    QString m_scriptType;                   // 実行中のスクリプト種別
    QString m_package;                      // 実行中スクリプトのパッケージ名
};

/**
 * @class RpmOutputReceiver
 * @brief 汎用rpmメッセージ (TransactionReportSA) を受信する
 */
class RpmOutputReceiver : public zypp::callback::ReceiveReport<zypp::target::rpm::TransactionReportSA>
{
public:
    /**
     * @brief 構築する
     * @param aggregator 出力集約器
     */
    explicit RpmOutputReceiver(ScriptOutputAggregator &aggregator);

    /**
     * @brief 汎用受信 (rpm出力行) を処理する
     * @param userData 受信データ
     */
    void report(const zypp::callback::UserData &userData) override;

private:
    ScriptOutputAggregator &m_aggregator;   // 出力集約器
};

/**
 * @class RpmLoglineReceiver
 * @brief rpmログ行 (SingleTransReport) を受信する
 */
class RpmLoglineReceiver
    : public zypp::callback::ReceiveReport<zypp::target::rpm::SingleTransReport>
{
public:
    /**
     * @brief 構築する
     * @param aggregator 出力集約器
     */
    explicit RpmLoglineReceiver(ScriptOutputAggregator &aggregator);

    /**
     * @brief 汎用受信 (ログ行) を処理する
     * @param userData 受信データ
     */
    void report(const zypp::callback::UserData &userData) override;

private:
    ScriptOutputAggregator &m_aggregator;   // 出力集約器
};

} // namespace qZypper

#endif // QZYPPER_BACKEND_COMMITRECEIVERS_H
