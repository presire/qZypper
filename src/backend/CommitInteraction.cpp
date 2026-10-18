#include "CommitInteraction.h"

#include <QByteArray>
#include <QChar>

#include <utility>

#include <zypp/sat/Solvable.h>

namespace qZypper {

namespace {

/**
 * @brief MediaChangeReport の Error を短い英語メッセージへ変換する。
 * @param error エラー種別
 * @return メッセージ
 */
QString mediaErrorMessage(zypp::media::MediaChangeReport::Error error)
{
    switch (error) {
    case zypp::media::MediaChangeReport::NO_ERROR:
        return QStringLiteral("Media change required.");
    case zypp::media::MediaChangeReport::NOT_FOUND:
        return QStringLiteral("The requested media was not found.");
    case zypp::media::MediaChangeReport::IO:
        return QStringLiteral("An I/O error occurred while accessing the media.");
    case zypp::media::MediaChangeReport::INVALID:
        return QStringLiteral("The media is broken or invalid.");
    case zypp::media::MediaChangeReport::WRONG:
        return QStringLiteral("The wrong media was inserted.");
    case zypp::media::MediaChangeReport::IO_SOFT:
        return QStringLiteral("A recoverable I/O error occurred while accessing the media.");
    }
    return QStringLiteral("Media change required.");
}

/**
 * @brief UserData から rpm 出力行を取り出す。
 * @param userData 受信データ
 * @return 行 (取得できなければ空)
 */
QString lineFromUserData(const zypp::callback::UserData &userData)
{
    if (!userData.haskey("line"))
        return QString();
    try {
        const std::reference_wrapper<const std::string> ref =
            userData.get<std::reference_wrapper<const std::string>>("line");
        return QString::fromStdString(ref.get());
    } catch (const zypp::callback::UserData::bad_AnyType_cast &) {
        // 型が異なる場合は std::string として再試行する
    } catch (...) {
        return QString();
    }
    try {
        return QString::fromStdString(userData.get<std::string>("line"));
    } catch (...) {
        return QString();
    }
}

} // anonymous namespace

/**
 * @brief UTF-8 バイト長でテキストを上限内に切り詰める。
 * @param text 対象テキスト
 * @param maxBytes 最大バイト数
 * @param truncated 切り詰め発生時に true を設定
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
    // 不完全な末尾バイト (継続バイト・不完全な先頭バイト) を除去する
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
 * @brief スクリプト出力 1 行を D-Bus 安全な形へ整える。
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
 * @brief ライセンス確認プロンプトのデータを構築する。
 * @param package パッケージ名
 * @param version バージョン
 * @param licenseText ライセンス本文
 * @return プロンプト data
 */
QVariantMap buildLicensePromptData(const QString &package, const QString &version,
                                   const QString &licenseText)
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
 * @brief ライセンス回答が承認かどうかを判定する。
 * @param answer 回答マップ
 * @return "accept" のとき true
 */
bool licenseAnswerAccepted(const QVariantMap &answer)
{
    return answer.value(QStringLiteral("action")).toString() == QStringLiteral("accept");
}

/**
 * @brief メディア交換プロンプトのデータを構築する。
 * @param url 対象 URL
 * @param label ラベル
 * @param mediaNr メディア番号
 * @param message 概要メッセージ
 * @param details 詳細メッセージ
 * @return プロンプト data
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
 * @brief メディア交換の回答をアクション文字列へ変換する。
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
 * @brief ファイル競合プロンプトのデータを構築する。
 * @param conflicts 競合説明行
 * @param totalCount 競合総数
 * @return プロンプト data
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
 * @brief ファイル競合の回答が続行かどうかを判定する。
 * @param answer 回答マップ
 * @return "continue" のとき true
 */
bool fileConflictAnswerContinue(const QVariantMap &answer)
{
    return answer.value(QStringLiteral("action")).toString() == QStringLiteral("continue");
}

// ─── ScriptOutputAggregator ────────────────────────

/**
 * @brief 集約器を構築する。
 * @param emit フラッシュ時に呼ぶコールバック
 */
ScriptOutputAggregator::ScriptOutputAggregator(ScriptOutputFn sink)
    : m_emit(std::move(sink))
{
}

/**
 * @brief スクリプト出力行を追加する。
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

    m_pending.push_back(Pending{package, scriptType, clean, isError});
    m_collected.append(clean);
    if (m_collected.size() > CommitLimits::kMaxScriptLines) {
        m_collected.removeFirst();
        m_truncated = true;
    }
    maybeFlush(now);
}

/**
 * @brief rpm ログメッセージを追加する。
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

    m_pending.push_back(Pending{QString(), QStringLiteral("rpm"), clean, isError});
    m_rpmMessages.append(clean);
    if (m_rpmMessages.size() > CommitLimits::kMaxScriptLines) {
        m_rpmMessages.removeFirst();
        m_truncated = true;
    }
    maybeFlush(now);
}

/**
 * @brief 閾値判定を行い必要ならフラッシュする。
 * @param now 現在時刻
 */
void ScriptOutputAggregator::maybeFlush(Clock::time_point now)
{
    const bool due = m_pending.size() >= static_cast<std::size_t>(CommitLimits::kFlushLineThreshold)
        || !m_hasFlushed
        || (now - m_lastFlush) >= std::chrono::milliseconds(CommitLimits::kFlushIntervalMs);
    if (due)
        flush(now);
}

/**
 * @brief 保留中の行を全てフラッシュする。
 * @param now 現在時刻
 */
void ScriptOutputAggregator::flush(Clock::time_point now)
{
    if (m_emit) {
        for (const Pending &pending : m_pending)
            m_emit(pending.package, pending.scriptType, pending.line, pending.isError);
    }
    m_pending.clear();
    m_hasFlushed = true;
    m_lastFlush = now;
}

// ─── MediaChangeReceiver ────────────────────────

/**
 * @brief 構築する。
 * @param ask プロンプト問い合わせコールバック
 */
MediaChangeReceiver::MediaChangeReceiver(PromptAskFn ask)
    : m_ask(std::move(ask))
{
}

/**
 * @brief メディア交換要求を処理する。
 * @param url 対象 URL
 * @param mediumNr メディア番号
 * @param label ラベル
 * @param error エラー種別
 * @param description 説明
 * @param devices 利用可能デバイス
 * @param dev_current デバイスインデックス
 * @return RETRY / IGNORE / ABORT
 */
MediaChangeReceiver::Action MediaChangeReceiver::requestMedia(
    zypp::Url &url, unsigned int mediumNr, const std::string &label, Error error,
    const std::string &description, const std::vector<std::string> &devices,
    unsigned int &dev_current)
{
    (void)devices;
    (void)dev_current;
    if (!m_ask)
        return ABORT;

    PromptRequest request;
    request.type = QStringLiteral("media-change");
    request.data = buildMediaChangePromptData(
        QString::fromStdString(url.asString()), QString::fromStdString(label), mediumNr,
        mediaErrorMessage(error), QString::fromStdString(description));
    request.actions = {QStringLiteral("retry"), QStringLiteral("skip"),
                       QStringLiteral("abort")};
    request.safeDefault = QVariantMap{{QStringLiteral("action"), QStringLiteral("abort")}};
    request.timeout = std::chrono::milliseconds(300000);

    const QString action = mediaAnswerAction(m_ask(request));
    if (action == QStringLiteral("retry"))
        return RETRY;
    if (action == QStringLiteral("skip"))
        return IGNORE;

    m_aborted.store(true);
    return ABORT;
}

// ─── FileConflictReceiver ────────────────────────

/**
 * @brief 構築する。
 * @param ask プロンプト問い合わせコールバック
 */
FileConflictReceiver::FileConflictReceiver(PromptAskFn ask)
    : m_ask(std::move(ask))
{
}

/**
 * @brief 競合検出結果を処理する。
 * @param progress 進捗
 * @param noFilelist ファイルリスト未取得の solvable
 * @param conflicts 検出された競合
 * @return 続行なら true、中断なら false
 */
bool FileConflictReceiver::result(const zypp::ProgressData &progress,
                                  const zypp::sat::Queue &noFilelist,
                                  const zypp::sat::FileConflicts &conflicts)
{
    (void)progress;
    (void)noFilelist;

    const int total = static_cast<int>(conflicts.size());
    if (total <= 0)
        return true;

    if (!m_ask) {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_aborted.store(true);
        m_abortMessage = QStringLiteral("File conflicts detected but no confirmation channel is available");
        return false;
    }

    QStringList lines;
    int reported = 0;
    for (const auto &conflict : conflicts) {
        if (reported >= CommitLimits::kMaxConflictsReported)
            break;
        lines.append(QString::fromStdString(conflict.asUserString()));
        ++reported;
    }
    if (reported < total)
        lines.append(QStringLiteral("... and %1 more conflict(s)").arg(total - reported));

    PromptRequest request;
    request.type = QStringLiteral("file-conflicts");
    request.data = buildFileConflictPromptData(lines, total);
    request.actions = {QStringLiteral("continue"), QStringLiteral("abort")};
    request.safeDefault = QVariantMap{{QStringLiteral("action"), QStringLiteral("abort")}};
    request.timeout = std::chrono::milliseconds(300000);

    if (!fileConflictAnswerContinue(m_ask(request))) {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_aborted.store(true);
        m_abortMessage = QStringLiteral("File conflicts were not accepted");
        return false;
    }
    return true;
}

/**
 * @brief 中断時のメッセージ。
 * @return メッセージ
 */
QString FileConflictReceiver::abortMessage() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_abortMessage;
}

