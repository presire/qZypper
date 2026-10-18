#ifndef QZYPPER_BACKEND_COMMITINTERACTION_H
#define QZYPPER_BACKEND_COMMITINTERACTION_H

#include <QString>
#include <QStringList>
#include <QVariantMap>

#include <atomic>
#include <chrono>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

#include <zypp/ZYppCallbacks.h>
#include <zypp/ProgressData.h>
#include <zypp/Url.h>
#include <zypp/Resolvable.h>
#include <zypp/sat/FileConflicts.h>
#include <zypp/sat/Queue.h>

#include "PromptRegistry.h"

namespace qZypper {

/**
 * @brief プロンプト問い合わせコールバック型。
 *
 * zypp ワーカースレッド上で PromptRegistry を呼び出し、所有者の回答を返す。
 */
using PromptAskFn = std::function<QVariantMap(const PromptRequest &request)>;

/**
 * @brief RPM/スクリプト出力 1 行を通知するコールバック型。
 * @param package 対象パッケージ名 (不明時は空)
 * @param scriptType スクリプト種別 ("pre"/"post"/"preun"/"postun"/"pretrans"/"posttrans"/"rpm")
 * @param line 出力行
 * @param isError エラー出力かどうか
 */
using ScriptOutputFn =
    std::function<void(const QString &package, const QString &scriptType,
                       const QString &line, bool isError)>;

/**
 * @brief コミット時インタラクションの各種上限値。
 */
namespace CommitLimits {
constexpr int kMaxLicenseTextBytes = 256 * 1024;   // ライセンス本文の上限 (256 KiB)
constexpr int kMaxConflictTextBytes = 64 * 1024;   // 競合詳細本文の上限 (64 KiB)
constexpr int kMaxScriptLineBytes = 8 * 1024;      // スクリプト 1 行の上限 (8 KiB)
constexpr int kMaxScriptLines = 500;               // Commit 返信に含める最大行数
constexpr int kFlushLineThreshold = 50;            // この行数でフラッシュ
constexpr int kFlushIntervalMs = 100;              // この経過時間でフラッシュ
constexpr int kMaxConflictsReported = 200;         // 競合として列挙する最大件数
} // namespace CommitLimits

/**
 * @brief UTF-8 バイト長でテキストを上限内に切り詰める。
 * @param text 対象テキスト
 * @param maxBytes 最大バイト数
 * @param truncated 切り詰めが発生した場合に true を設定 (nullptr 可)
 * @return 切り詰め後のテキスト (不正な末尾バイトは除去)
 */
QString capText(const QString &text, int maxBytes, bool *truncated = nullptr);

/**
 * @brief スクリプト出力 1 行を D-Bus 安全な形へ整える。
 * @param line 元の行
 * @return 制御文字を除去し長さを上限内に収めた行
 */
QString sanitizeScriptLine(const QString &line);

/**
 * @brief ライセンス確認プロンプトのデータを構築する。
 * @param package パッケージ名
 * @param version バージョン (edition 文字列)
 * @param licenseText ライセンス本文 (内部で 256 KiB に切り詰め)
 * @return type "license" 用 data (message/details/package/version/buttons)
 */
QVariantMap buildLicensePromptData(const QString &package, const QString &version,
                                   const QString &licenseText);

/**
 * @brief ライセンス回答が承認かどうかを判定する。
 * @param answer AnswerPrompt の回答マップ
 * @return "action" が "accept" のとき true
 */
bool licenseAnswerAccepted(const QVariantMap &answer);

/**
 * @brief メディア交換プロンプトのデータを構築する。
 * @param url 対象 URL
 * @param label メディアラベル
 * @param mediaNr メディア番号
 * @param message 概要メッセージ
 * @param details 詳細メッセージ
 * @return type "media-change" 用 data
 */
QVariantMap buildMediaChangePromptData(const QString &url, const QString &label,
                                       unsigned int mediaNr, const QString &message,
                                       const QString &details);

/**
 * @brief メディア交換の回答を正規化されたアクション文字列へ変換する。
 * @param answer AnswerPrompt の回答マップ
 * @return "retry" / "skip" / "abort" (不明時は "abort")
 */
QString mediaAnswerAction(const QVariantMap &answer);

/**
 * @brief ファイル競合プロンプトのデータを構築する。
 * @param conflicts 競合の説明行一覧
 * @param totalCount 競合総数
 * @return type "file-conflicts" 用 data (message/details/count/buttons)
 */
QVariantMap buildFileConflictPromptData(const QStringList &conflicts, int totalCount);

/**
 * @brief ファイル競合の回答が続行かどうかを判定する。
 * @param answer AnswerPrompt の回答マップ
 * @return "action" が "continue" のとき true
 */
bool fileConflictAnswerContinue(const QVariantMap &answer);

/**
 * @brief スクリプト/rpm 出力を集約・レート制限して通知する。
 *
 * 到着行を内部バッファに貯め、閾値行数または経過時間でまとめてコールバックへ流す。
 * 収集した最新 500 行と rpm メッセージも保持し、Commit 返信に利用できる。
 */
class ScriptOutputAggregator
{
public:
    using Clock = std::chrono::steady_clock;

