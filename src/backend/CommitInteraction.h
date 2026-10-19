#ifndef QZYPPER_BACKEND_COMMITINTERACTION_H
#define QZYPPER_BACKEND_COMMITINTERACTION_H

#include <QString>
#include <QStringList>
#include <QVariantMap>
#include <chrono>
#include <functional>
#include <mutex>
#include <string>
#include <vector>
#include "PromptRegistry.h"

namespace qZypper {

/**
 * @brief プロンプト問い合わせコールバック型
 *
 * zyppワーカースレッド上でPromptRegistryを呼び出し、所有者の回答を返す
 */
using PromptAskFn = std::function<QVariantMap(const PromptRequest &request)>;

/**
 * @brief RPM/スクリプト出力1行を通知するコールバック型
 * @param package 対象パッケージ名 (不明時は空)
 * @param scriptType スクリプト種別 ("pre"/"post"/"preun"/"postun"/"pretrans"/"posttrans"/"rpm")
 * @param line 出力行
 * @param isError エラー出力かどうか
 */
using ScriptOutputFn = std::function<void(const QString &package, const QString &scriptType, const QString &line, bool isError)>;

/**
 * @brief コミット時インタラクションの各種上限値
 */
namespace CommitLimits {
constexpr int kMaxLicenseTextBytes = 256 * 1024;   // ライセンス本文の上限 (256 KiB)
constexpr int kMaxConflictTextBytes = 64 * 1024;   // 競合詳細本文の上限 (64 KiB)
constexpr int kMaxScriptLineBytes = 8 * 1024;      // スクリプト1行の上限 (8 KiB)
constexpr int kMaxScriptLines = 500;               // Commit返信に含める最大行数
constexpr int kFlushLineThreshold = 50;            // この行数でフラッシュ
constexpr int kFlushIntervalMs = 100;              // この経過時間でフラッシュ
constexpr int kMaxConflictsReported = 200;         // 競合として列挙する最大件数
} // namespace CommitLimits

/**
 * @brief UTF-8バイト長でテキストを上限内に切り詰める
 * @param text 対象テキスト
 * @param maxBytes 最大バイト数
 * @param truncated 切り詰めが発生した場合にtrueを設定 (nullptr可)
 * @return 切り詰め後のテキスト (不正な末尾バイトは除去)
 */
QString capText(const QString &text, int maxBytes, bool *truncated = nullptr);

/**
 * @brief スクリプト出力1行をD-Bus安全な形へ整える
 * @param line 元の行
 * @return 制御文字を除去し長さを上限内に収めた行
 */
QString sanitizeScriptLine(const QString &line);

/**
 * @brief ライセンス確認プロンプトのデータを構築する
 * @param package パッケージ名
 * @param version バージョン (edition文字列)
 * @param licenseText ライセンス本文 (内部で256 KiBに切り詰め)
 * @return type "license"用data (message/details/package/version/buttons)
 */
QVariantMap buildLicensePromptData(const QString &package, const QString &version,
                                   const QString &licenseText);

/**
 * @brief ライセンス回答が承認かどうかを判定する
 * @param answer AnswerPromptの回答マップ
 * @return "action"が"accept"のときtrue
 */
bool licenseAnswerAccepted(const QVariantMap &answer);

/**
 * @brief メディア交換プロンプトのデータを構築する
 * @param url 対象URL
 * @param label メディアラベル
 * @param mediaNr メディア番号
 * @param message 概要メッセージ
 * @param details 詳細メッセージ
 * @return type "media-change"用data
 */
QVariantMap buildMediaChangePromptData(const QString &url, const QString &label,
                                       unsigned int mediaNr, const QString &message,
                                       const QString &details);

/**
 * @brief メディア交換の回答を正規化されたアクション文字列へ変換する
 * @param answer AnswerPromptの回答マップ
 * @return "retry" / "skip" / "abort" (不明時は"abort")
 */
QString mediaAnswerAction(const QVariantMap &answer);

/**
 * @brief ファイル競合プロンプトのデータを構築する
 * @param conflicts 競合の説明行一覧
 * @param totalCount 競合総数
 * @return type "file-conflicts"用data (message/details/count/buttons)
 */
QVariantMap buildFileConflictPromptData(const QStringList &conflicts, int totalCount);

/**
 * @brief ファイル競合の回答が続行かどうかを判定する
 * @param answer AnswerPromptの回答マップ
 * @return "action"が"continue"のときtrue
 */
bool fileConflictAnswerContinue(const QVariantMap &answer);

/**
 * @class ScriptOutputAggregator
 * @brief スクリプト/rpm出力を集約・レート制限して通知する
 *
 * 到着行を内部バッファに貯め、閾値行数または経過時間でまとめてコールバックへ流す
 * 収集した最新500行とrpmメッセージも保持し、Commit返信に利用できる
 */
class ScriptOutputAggregator
{
public:
    using Clock = std::chrono::steady_clock;

    /**
     * @brief 集約器を構築する
     * @param sink フラッシュ時に1行ずつ呼ぶコールバック (nullptr可)
     */
    explicit ScriptOutputAggregator(ScriptOutputFn sink = nullptr);

    /**
     * @brief スクリプト出力行を追加する
     * @param package パッケージ名
     * @param scriptType スクリプト種別
     * @param line 出力行
     * @param isError エラー出力か
     * @param now 現在時刻 (テスト用に注入可能)
     */
    void addLine(const QString &package, const QString &scriptType, const QString &line,
                 bool isError, Clock::time_point now = Clock::now());

    /**
     * @brief rpmログメッセージを追加する
     * @param line メッセージ行
     * @param isError エラー相当か
     * @param now 現在時刻
     */
    void addRpmMessage(const QString &line, bool isError, Clock::time_point now = Clock::now());

    /**
     * @brief 保留中の行を全てフラッシュする
     * @param now 現在時刻 (テスト用に注入可能)
     */
    void flush(Clock::time_point now = Clock::now());

    /**
     * @brief 収集済みスクリプト出力 (最新500行)
     * @return 行一覧
     */
    const QStringList &collected() const { return m_collected; }

    /**
     * @brief 収集済みrpmメッセージ (最新500行)
     * @return 行一覧
     */
    const QStringList &rpmMessages() const { return m_rpmMessages; }

    /**
     * @brief 収集時に切り捨てが発生したか
     * @return 発生していればtrue
     */
    bool truncated() const { return m_truncated; }

    /**
     * @brief 未フラッシュの保留行数
     * @return 保留行数
     */
    int pendingCount() const { return static_cast<int>(m_pending.size()); }

private:
    /**
     * @struct Pending
     * @brief フラッシュ待ちのスクリプト出力1行
     */
    struct Pending {
        QString package;                // パッケージ名
        QString scriptType;             // スクリプト種別
        QString line;                   // 出力行
        bool isError = false;           // エラー出力か
    };

    /**
     * @brief 閾値判定を行い、必要な場合にフラッシュする
     * @param now 現在時刻
     */
    void maybeFlush(Clock::time_point now);

    ScriptOutputFn m_emit;              // フラッシュ時の送出先
    std::vector<Pending> m_pending;     // 未送出の行
    QStringList m_collected;            // コミット結果に含めるスクリプト出力
    QStringList m_rpmMessages;          // コミット結果に含めるrpmメッセージ
    bool m_truncated = false;           // 上限超過で切り詰めたか
    Clock::time_point m_lastFlush{};    // 最後にフラッシュした時刻
    bool m_hasFlushed = false;          // 一度でもフラッシュしたか
};

} // namespace qZypper

#endif // QZYPPER_BACKEND_COMMITINTERACTION_H
