#include <utility>
#include <zypp/sat/Solvable.h>
#include "CommitReceivers.h"

namespace qZypper {

namespace {

/**
 * @brief MediaChangeReportのErrorを短い英語メッセージへ変換する
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
 * @brief UserDataからrpm出力行を取り出す
 * @param userData 受信データ
 * @return 行 (取得できなければ空)
 */
QString lineFromUserData(const zypp::callback::UserData &userData)
{
    if (!userData.haskey("line"))
        return QString();

    try {
        const std::reference_wrapper<const std::string> ref = userData.get<std::reference_wrapper<const std::string>>("line");
        return QString::fromStdString(ref.get());
    }
    catch (const zypp::callback::UserData::bad_AnyType_cast &) {
        // 型が異なる場合はstd::stringとして再試行する
    }
    catch (...) {
        return QString();
    }

    try {
        return QString::fromStdString(userData.get<std::string>("line"));
    }
    catch (...) {
        return QString();
    }
}

} // anonymous namespace

// --- MediaChangeReceiver ---

/**
 * @brief 構築する
 * @param ask プロンプト問い合わせコールバック
 */
MediaChangeReceiver::MediaChangeReceiver(PromptAskFn ask) : m_ask(std::move(ask))
{
}

/**
 * @brief メディア交換要求を処理する
 * @param url 対象URL
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
    request.data = buildMediaChangePromptData(QString::fromStdString(url.asString()), QString::fromStdString(label), mediumNr,
                                              mediaErrorMessage(error), QString::fromStdString(description));
    request.actions = {QStringLiteral("retry"), QStringLiteral("skip"), QStringLiteral("abort")};
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

// --- FileConflictReceiver ---

/**
 * @brief 構築する
 * @param ask プロンプト問い合わせコールバック
 */
FileConflictReceiver::FileConflictReceiver(PromptAskFn ask) : m_ask(std::move(ask))
{
}

/**
 * @brief 競合検出結果を処理する
 * @param progress 進捗
 * @param noFilelist ファイルリスト未取得のsolvable
 * @param conflicts 検出された競合
 * @return 続行ならtrue、中断ならfalse
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
 * @brief 中断時のメッセージ
 * @return メッセージ
 */
QString FileConflictReceiver::abortMessage() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_abortMessage;
}

// --- CommitScriptReceiver ---

/**
 * @brief 構築する
 * @param aggregator 出力集約器
 */
CommitScriptReceiver::CommitScriptReceiver(ScriptOutputAggregator &aggregator) : m_aggregator(aggregator)
{
}

/**
 * @brief userDataからrpm出力行を取り出す
 * @param userData 受信データ
 * @return 行
 */
QString CommitScriptReceiver::extractLine(const zypp::callback::UserData &userData, bool *isError) const
{
    if (isError)
        *isError = false;

    return lineFromUserData(userData);
}

/**
 * @brief 汎用受信 (rpm出力行) を処理する
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
 * @brief スクリプト開始を処理する
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
 * @brief スクリプト終了を処理する
 * @param resolvable 対象
 * @param error 終了ステータス
 * @param userData 受信データ
 */
void CommitScriptReceiver::finish(zypp::Resolvable::constPtr resolvable, Error error, const zypp::callback::UserData &userData)
{
    (void)userData;
    std::lock_guard<std::mutex> lock(m_mutex);

    if (error != NO_ERROR) {
        QString package = m_package;
        if (package.isEmpty() && resolvable)
            package = QString::fromStdString(resolvable->name());

        m_aggregator.addLine(package, m_scriptType, QStringLiteral("Script %1 finished with errors").arg(m_scriptType), true);
    }

    m_scriptType.clear();
    m_package.clear();
}

// --- RpmOutputReceiver ---

/**
 * @brief 構築する
 * @param aggregator 出力集約器
 */
RpmOutputReceiver::RpmOutputReceiver(ScriptOutputAggregator &aggregator) : m_aggregator(aggregator)
{
}

/**
 * @brief 汎用受信 (rpm出力行) を処理する
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

// --- RpmLoglineReceiver ---

/**
 * @brief 構築する
 * @param aggregator 出力集約器
 */
RpmLoglineReceiver::RpmLoglineReceiver(ScriptOutputAggregator &aggregator) : m_aggregator(aggregator)
{
}

/**
 * @brief 汎用受信 (ログ行) を処理する
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
    zypp::target::rpm::SingleTransReport::loglevel level = zypp::target::rpm::SingleTransReport::loglevel::msg;

    if (userData.get<zypp::target::rpm::SingleTransReport::loglevel>("level", level))
        isError = (level == zypp::target::rpm::SingleTransReport::loglevel::err || level == zypp::target::rpm::SingleTransReport::loglevel::crt);

    m_aggregator.addRpmMessage(line, isError);
}

} // namespace qZypper