    /**
     * @brief 集約器を構築する。
     * @param sink フラッシュ時に 1 行ずつ呼ぶコールバック (nullptr 可)
     */
    explicit ScriptOutputAggregator(ScriptOutputFn sink = nullptr);

    /**
     * @brief スクリプト出力行を追加する。
     * @param package パッケージ名
     * @param scriptType スクリプト種別
     * @param line 出力行
     * @param isError エラー出力か
     * @param now 現在時刻 (テスト用に注入可能)
     */
    void addLine(const QString &package, const QString &scriptType, const QString &line,
                 bool isError, Clock::time_point now = Clock::now());

    /**
     * @brief rpm ログメッセージを追加する。
     * @param line メッセージ行
     * @param isError エラー相当か
     * @param now 現在時刻
     */
    void addRpmMessage(const QString &line, bool isError,
                       Clock::time_point now = Clock::now());

    /**
     * @brief 保留中の行を全てフラッシュする。
     * @param now 現在時刻 (テスト用に注入可能)
     */
    void flush(Clock::time_point now = Clock::now());

    /**
     * @brief 収集済みスクリプト出力 (最新 500 行)。
     * @return 行一覧
     */
    const QStringList &collected() const { return m_collected; }

    /**
     * @brief 収集済み rpm メッセージ (最新 500 行)。
     * @return 行一覧
     */
    const QStringList &rpmMessages() const { return m_rpmMessages; }

    /**
     * @brief 収集時に切り捨てが発生したか。
     * @return 発生していれば true
     */
    bool truncated() const { return m_truncated; }

    /**
     * @brief 未フラッシュの保留行数。
     * @return 保留行数
     */
    int pendingCount() const { return static_cast<int>(m_pending.size()); }

private:
    struct Pending {
        QString package;
        QString scriptType;
        QString line;
        bool isError = false;
    };

    /**
     * @brief 閾値判定を行い、必要な場合にフラッシュする。
     * @param now 現在時刻
     */
    void maybeFlush(Clock::time_point now);