// ─── CommitScriptReceiver ────────────────────────

/**
 * @brief 構築する。
 * @param aggregator 出力集約器
 */
CommitScriptReceiver::CommitScriptReceiver(ScriptOutputAggregator &aggregator)
    : m_aggregator(aggregator)
{
}

/**
 * @brief userData から rpm 出力行を取り出す。
 * @param userData 受信データ
 * @return 行
 */
QString CommitScriptReceiver::extractLine(
    const zypp::callback::UserData &userData, bool *isError) const
{
    if (isError)
        *isError = false;
    return lineFromUserData(userData);
}

/**
 * @brief 汎用受信 (rpm 出力行) を処理する。
 * @param userData 受信データ
 */
void CommitScriptReceiver::report(const zypp::callback::UserData &userData)
{
    if (userData.type() != zypp::target::rpm::CommitScriptReportSA::contentRpmout)
        return;

    bool isError = false;
    const QString line = extractLine(userData, &isError);
    if (line.isEmpty())
        return;

    std::lock_guard<std::mutex> lock(m_mutex);
    QString package = m_package;
    if (package.isEmpty()) {
        zypp::sat::Solvable solvable;
        if (userData.get<zypp::sat::Solvable>("solvable", solvable) && solvable)
            package = QString::fromStdString(solvable.name());
    }
    m_aggregator.addLine(package, m_scriptType, line, isError);
}

