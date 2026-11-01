#include <QByteArray>
#include <QChar>
#include <utility>
#include "CommitInteraction.h"

namespace qZypper {

/**
 * @brief UTF-8バイト長でテキストを上限内に切り詰める
 * @param text 対象テキスト
 * @param maxBytes 最大バイト数
 * @param truncated 切り詰め発生時にtrueを設定
 * @return 切り詰め後のテキスト
 */
QString capText(const QString &text, int maxBytes, bool *truncated)
{
    if (truncated)
        *truncated = false;

    if (maxBytes <= 0)
        return text;

    const QByteArray utf8 = text.toUtf8();
    if (utf8.size() <= maxBytes)
        return text;

    QByteArray cut = utf8.left(maxBytes);
    // 不完全な末尾バイト(継続バイト・不完全な先頭バイト) を除去する
    while (!cut.isEmpty()
           && (static_cast<unsigned char>(cut.at(cut.size() - 1)) & 0xC0) == 0x80) {
        cut.chop(1);
    }

    if (!cut.isEmpty() && static_cast<unsigned char>(cut.at(cut.size() - 1)) >= 0xC0)
        cut.chop(1);

    QString result = QString::fromUtf8(cut);
    while (result.endsWith(QChar(0xFFFD)))
        result.chop(1);

    if (truncated)
        *truncated = true;

    return result;
}

/**
 * @brief スクリプト出力1行をD-Busで安全に送れる形へ整える
 * @param line 元の行
 * @return 制御文字を除去し長さを上限内に収めた行
 */
QString sanitizeScriptLine(const QString &line)
{
    QString out;
    out.reserve(line.size());
    for (const QChar ch : line) {
        const ushort code = ch.unicode();
        if (code == u'\t') {
            out.append(ch);
            continue;
        }

        if (code < 0x20 || code == 0x7F) // 改行・復帰等の制御文字は除去
            continue;

        out.append(ch);
    }
    return capText(out, CommitLimits::kMaxScriptLineBytes);
}

/**
 * @brief ライセンス確認プロンプトのデータを構築する
 * @param package パッケージ名
 * @param version バージョン
 * @param licenseText ライセンス本文
 * @return プロンプトデータ
 */
QVariantMap buildLicensePromptData(const QString &package, const QString &version, const QString &licenseText)
{
    QVariantList buttons;
    buttons.append(QVariantMap{{QStringLiteral("action"), QStringLiteral("accept")},
                               {QStringLiteral("label"), QStringLiteral("Accept")}});
    buttons.append(QVariantMap{{QStringLiteral("action"), QStringLiteral("reject")},
                               {QStringLiteral("label"), QStringLiteral("Reject")}});

    QVariantMap data;
    data.insert(QStringLiteral("message"),
                QStringLiteral("Package \"%1\" requires a license agreement.").arg(package));
    data.insert(QStringLiteral("details"),
                capText(licenseText, CommitLimits::kMaxLicenseTextBytes));
    data.insert(QStringLiteral("package"), package);
    data.insert(QStringLiteral("version"), version);
    data.insert(QStringLiteral("buttons"), buttons);
    return data;
}

/**
 * @brief ライセンス確認の回答が承認かどうかを判定する
 * @param answer 回答マップ
 * @return "accept"のときtrue
 */
bool licenseAnswerAccepted(const QVariantMap &answer)
{
    return answer.value(QStringLiteral("action")).toString() == QStringLiteral("accept");
}

/**
 * @brief メディア交換プロンプトのデータを構築する
 * @param url 対象URL
 * @param label ラベル
 * @param mediaNr メディア番号
 * @param message 概要メッセージ
 * @param details 詳細メッセージ
 * @return プロンプトデータ
 */
QVariantMap buildMediaChangePromptData(const QString &url, const QString &label,
                                       unsigned int mediaNr, const QString &message,
                                       const QString &details)
{
    QVariantList buttons;
    buttons.append(QVariantMap{{QStringLiteral("action"), QStringLiteral("retry")},
                               {QStringLiteral("label"), QStringLiteral("Retry")}});
    buttons.append(QVariantMap{{QStringLiteral("action"), QStringLiteral("skip")},
                               {QStringLiteral("label"), QStringLiteral("Skip")}});
    buttons.append(QVariantMap{{QStringLiteral("action"), QStringLiteral("abort")},
                               {QStringLiteral("label"), QStringLiteral("Abort")}});

    QVariantMap data;
    data.insert(QStringLiteral("message"), message);
    data.insert(QStringLiteral("details"), details);
    data.insert(QStringLiteral("url"), url);
    data.insert(QStringLiteral("label"), label);
    data.insert(QStringLiteral("mediaNr"), QVariant::fromValue<quint32>(mediaNr));
    data.insert(QStringLiteral("buttons"), buttons);
    return data;
}

/**
 * @brief メディア交換の回答をアクション文字列へ変換する
 * @param answer 回答マップ
 * @return "retry" / "skip" / "abort"
 */
QString mediaAnswerAction(const QVariantMap &answer)
{
    const QString action = answer.value(QStringLiteral("action")).toString();

    if (action == QStringLiteral("retry") || action == QStringLiteral("skip"))
        return action;

    return QStringLiteral("abort");
}

/**
 * @brief パッケージ操作エラープロンプトのデータを構築する
 * @param operation 操作種別 [download]/[install]/[remove]
 * @param package 対象パッケージ名
 * @param errorKind エラー種別
 * @param description エラー詳細
 * @param actions 提示するアクション [abort]/[retry]/[ignore]
 * @return [download-error]/[install-error]/[remove-error]用のプロンプトデータ
 */
QVariantMap buildProblemPromptData(const QString &operation, const QString &package,
                                   const QString &errorKind, const QString &description,
                                   const QStringList &actions)
{
    // 操作種別を表示用の見出しへ変換する
    QString operationLabel = operation;
    if (operation == QStringLiteral("download")) {
        operationLabel = QStringLiteral("Download");
    }
    else if (operation == QStringLiteral("install")) {
        operationLabel = QStringLiteral("Installation");
    }
    else if (operation == QStringLiteral("remove")) {
        operationLabel = QStringLiteral("Removal");
    }

    const QString message = package.isEmpty()
        ? QStringLiteral("%1 error (%2).").arg(operationLabel, errorKind)
        : QStringLiteral("%1 error for %2 (%3).").arg(operationLabel, package, errorKind);

    // 提示するアクションごとにボタン定義を作る
    QVariantList buttons;
    for (const QString &action : actions) {
        QString label = QStringLiteral("Abort");
        if (action == QStringLiteral("retry")) {
            label = QStringLiteral("Retry");
        }
        else if (action == QStringLiteral("ignore")) {
            label = QStringLiteral("Ignore");
        }

        buttons.append(QVariantMap{{QStringLiteral("action"), action},
                                   {QStringLiteral("label"), label}});
    }

    QVariantMap data;
    data.insert(QStringLiteral("message"), message);
    data.insert(QStringLiteral("details"), capText(description, CommitLimits::kMaxProblemTextBytes));
    data.insert(QStringLiteral("operation"), operation);
    data.insert(QStringLiteral("package"), package);
    data.insert(QStringLiteral("errorKind"), errorKind);
    data.insert(QStringLiteral("actions"), actions);
    data.insert(QStringLiteral("buttons"), buttons);
    return data;
}

/**
 * @brief エラープロンプトの回答を許可されたアクションへ正規化する
 * @param answer 回答マップ
 * @param allowed 許可されたアクション一覧
 * @return 許可されたアクション
 * @note 不正または不明なときは[abort]
 */
QString problemAnswerAction(const QVariantMap &answer, const QStringList &allowed)
{
    const QString action = answer.value(QStringLiteral("action")).toString();

    // 許可されたアクションだけを採用する
    if (allowed.contains(action)) {
        return action;
    }

    return QStringLiteral("abort");
}

/**
 * @brief ファイル競合プロンプトのデータを構築する
 * @param conflicts 競合説明行
 * @param totalCount 競合総数
 * @return プロンプトデータ
 */
QVariantMap buildFileConflictPromptData(const QStringList &conflicts, int totalCount)
{
    QVariantList buttons;
    buttons.append(QVariantMap{{QStringLiteral("action"), QStringLiteral("continue")},
                               {QStringLiteral("label"), QStringLiteral("Continue")}});
    buttons.append(QVariantMap{{QStringLiteral("action"), QStringLiteral("abort")},
                               {QStringLiteral("label"), QStringLiteral("Abort")}});

    QString details = conflicts.join(QChar(u'\n'));
    details = capText(details, CommitLimits::kMaxConflictTextBytes);

    QVariantMap data;
    data.insert(QStringLiteral("message"),
                QStringLiteral("Package file conflicts were detected."));
    data.insert(QStringLiteral("details"), details);
    data.insert(QStringLiteral("count"), totalCount);
    data.insert(QStringLiteral("buttons"), buttons);
    return data;
}

/**
 * @brief ファイル競合の回答が続行かどうかを判定する
 * @param answer 回答マップ
 * @return "continue"のときはtrue
 */
bool fileConflictAnswerContinue(const QVariantMap &answer)
{
    return answer.value(QStringLiteral("action")).toString() == QStringLiteral("continue");
}

// ─── ProgressThrottle ────────────────────────

/**
 * @brief サンプルを送信すべきか判定し、送信時は内部状態を更新する
 * @param sample 判定対象の進捗サンプル
 * @param now 現在時刻
 * @return 送信する場合true
 * @note 初回、package/stage/ステップ数の変化、0/100%、前回送信から
 *       kProgressIntervalMs 以上の経過のいずれかで送信する。
 *       送信しなかったサンプルは最新1件を保留し、takePending() で回収できる
 */
bool ProgressThrottle::shouldEmit(const ProgressSample &sample,
                                  std::chrono::steady_clock::time_point now)
{
    // 初回は必ず送る
    if (!m_hasLast) {
        m_last = sample;
        m_lastEmit = now;
        m_hasLast = true;
        return true;
    }

    // 全項目が同じサンプルは再送しない
    const bool identical = sample.package == m_last.package
        && sample.stage == m_last.stage
        && sample.percentage == m_last.percentage
        && sample.totalSteps == m_last.totalSteps
        && sample.completedSteps == m_last.completedSteps
        && sample.overallPercentage == m_last.overallPercentage;
    if (identical) {
        // 最新値が送信済みと一致するため、それ以前の保留は古い値になっている
        m_pending.reset();
        return false;
    }

    // パッケージ・ステージ・ステップ数の変化は即時通知する
    bool accepted = sample.package != m_last.package
        || sample.stage != m_last.stage
        || sample.totalSteps != m_last.totalSteps
        || sample.completedSteps != m_last.completedSteps;

    // 0%と100%は間引かずに通知する
    if (!accepted && (sample.percentage == 0 || sample.percentage == 100))
        accepted = true;

    // 前回送信から一定時間が経過していれば通知する
    if (!accepted
        && (now - m_lastEmit) >= std::chrono::milliseconds(CommitLimits::kProgressIntervalMs))
        accepted = true;

    if (accepted) {
        m_last = sample;
        m_lastEmit = now;
        m_pending.reset();
    } else {
        // 間引いたサンプルを最新1件だけ保留し、takePending() で送り漏れを回収できるようにする
        m_pending = sample;
    }
    return accepted;
}

/**
 * @brief 間引かれて未送信の最新サンプルを取り出し、送信済みとして記録する
 * @param now 現在時刻
 * @return 未送信サンプル。無ければ std::nullopt
 * @note 取り出したサンプルは呼出側が必ず送信する前提で m_last / m_lastEmit に反映する。
 *       これにより同じサンプルの二重送信や、直後の間引き判定の基準ずれを防ぐ
 */
std::optional<ProgressSample> ProgressThrottle::takePending(std::chrono::steady_clock::time_point now)
{
    if (!m_pending)
        return std::nullopt;

    std::optional<ProgressSample> pending = std::move(m_pending);
    m_pending.reset();
    m_last = *pending;
    m_lastEmit = now;
    m_hasLast = true;
    return pending;
}

// ─── ScriptOutputAggregator ────────────────────────

/**
 * @brief 集約器を構築する
 * @param sink フラッシュ時に呼ぶコールバック
 */
ScriptOutputAggregator::ScriptOutputAggregator(ScriptOutputFn sink)
    : m_emit(std::move(sink))
{
}

/**
 * @brief 集約器をバッチ送出先付きで構築する
 * @param batchSink フラッシュ時にまとめて呼ぶコールバック
 */
ScriptOutputAggregator::ScriptOutputAggregator(ScriptOutputBatchFn batchSink)
    : m_emitBatch(std::move(batchSink))
{
}

/**
 * @brief スクリプト出力行を追加する
 * @param package パッケージ名
 * @param scriptType スクリプト種別
 * @param line 出力行
 * @param isError エラー出力か
 * @param now 現在時刻
 */
void ScriptOutputAggregator::addLine(const QString &package, const QString &scriptType,
                                     const QString &line, bool isError,
                                     Clock::time_point now)
{
    const QString clean = sanitizeScriptLine(line);
    if (clean.isEmpty())
        return;

    m_pending.push_back(ScriptOutputEntry{package, scriptType, clean, isError});
    m_collected.append(clean);

    if (m_collected.size() > CommitLimits::kMaxScriptLines) {
        m_collected.removeFirst();
        m_truncated = true;
    }

    maybeFlush(now);
}

/**
 * @brief rpmログメッセージを追加する
 * @param line メッセージ
 * @param isError エラー相当か
 * @param now 現在時刻
 */
void ScriptOutputAggregator::addRpmMessage(const QString &line, bool isError,
                                           Clock::time_point now)
{
    const QString clean = sanitizeScriptLine(line);
    if (clean.isEmpty())
        return;

    m_pending.push_back(ScriptOutputEntry{QString(), QStringLiteral("rpm"), clean, isError});
    m_rpmMessages.append(clean);

    if (m_rpmMessages.size() > CommitLimits::kMaxScriptLines) {
        m_rpmMessages.removeFirst();
        m_truncated = true;
    }

    maybeFlush(now);
}

/**
 * @brief 閾値を判定し、必要ならフラッシュする
 * @param now 現在時刻
 */
void ScriptOutputAggregator::maybeFlush(Clock::time_point now)
{
    const bool due = m_pending.size() >= static_cast<std::size_t>(CommitLimits::kFlushLineThreshold) ||
                     !m_hasFlushed ||
                     (now - m_lastFlush) >= std::chrono::milliseconds(CommitLimits::kFlushIntervalMs);

    if (due)
        flush(now);
}

/**
 * @brief 保留中の行を全てフラッシュする
 * @param now 現在時刻
 */
void ScriptOutputAggregator::flush(Clock::time_point now)
{
    if (m_emitBatch) {
        if (!m_pending.empty())
            m_emitBatch(m_pending);
    } else if (m_emit) {
        for (const ScriptOutputEntry &pending : m_pending)
            m_emit(pending.package, pending.scriptType, pending.line, pending.isError);
    }

    m_pending.clear();
    m_hasFlushed = true;
    m_lastFlush = now;
}

} // namespace qZypper