    ScriptOutputFn m_emit;
    std::vector<Pending> m_pending;
    QStringList m_collected;
    QStringList m_rpmMessages;
    bool m_truncated = false;
    Clock::time_point m_lastFlush{};
    bool m_hasFlushed = false;
};

/**
 * @brief メディア交換要求を PromptRegistry 経由で所有者へ問い合わせる。
 */
class MediaChangeReceiver
    : public zypp::callback::ReceiveReport<zypp::media::MediaChangeReport>
{
public:
    /**
     * @brief 構築する。
     * @param ask プロンプト問い合わせコールバック
     */
    explicit MediaChangeReceiver(PromptAskFn ask);

    /**
     * @brief メディア交換要求を処理する。
     * @param url 対象 URL (I/O)
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
     * @brief 直近の回答が中断だったか。
     * @return ABORT を返した場合 true
     */
    bool aborted() const { return m_aborted.load(); }

private:
    PromptAskFn m_ask;
    std::atomic<bool> m_aborted{false};
};

/**
 * @brief ファイル競合検出を PromptRegistry 経由で所有者へ問い合わせる。
 */
class FileConflictReceiver
    : public zypp::callback::ReceiveReport<zypp::target::FindFileConflictstReport>
{
public:
    /**
     * @brief 構築する。
     * @param ask プロンプト問い合わせコールバック
     */
    explicit FileConflictReceiver(PromptAskFn ask);

    /**
     * @brief 競合検出結果を処理する。
     * @param progress 進捗
     * @param noFilelist ファイルリスト未取得の solvable
     * @param conflicts 検出された競合
     * @return 続行なら true、中断なら false
     */
    bool result(const zypp::ProgressData &progress, const zypp::sat::Queue &noFilelist,
                const zypp::sat::FileConflicts &conflicts) override;

    /**
     * @brief ユーザーが中断を選択したか。
     * @return 中断の場合 true
     */
    bool aborted() const { return m_aborted.load(); }

    /**
     * @brief 中断時のメッセージ。
     * @return メッセージ
     */
    QString abortMessage() const;

private:
    PromptAskFn m_ask;
    std::atomic<bool> m_aborted{false};
    mutable std::mutex m_mutex;
    QString m_abortMessage;
};

/**
 * @brief %pre/%post 等スクリプトレット出力を受信する。
 */
class CommitScriptReceiver
    : public zypp::callback::ReceiveReport<zypp::target::rpm::CommitScriptReportSA>
{
public:
    /**
     * @brief 構築する。
     * @param aggregator 出力集約器
     */
    explicit CommitScriptReceiver(ScriptOutputAggregator &aggregator);

    /**
     * @brief 汎用受信 (rpm 出力行) を処理する。
     * @param userData 受信データ
     */
    void report(const zypp::callback::UserData &userData) override;

    /**
     * @brief スクリプト開始を処理する。
     * @param scriptType スクリプト種別
     * @param packageName パッケージ名 (空可)
     * @param resolvable 対象 (null 可)
     * @param userData 受信データ
     */
    void start(const std::string &scriptType, const std::string &packageName,
               zypp::Resolvable::constPtr resolvable,
               const zypp::callback::UserData &userData) override;

    /**
     * @brief スクリプト終了を処理する。
     * @param resolvable 対象 (null 可)
     * @param error 終了ステータス
     * @param userData 受信データ
     */
    void finish(zypp::Resolvable::constPtr resolvable, Error error,
                const zypp::callback::UserData &userData) override;

private:
    /**
     * @brief userData から rpm 出力行を取り出す。
     * @param userData 受信データ
     * @param isError エラー相当かどうか
     * @return 行 (無ければ空)
     */
    QString extractLine(const zypp::callback::UserData &userData, bool *isError) const;

    ScriptOutputAggregator &m_aggregator;
    mutable std::mutex m_mutex;
    QString m_scriptType;
    QString m_package;
};

/**
 * @brief 汎用 rpm メッセージ (TransactionReportSA) を受信する。
 */
class RpmOutputReceiver
    : public zypp::callback::ReceiveReport<zypp::target::rpm::TransactionReportSA>
{
public:
    /**
     * @brief 構築する。
     * @param aggregator 出力集約器
     */
    explicit RpmOutputReceiver(ScriptOutputAggregator &aggregator);

    /**
     * @brief 汎用受信 (rpm 出力行) を処理する。
     * @param userData 受信データ
     */
    void report(const zypp::callback::UserData &userData) override;

private:
    ScriptOutputAggregator &m_aggregator;
};

/**
 * @brief rpm ログ行 (SingleTransReport) を受信する。
 */
class RpmLoglineReceiver
    : public zypp::callback::ReceiveReport<zypp::target::rpm::SingleTransReport>
{
public:
    /**
     * @brief 構築する。
     * @param aggregator 出力集約器
     */
    explicit RpmLoglineReceiver(ScriptOutputAggregator &aggregator);

    /**
     * @brief 汎用受信 (ログ行) を処理する。
     * @param userData 受信データ
     */
    void report(const zypp::callback::UserData &userData) override;

private:
    ScriptOutputAggregator &m_aggregator;
};

} // namespace qZypper

#endif // QZYPPER_BACKEND_COMMITINTERACTION_H