/**
 * @brief スクリプト開始を処理する。
 * @param scriptType スクリプト種別
 * @param packageName パッケージ名
 * @param resolvable 対象
 * @param userData 受信データ
 */
void CommitScriptReceiver::start(const std::string &scriptType,
                                 const std::string &packageName,
                                 zypp::Resolvable::constPtr resolvable,
                                 const zypp::callback::UserData &userData)
{
    (void)userData;
    std::lock_guard<std::mutex> lock(m_mutex);
    m_scriptType = QString::fromStdString(scriptType);
    if (!packageName.empty())
        m_package = QString::fromStdString(packageName);
    else if (resolvable)
        m_package = QString::fromStdString(resolvable->name());
    else
        m_package.clear();
}

/**
 * @brief スクリプト終了を処理する。
 * @param resolvable 対象
 * @param error 終了ステータス
 * @param userData 受信データ
 */
void CommitScriptReceiver::finish(zypp::Resolvable::constPtr resolvable, Error error,
                                  const zypp::callback::UserData &userData)
{
    (void)userData;
    std::lock_guard<std::mutex> lock(m_mutex);
    if (error != NO_ERROR) {
        QString package = m_package;
        if (package.isEmpty() && resolvable)
            package = QString::fromStdString(resolvable->name());
        m_aggregator.addLine(package, m_scriptType,
                             QStringLiteral("Script %1 finished with errors").arg(m_scriptType),
                             true);
    }
    m_scriptType.clear();
    m_package.clear();
}

// ─── RpmOutputReceiver ────────────────────────

/**
 * @brief 構築する。
 * @param aggregator 出力集約器
 */
RpmOutputReceiver::RpmOutputReceiver(ScriptOutputAggregator &aggregator)
    : m_aggregator(aggregator)
{
}

/**
 * @brief 汎用受信 (rpm 出力行) を処理する。
 * @param userData 受信データ
 */
void RpmOutputReceiver::report(const zypp::callback::UserData &userData)
{
    if (userData.type() != zypp::target::rpm::TransactionReportSA::contentRpmout)
        return;
    const QString line = lineFromUserData(userData);
    if (!line.isEmpty())
        m_aggregator.addRpmMessage(line, false);
}

// ─── RpmLoglineReceiver ────────────────────────

/**
 * @brief 構築する。
 * @param aggregator 出力集約器
 */
RpmLoglineReceiver::RpmLoglineReceiver(ScriptOutputAggregator &aggregator)
    : m_aggregator(aggregator)
{
}

/**
 * @brief 汎用受信 (ログ行) を処理する。
 * @param userData 受信データ
 */
void RpmLoglineReceiver::report(const zypp::callback::UserData &userData)
{
    if (userData.type() != zypp::target::rpm::SingleTransReport::contentLogline)
        return;
    const QString line = lineFromUserData(userData);
    if (line.isEmpty())
        return;

    bool isError = false;
    zypp::target::rpm::SingleTransReport::loglevel level =
        zypp::target::rpm::SingleTransReport::loglevel::msg;
    if (userData.get<zypp::target::rpm::SingleTransReport::loglevel>("level", level))
        isError = (level == zypp::target::rpm::SingleTransReport::loglevel::err
                   || level == zypp::target::rpm::SingleTransReport::loglevel::crt);
    m_aggregator.addRpmMessage(line, isError);
}

} // namespace qZypper
