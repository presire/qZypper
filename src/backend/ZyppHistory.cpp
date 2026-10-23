#include <algorithm>
#include <map>
#include <memory>
#include <numeric>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>
#include <zypp/Date.h>
#include <zypp/Url.h>
#include <zypp/PathInfo.h>
#include <zypp/HistoryLogData.h>
#include <zypp/parser/HistoryLogReader.h>
#include "ZyppHistory.h"
#include "ZyppManager.h"

namespace qZypper {

namespace {

/**
 * @brief 履歴フィルタで許可する操作種別の集合を返す
 * @return 許可される操作種別名 (小文字)
 */
const std::set<std::string> &allowedActions()
{
    static const std::set<std::string> actions = {
        "install", "remove", "radd", "rremove",
        "command", "patch", "rchange", "alias", "url"
    };

    return actions;
}

/**
 * @brief 操作種別名をzyppのHistoryActionIDに変換する
 * @param action 正規化済み操作種別名
 * @return 対応するHistoryActionID
 */
zypp::HistoryActionID actionIdFor(const QString &action)
{
    if (action == QLatin1String("install")) {
        return zypp::HistoryActionID::INSTALL;
    }

    if (action == QLatin1String("remove")) {
        return zypp::HistoryActionID::REMOVE;
    }

    if (action == QLatin1String("radd")) {
        return zypp::HistoryActionID::REPO_ADD;
    }

    if (action == QLatin1String("rremove")) {
        return zypp::HistoryActionID::REPO_REMOVE;
    }

    if (action == QLatin1String("rchange") || action == QLatin1String("alias")) {
        return zypp::HistoryActionID::REPO_CHANGE_ALIAS;
    }

    if (action == QLatin1String("url")) {
        return zypp::HistoryActionID::REPO_CHANGE_URL;
    }

    if (action == QLatin1String("patch")) {
        return zypp::HistoryActionID::PATCH_STATE_CHANGE;
    }

    return zypp::HistoryActionID::STAMP_COMMAND;
}

/**
 * @brief zyppのHistoryActionIDを表示用の操作種別名に変換する
 * @param id 履歴操作種別
 * @return 正規化した操作種別名 (未知は空文字)
 */
QString actionNameFor(zypp::HistoryActionID id)
{
    switch (id.toEnum()) {
        case zypp::HistoryActionID::INSTALL_e:
            return QStringLiteral("install");
        case zypp::HistoryActionID::REMOVE_e:
            return QStringLiteral("remove");
        case zypp::HistoryActionID::REPO_ADD_e:
            return QStringLiteral("radd");
        case zypp::HistoryActionID::REPO_REMOVE_e:
            return QStringLiteral("rremove");
        case zypp::HistoryActionID::REPO_CHANGE_ALIAS_e:
            return QStringLiteral("rchange");
        case zypp::HistoryActionID::REPO_CHANGE_URL_e:
            return QStringLiteral("url");
        case zypp::HistoryActionID::STAMP_COMMAND_e:
            return QStringLiteral("command");
        case zypp::HistoryActionID::PATCH_STATE_CHANGE_e:
            return QStringLiteral("patch");
        case zypp::HistoryActionID::NONE_e:
            break;
    }

    return QString();
}

/**
 * @brief 履歴に記録されたURLからパスワードを除いた表示用文字列を返す
 * @param url 履歴のURL
 * @return パスワードを含まないURL文字列
 */
QString maskedUrl(const zypp::Url &url)
{
    // パスワードを表示対象から外し、履歴の閲覧で資格情報が漏れないようにする
    return QString::fromStdString(url.asString(url.getViewOptions() - zypp::url::ViewOptions::WITH_PASSWORD));
}

/**
 * @brief HistoryLogDataから表示用のHistoryEntryを構築する
 * @param data 履歴1行分のデータ
 * @return 構築したエントリ
 */
HistoryEntry buildEntry(const zypp::HistoryLogData::Ptr &data)
{
    HistoryEntry entry;
    entry.timestamp = static_cast<qint64>(static_cast<time_t>(data->date()));
    entry.action = actionNameFor(data->action());

    using namespace zypp;
    if (auto p = zypp::dynamic_pointer_cast<HistoryLogDataInstall>(data)) {
        entry.name = QString::fromStdString(p->name());
        entry.edition = QString::fromStdString(p->edition().asString());
        entry.arch = QString::fromStdString(p->arch().asString());
        entry.repoAlias = QString::fromStdString(p->repoAlias());
        entry.requestedBy = QString::fromStdString(p->reqby());
        entry.userdata = QString::fromStdString(p->userdata());
    }
    else if (auto p = zypp::dynamic_pointer_cast<HistoryLogDataRemove>(data)) {
        entry.name = QString::fromStdString(p->name());
        entry.edition = QString::fromStdString(p->edition().asString());
        entry.arch = QString::fromStdString(p->arch().asString());
        entry.requestedBy = QString::fromStdString(p->reqby());
        entry.userdata = QString::fromStdString(p->userdata());
    }
    else if (auto p = zypp::dynamic_pointer_cast<HistoryLogDataRepoAdd>(data)) {
        entry.name = QString::fromStdString(p->alias());
        entry.repoAlias = entry.name;
        entry.url = maskedUrl(p->url());
        entry.userdata = QString::fromStdString(p->userdata());
    }
    else if (auto p = zypp::dynamic_pointer_cast<HistoryLogDataRepoRemove>(data)) {
        entry.name = QString::fromStdString(p->alias());
        entry.repoAlias = entry.name;
        entry.userdata = QString::fromStdString(p->userdata());
    }
    else if (auto p = zypp::dynamic_pointer_cast<HistoryLogDataRepoAliasChange>(data)) {
        entry.name = QString::fromStdString(p->newAlias());
        entry.repoAlias = entry.name;
        entry.oldAlias = QString::fromStdString(p->oldAlias());
        entry.userdata = QString::fromStdString(p->userdata());
    }
    else if (auto p = zypp::dynamic_pointer_cast<HistoryLogDataRepoUrlChange>(data)) {
        entry.name = QString::fromStdString(p->alias());
        entry.repoAlias = entry.name;
        entry.url = maskedUrl(p->newUrl());
        entry.userdata = QString::fromStdString(p->userdata());
    }
    else if (auto p = zypp::dynamic_pointer_cast<HistoryLogDataStampCommand>(data)) {
        entry.requestedBy = QString::fromStdString(p->executedBy());
        entry.command = QString::fromStdString(p->command());
        entry.userdata = QString::fromStdString(p->userdata());
    }
    else if (auto p = zypp::dynamic_pointer_cast<HistoryLogPatchStateChange>(data)) {
        entry.name = QString::fromStdString(p->name());
        entry.edition = QString::fromStdString(p->edition().asString());
        entry.arch = QString::fromStdString(p->arch().asString());
        entry.repoAlias = QString::fromStdString(p->repoAlias());
    }

    return entry;
}

} // namespace

/**
 * @brief QVariantMapへ変換する
 * @return D-Bus転送用マップ
 */
QVariantMap HistoryEntry::toVariantMap() const
{
    QVariantMap map;
    map.insert(QStringLiteral("timestamp"), QVariant::fromValue(timestamp));
    map.insert(QStringLiteral("action"), action);
    map.insert(QStringLiteral("name"), name);
    map.insert(QStringLiteral("edition"), edition);
    map.insert(QStringLiteral("arch"), arch);
    map.insert(QStringLiteral("repoAlias"), repoAlias);

    if (!requestedBy.isEmpty()) {
        map.insert(QStringLiteral("requestedBy"), requestedBy);
    }

    if (!userdata.isEmpty()) {
        map.insert(QStringLiteral("userdata"), userdata);
    }

    if (!command.isEmpty()) {
        map.insert(QStringLiteral("command"), command);
    }

    // リポジトリ操作の補足項目は、値があるときだけマップへ含める
    if (!url.isEmpty()) {
        map.insert(QStringLiteral("url"), url);
    }

    if (!oldUrl.isEmpty()) {
        map.insert(QStringLiteral("oldUrl"), oldUrl);
    }

    if (!oldAlias.isEmpty()) {
        map.insert(QStringLiteral("oldAlias"), oldAlias);
    }

    return map;
}

/**
 * @brief コンストラクタ
 * @param offset 最終順序の先頭から読み飛ばす件数
 * @param limit 読み飛ばし後に返却する最大件数
 */
BoundedHistoryCollector::BoundedHistoryCollector(quint32 offset, quint32 limit)
    : m_capacity(static_cast<std::size_t>(offset) + static_cast<std::size_t>(limit))
    , m_offset(offset)
{
    m_kept.reserve(m_capacity);
}

/**
 * @brief 最終順序でaがbより前 (新しい) ならtrue
 * @param a 比較対象
 * @param b 比較対象
 * @return aが先に来る場合true
 */
bool BoundedHistoryCollector::newerThan(const Item &a, const Item &b)
{
    if (a.entry.timestamp != b.entry.timestamp) {
        return a.entry.timestamp > b.entry.timestamp;
    }

    return a.seq < b.seq;
}

/**
 * @brief エントリを1件追加する
 * @param entry 追加するエントリ (呼び出し順がファイル出現順)
 */
void BoundedHistoryCollector::add(const HistoryEntry &entry)
{
    if (m_capacity == 0) {
        return;
    }

    Item item;
    item.entry = entry;
    item.seq = m_seq++;

    if (m_kept.size() < m_capacity) {
        m_kept.push_back(item);
        std::push_heap(m_kept.begin(), m_kept.end(), HeapCompare());
        return;
    }

    // ヒープ先頭は最終順序で最下位 (最初に捨てる候補)
    // より新しければ差し替える
    if (newerThan(item, m_kept.front())) {
        std::pop_heap(m_kept.begin(), m_kept.end(), HeapCompare());
        m_kept.back() = item;
        std::push_heap(m_kept.begin(), m_kept.end(), HeapCompare());
    }
}

/**
 * @brief 最終順序でoffset件読み飛ばした結果を返す
 * @return 返却対象のエントリ一覧 (最大limit件)
 */
std::vector<HistoryEntry> BoundedHistoryCollector::take() const
{
    std::vector<Item> sorted = m_kept;
    std::sort(sorted.begin(), sorted.end(), newerThan);

    std::vector<HistoryEntry> result;
    if (m_offset >= sorted.size()) {
        return result;
    }

    result.reserve(sorted.size() - m_offset);

    for (std::size_t i = m_offset; i < sorted.size(); ++i) {
        result.push_back(sorted[i].entry);
    }

    return result;
}

/**
 * @brief 履歴オプションマップを検証し正規化する
 * @param map D-Busから受け取ったオプションマップ
 * @param out 正規化したオプション (成功時のみ有効)
 * @param error 失敗時のエラーメッセージ (英語)
 * @return 有効な場合はtrue
 */
bool parseHistoryOptions(const QVariantMap &map, HistoryOptions &out, std::string &error)
{
    HistoryOptions options;

    if (map.contains(QStringLiteral("since"))) {
        bool ok = false;
        const qlonglong value = map.value(QStringLiteral("since")).toLongLong(&ok);

        if (!ok || value < 0) {
            error = "invalid 'since'";
            return false;
        }

        // D-Bus契約はエポックミリ秒、HistoryOptionsはエポック秒 (単位変換はここだけ)
        options.since = epochMillisecondsToSeconds(value);
    }

    if (map.contains(QStringLiteral("until"))) {
        bool ok = false;
        const qlonglong value = map.value(QStringLiteral("until")).toLongLong(&ok);

        if (!ok || value < 0) {
            error = "invalid 'until'";
            return false;
        }

        options.until = epochMillisecondsToSeconds(value);
    }

    if (options.since > 0 && options.until > 0 && options.since > options.until) {
        error = "'since' must be <= 'until'";
        return false;
    }

    // limitとoffsetは符号付き64bitで範囲を確かめてからquint32へ縮める (巨大値の切り捨てによる誤受理を防ぐ)
    if (map.contains(QStringLiteral("limit"))) {
        bool ok = false;
        const qlonglong value = map.value(QStringLiteral("limit")).toLongLong(&ok);

        if (!ok || value <= 0 || value > static_cast<qlonglong>(kMaxHistoryLimit)) {
            error = "invalid 'limit'";
            return false;
        }

        options.limit = static_cast<quint32>(value);
    }

    if (map.contains(QStringLiteral("offset"))) {
        bool ok = false;
        const qlonglong value = map.value(QStringLiteral("offset")).toLongLong(&ok);

        if (!ok || value < 0 || value > static_cast<qlonglong>(kMaxHistoryWindow)) {
            error = "invalid 'offset'";
            return false;
        }

        options.offset = static_cast<quint32>(value);
    }

    // 保持件数offset + limitを上限内に制限する (メモリ使用量の安全弁)
    if (options.offset > kMaxHistoryWindow || options.limit > kMaxHistoryWindow - options.offset) {
        error = "invalid 'offset'";
        return false;
    }

    const QVariant actionsVar = map.value(QStringLiteral("actions"));
    if (!actionsVar.isNull()) {
        QStringList actions;

        if (actionsVar.typeId() == QMetaType::QStringList) {
            actions = actionsVar.toStringList();
        }
        else {
            const QVariantList raw = actionsVar.toList();
            actions.reserve(raw.size());

            for (const QVariant &item : raw) {
                actions.append(item.toString());
            }
        }

        for (const QString &item : actions) {
            const QString norm = item.trimmed().toLower();

            if (norm.isEmpty() || allowedActions().count(norm.toStdString()) == 0) {
                error = "invalid action filter: " + norm.toStdString();
                return false;
            }

            options.actions.append(norm);
        }
    }

    out = options;
    error.clear();
    return true;
}

/**
 * @brief 履歴ファイルを資源上限付きで読み込む
 * @param path 履歴ファイルのパス
 * @param options 検証済みの履歴オプション
 * @param cancel キャンセルフラグ
 * @param heartbeat 走査の進捗通知 (一定件数ごとに呼ぶ)
 * @param out 結果 (成功時のみ有効)
 * @param error 失敗時のエラーメッセージ (英語)
 * @param limits 資源上限
 * @return 成功した場合はtrue
 */
bool readHistoryFile(const std::string &path, const HistoryOptions &options,
                     const std::atomic<bool> &cancel, const std::function<void()> &heartbeat,
                     std::vector<HistoryEntry> &out, std::string &error,
                     const HistoryReadLimits &limits)
{
    const zypp::PathInfo info{zypp::Pathname(path)};

    if (!info.isExist()) {
        out.clear();
        return true;
    }

    if (!info.isFile()) {
        error = "history file is not a regular file";
        return false;
    }

    // 読み込み量はフィルタ後の件数ではなくファイル全体で決まるため、
    // 先にファイルサイズで上限を掛ける (フィルタ不一致や不正行も対象)
    if (static_cast<quint64>(info.size()) > limits.maxBytes) {
        error = "history file is too large (" + std::to_string(info.size()) + " bytes, limit " + std::to_string(limits.maxBytes) + ")";
        return false;
    }

    // ファイル走査中にwatchdogへ進捗を伝えるheartbeat間隔 (エントリ数)
    constexpr quint64 kHeartbeatInterval = 1000;

    BoundedHistoryCollector collector(options.offset, options.limit);
    bool cancelled = false;
    bool tooMany = false;
    quint64 scanned = 0;

    try {
        zypp::parser::HistoryLogReader reader(
            info.path(),
            zypp::parser::HistoryLogReader::Options(),
            [&](const zypp::HistoryLogData::Ptr &data) -> bool {
                // HistoryLogReaderはコールバックのfalseで読み込みを打ち切る
                if (cancel.load()) {
                    cancelled = true;
                    return false;
                }

                // 上限で打ち切った結果は最新側を欠くため、部分結果は返さない
                if (scanned >= limits.maxEntries) {
                    tooMany = true;
                    return false;
                }

                collector.add(buildEntry(data));

                if (++scanned % kHeartbeatInterval == 0 && heartbeat)
                    heartbeat();

                return true;
            });

        reader.setIgnoreInvalidItems(true);

        for (const QString &action : options.actions) {
            reader.addActionFilter(actionIdFor(action));
        }

        if (options.since > 0 || options.until > 0) {
            const zypp::Date from(options.since > 0 ? options.since : 0);
            const zypp::Date to(options.until > 0 ? options.until : static_cast<qint64>(::time(nullptr)));
            reader.readFromTo(from, to);
        }
        else {
            reader.readAll();
        }
    }
    catch (const std::exception &e) {
        // 履歴ファイルの解析に失敗したときは、例外の内容をエラーメッセージとして返す
        error = e.what();
        return false;
    }

    // 取消しで止まったときは、途中までの結果を使わせないよう失敗にする
    if (cancelled) {
        error = "Operation cancelled";
        return false;
    }

    // 上限超過で止まったときも、部分結果を返さず失敗にする
    if (tooMany) {
        error = "history has too many entries (limit " + std::to_string(limits.maxEntries) + ")";
        return false;
    }

    out = collector.take();
    return true;
}

/**
 * @brief zyppヒストリを取得する
 * @param options 検証済みの履歴オプション
 * @param entries 結果のエントリ一覧 (成功時のみ有効)
 * @param error 失敗時のエラーメッセージ (英語)
 * @return 成功した場合はtrue
 */
bool ZyppManager::getHistory(const HistoryOptions &options, std::vector<HistoryEntry> &entries, std::string &error)
{
    return readHistoryFile("/var/log/zypp/history", options, m_cancelRequested, [this] { m_operation.heartbeat(); }, entries, error);
}

/**
 * @brief QVariantMapへ変換する
 * @return D-Bus転送用マップ (eventsはa{sv}のリスト)
 */
QVariantMap HistoryGroup::toVariantMap() const
{
    QVariantList eventList;
    eventList.reserve(static_cast<int>(events.size()));

    // 各イベントをa{sv}へ変換して並べる
    for (const HistoryEntry &event : events) {
        eventList.append(event.toVariantMap());
    }

    QVariantMap map;

    // 同時刻のグループを区別できるよう、時刻とファイル内の出現番号を組み合わせた識別子にする
    map.insert(QStringLiteral("id"), QStringLiteral("%1:%2").arg(timestamp).arg(seq));
    map.insert(QStringLiteral("seq"), QVariant::fromValue(static_cast<qulonglong>(seq)));
    map.insert(QStringLiteral("timestamp"), QVariant::fromValue(static_cast<qlonglong>(timestamp)));
    map.insert(QStringLiteral("command"), command);
    map.insert(QStringLiteral("requestedBy"), requestedBy);
    map.insert(QStringLiteral("userdata"), userdata);
    map.insert(QStringLiteral("artificial"), artificial);
    map.insert(QStringLiteral("eventCount"), QVariant::fromValue(eventCount));
    map.insert(QStringLiteral("installCount"), QVariant::fromValue(installCount));
    map.insert(QStringLiteral("removeCount"), QVariant::fromValue(removeCount));
    map.insert(QStringLiteral("hiddenCount"), QVariant::fromValue(hiddenCount()));
    map.insert(QStringLiteral("truncated"), hiddenCount() > 0);
    map.insert(QStringLiteral("events"), eventList);
    return map;
}

/**
 * @brief QVariantMapへ変換する
 * @return D-Bus転送用マップ (groupsはa{sv}のリスト)
 */
QVariantMap HistoryGroupResult::toVariantMap() const
{
    QVariantList groupList;
    groupList.reserve(static_cast<int>(groups.size()));

    // 各グループをa{sv}へ変換して新しい順のまま並べる
    for (const HistoryGroup &group : groups) {
        groupList.append(group.toVariantMap());
    }

    QVariantMap map;
    map.insert(QStringLiteral("groups"), groupList);
    map.insert(QStringLiteral("totalGroups"), QVariant::fromValue(static_cast<qulonglong>(totalGroups)));
    map.insert(QStringLiteral("offset"), QVariant::fromValue(offset));
    map.insert(QStringLiteral("limit"), QVariant::fromValue(limit));
    map.insert(QStringLiteral("hiddenEvents"), QVariant::fromValue(static_cast<qulonglong>(hiddenEvents)));
    map.insert(QStringLiteral("truncated"), truncated);
    map.insert(QStringLiteral("hasMore"), hasMore);
    return map;
}

/**
 * @brief 履歴グループのオプションマップを検証し正規化する
 * @param map D-Busから受け取ったオプションマップ
 * @param out 正規化したオプション (成功時のみ有効)
 * @param error 失敗時のエラーメッセージ (英語)
 * @return 有効な場合はtrue
 */
bool parseHistoryGroupOptions(const QVariantMap &map, HistoryGroupOptions &out, std::string &error)
{
    HistoryGroupOptions options;

    // since、until、actionsはGetHistoryと同じ規則で検証する (limit、offset、textはグループ単位のため別に検証する)
    QVariantMap base = map;
    base.remove(QStringLiteral("limit"));
    base.remove(QStringLiteral("offset"));
    base.remove(QStringLiteral("text"));

    // 検証に失敗した場合はparseHistoryOptionsが設定したエラーをそのまま返す
    if (!parseHistoryOptions(base, options.filter, error)) {
        return false;
    }

    // グループ件数はグループ単位の上限で検証する (符号付き64bitで範囲を確かめてからquint32へ縮める)
    if (map.contains(QStringLiteral("limit"))) {
        bool ok = false;
        const qlonglong value = map.value(QStringLiteral("limit")).toLongLong(&ok);

        if (!ok || value <= 0 || value > static_cast<qlonglong>(kMaxHistoryGroupLimit)) {
            error = "invalid 'limit'";
            return false;
        }

        options.limit = static_cast<quint32>(value);
    }

    if (map.contains(QStringLiteral("offset"))) {
        bool ok = false;
        const qlonglong value = map.value(QStringLiteral("offset")).toLongLong(&ok);

        if (!ok || value < 0 || value > static_cast<qlonglong>(kMaxHistoryWindow)) {
            error = "invalid 'offset'";
            return false;
        }

        options.offset = static_cast<quint32>(value);
    }

    if (map.contains(QStringLiteral("text"))) {
        const QVariant textVar = map.value(QStringLiteral("text"));

        // 文字列以外 (数値など) の検索条件は誤指定のため拒否する
        if (textVar.typeId() != QMetaType::QString) {
            error = "invalid 'text'";
            return false;
        }

        const QString text = textVar.toString().trimmed();

        // 検索文字列が過大なときは、部分一致の負荷を抑えるため拒否する
        if (text.toUtf8().size() > kMaxHistoryTextBytes) {
            error = "invalid 'text'";
            return false;
        }

        options.text = text;
    }

    // 全項目の検証に成功したときだけ出力へ反映する
    out = options;
    error.clear();
    return true;
}

namespace {

/**
 * @brief グループ取得で配下イベントへ適用する絞り込み条件
 */
class EventFilter {
public:
    /**
     * @brief コンストラクタ
     * @param options 検証済みのグループ取得オプション
     */
    explicit EventFilter(const HistoryGroupOptions &options)
        : m_since(options.filter.since)
        , m_until(options.filter.until)
        , m_hasActions(!options.filter.actions.isEmpty())
        , m_text(options.text)
    {
        for (const QString &action : options.filter.actions) {
            // エイリアス変更は"alias"でも"rchange"でも指定できる (commandは境界のため配下イベントには一致しない)
            m_actions.insert(action == QLatin1String("alias") ? QStringLiteral("rchange") : action);
        }
    }

    /**
     * @brief 配下イベントが絞り込み条件を満たすか判定する
     * @param entry 判定するイベント
     * @return 満たす場合はtrue
     */
    bool matches(const HistoryEntry &entry) const
    {
        // 期間の開始より前のイベントは対象外
        if (m_since > 0 && entry.timestamp < m_since) {
            return false;
        }

        // 期間の終了より後のイベントは対象外
        if (m_until > 0 && entry.timestamp > m_until) {
            return false;
        }

        // 操作種別が指定されているときは、一覧に含まれない種別を対象外にする
        if (m_hasActions && m_actions.count(entry.action) == 0) {
            return false;
        }

        // 検索文字列が無いときは、期間と種別の条件を満たした時点で一致とする
        if (m_text.isEmpty()) {
            return true;
        }

        // 名前、版、アーキテクチャ、リポジトリ関連の項目のいずれかに検索文字列を含めば一致とする
        for (const QString *field : {&entry.name, &entry.edition, &entry.arch, &entry.repoAlias,
                                      &entry.url, &entry.oldUrl, &entry.oldAlias}) {
            if (field->contains(m_text, Qt::CaseInsensitive)) {
                return true;
            }
        }

        return false;
    }

private:
    qint64 m_since;               // エポック秒の下限 (0は制限なし)
    qint64 m_until;               // エポック秒の上限 (0は制限なし)
    bool m_hasActions;            // 操作種別の絞り込みがあるか
    std::set<QString> m_actions;  // 一致する操作種別
    QString m_text;               // 部分一致の検索文字列
};

/**
 * @brief ファイル順のエントリ列をcommand単位のグループ境界へ分ける
 *
 * 最初のcommandより前のイベントのために仮想グループを作る
 * 操作種別が不明な行は境界でもイベントでもない
 */
class GroupSplitter {
public:
    /**
     * @brief feed()の判定結果
     */
    struct Step {
        bool newGroup = false;  // このエントリで新しいグループが始まった
        bool child = false;     // このエントリが現在のグループの配下イベントである
    };

    /**
     * @brief エントリを1件処理する
     * @param entry ファイル順のエントリ
     * @return 判定結果
     * @note 現在のグループ見出しを更新する (commandなら新規、先頭のcommand前なら仮想グループ)
     */
    Step feed(const HistoryEntry &entry)
    {
        Step step;

        // 操作種別が不明な行は、境界でもイベントでもないため何もせず返す
        if (entry.action.isEmpty()) {
            return step;
        }

        // command行は新しいグループの開始を表す
        if (entry.action == QLatin1String("command")) {
            m_head = HistoryGroup();
            m_head.seq = m_next++;
            m_head.timestamp = entry.timestamp;
            m_head.command = entry.command;
            m_head.requestedBy = entry.requestedBy;
            m_head.userdata = entry.userdata;
            m_open = true;
            step.newGroup = true;
            return step;
        }

        // 最初のcommandより前のイベントは、仮想グループへまとめる
        if (!m_open) {
            m_head = HistoryGroup();
            m_head.seq = m_next++;
            m_head.timestamp = entry.timestamp;
            m_head.command = QStringLiteral("<unknown>");
            m_head.artificial = true;
            m_open = true;
            step.newGroup = true;
        }

        step.child = true;
        return step;
    }

    /**
     * @brief 現在のグループ見出しを返す
     * @return 見出し (イベントは空)
     */
    const HistoryGroup &head() const
    {
        return m_head;
    }

private:
    HistoryGroup m_head;  // 現在のグループ見出し
    quint64 m_next = 0;   // 次のグループ出現番号
    bool m_open = false;  // グループが開始済みか
};

/**
 * @brief 1回目の走査で集める、イベントを持たないグループの要約
 */
struct GroupSummary {
    quint64 seq = 0;           // グループ出現番号
    qint64 timestamp = 0;      // グループの時刻
    QString command;           // 実行コマンド
    QString requestedBy;       // 実行主体
    QString userdata;          // 補足データ
    bool artificial = false;   // 仮想グループか
    quint32 eventCount = 0;    // 絞り込み後の配下イベント数
    quint32 installCount = 0;  // 絞り込み後のinstall数
    quint32 removeCount = 0;   // 絞り込み後のremove数
};

/**
 * @brief 履歴ファイルを絞り込みなしでファイル順に走査し、各エントリを訪問する
 *
 * リポジトリURLを追跡し、URL変更行へ変更前URLを補う
 * ファイルサイズと走査数の上限、取消しはreadHistoryFileと同じ規則で扱う
 *
 * @param path 履歴ファイルのパス
 * @param cancel キャンセルフラグ
 * @param heartbeat 進捗通知
 * @param maxBytes ファイルサイズ上限
 * @param maxEntries 走査エントリ数の上限
 * @param stopAfter 0以外のとき、この件数を超えるエントリは読まない (2回目の走査で、1回目より後の追記を無視する)
 * @param scanned 訪問したエントリ数の出力先
 * @param visit エントリごとの処理 (falseを返すと走査を打ち切る、エラー時は呼び出し側がerrorを設定する)
 * @param error 失敗時のエラーメッセージ (英語)
 * @return 成功した場合はtrue (visitによる打ち切りも成功として扱う)
 * @note ファイルが存在しないときは履歴なしとして成功を返す
 *       libzyppの解析例外は捕捉し、エラーメッセージとして返す
 */
bool scanHistoryEntries(const std::string &path, const std::atomic<bool> &cancel,
                        const std::function<void()> &heartbeat, quint64 maxBytes, quint64 maxEntries,
                        quint64 stopAfter, quint64 &scanned,
                        const std::function<bool(const HistoryEntry &)> &visit, std::string &error)
{
    scanned = 0;
    const zypp::PathInfo info{zypp::Pathname(path)};

    // 履歴ファイルがまだ無い環境は、エラーではなく履歴なしとして扱う
    if (!info.isExist()) {
        return true;
    }

    // 通常ファイル以外 (ディレクトリやデバイス等) は読み込まない
    if (!info.isFile()) {
        error = "history file is not a regular file";
        return false;
    }

    // 巨大なファイルによるメモリと時間の消費を防ぐため、読み込み前にサイズを検査する
    if (static_cast<quint64>(info.size()) > maxBytes) {
        error = "history file is too large (" + std::to_string(info.size()) + " bytes, limit " + std::to_string(maxBytes) + ")";
        return false;
    }

    constexpr quint64 kHeartbeatInterval = 1000;  // 進捗通知を行うエントリ間隔
    bool cancelled = false;                       // 取消しで走査を止めたか
    bool tooMany = false;                         // 走査数の上限で走査を止めたか
    bool failed = false;                          // visitが走査の打ち切りを要求したか

    // エイリアスから直近の既知URLへの対応 (URL変更行へ変更前URLを補うために使う)
    std::map<QString, QString> repoUrls;

    try {
        zypp::parser::HistoryLogReader reader(
            info.path(),
            zypp::parser::HistoryLogReader::Options(),
            [&](const zypp::HistoryLogData::Ptr &data) -> bool {
                // 取消し要求があれば読み込みを止める (結果は破棄してエラーを返す)
                if (cancel.load()) {
                    cancelled = true;
                    return false;
                }

                // 1回目の走査より後に追記された行は読まない (2回目の走査で1回目と件数を揃える)
                if (stopAfter != 0 && scanned >= stopAfter) {
                    return false;
                }

                // 走査数の上限を超えたときは、部分結果を返さずエラーにする
                if (scanned >= maxEntries) {
                    tooMany = true;
                    return false;
                }

                HistoryEntry entry = buildEntry(data);

                // リポジトリURLを追跡し、URL変更行へ変更前URLを補う
                if (entry.action == QLatin1String("radd")) {
                    repoUrls[entry.repoAlias] = entry.url;
                }
                else if (entry.action == QLatin1String("rremove")) {
                    repoUrls.erase(entry.repoAlias);
                }
                else if (entry.action == QLatin1String("url")) {
                    entry.oldUrl = repoUrls[entry.repoAlias];
                    repoUrls[entry.repoAlias] = entry.url;
                }
                else if (entry.action == QLatin1String("rchange") && !entry.oldAlias.isEmpty()) {
                    // エイリアスが変わったリポジトリのURLを、新しいエイリアスへ引き継ぐ
                    const auto found = repoUrls.find(entry.oldAlias);

                    if (found != repoUrls.end()) {
                        const QString known = found->second;
                        repoUrls.erase(found);
                        repoUrls[entry.repoAlias] = known;
                    }
                }

                ++scanned;

                // 長い履歴でも処理中であることを通知し、アイドルタイムアウトによる終了を防ぐ
                if (scanned % kHeartbeatInterval == 0 && heartbeat) {
                    heartbeat();
                }

                // 呼び出し側が走査の打ち切りを要求したときは、読み込みを止める
                if (!visit(entry)) {
                    failed = true;
                    return false;
                }

                return true;
            });

        reader.setIgnoreInvalidItems(true);
        reader.readAll();
    }
    catch (const std::exception &e) {
        error = e.what();
        return false;
    }

    if (cancelled) {
        error = "Operation cancelled";
        return false;
    }

    if (tooMany) {
        error = "history has too many entries (limit " + std::to_string(maxEntries) + ")";
        return false;
    }

    // visitが打ち切った場合、失敗理由を呼び出し側が設定済みならerrorに入っている
    return !(failed && !error.empty());
}

} // namespace

/**
 * @brief 履歴ファイルをcommand単位のグループにして読み込む
 * @param path 履歴ファイルのパス
 * @param options 検証済みのオプション
 * @param cancel キャンセルフラグ
 * @param heartbeat 走査の進捗通知
 * @param out 結果 (成功時のみ有効)
 * @param error 失敗時のエラーメッセージ (英語)
 * @param limits 資源上限
 * @return 成功した場合はtrue
 */
bool readHistoryGroups(const std::string &path, const HistoryGroupOptions &options,
                       const std::atomic<bool> &cancel, const std::function<void()> &heartbeat,
                       HistoryGroupResult &out, std::string &error, const HistoryGroupLimits &limits)
{
    error.clear();
    const EventFilter filter(options);
    std::vector<GroupSummary> summaries;  // 絞り込み後に配下イベントが残ったグループの要約 (ファイル順)
    quint64 scannedFirst = 0;             // 1回目の走査で読んだエントリ数

    // 1回目: グループ要約だけを集める (イベント本体は保持しない)
    {
        GroupSplitter splitter;
        GroupSummary current;
        bool haveCurrent = false;

        // 配下イベントが残ったグループだけを要約へ確定する (空グループはここで捨てる)
        const auto flush = [&] {
            if (haveCurrent && current.eventCount > 0) {
                summaries.push_back(current);
            }

            haveCurrent = false;
        };

        // 全エントリを絞り込みなしで読み、グループごとの件数を数える
        const bool ok = scanHistoryEntries(path, cancel, heartbeat, limits.maxBytes, limits.maxEntries, 0, scannedFirst,
            [&](const HistoryEntry &entry) {
                const GroupSplitter::Step step = splitter.feed(entry);

                // 新しいグループが始まったら、直前のグループを確定して見出しを引き継ぐ
                if (step.newGroup) {
                    flush();
                    const HistoryGroup &head = splitter.head();
                    current = GroupSummary();
                    current.seq = head.seq;
                    current.timestamp = head.timestamp;
                    current.command = head.command;
                    current.requestedBy = head.requestedBy;
                    current.userdata = head.userdata;
                    current.artificial = head.artificial;
                    haveCurrent = true;
                }

                // 絞り込みを満たす配下イベントだけを数える (commandは境界のため数えない)
                if (step.child && filter.matches(entry)) {
                    ++current.eventCount;

                    if (entry.action == QLatin1String("install")) {
                        ++current.installCount;
                    }
                    else if (entry.action == QLatin1String("remove")) {
                        ++current.removeCount;
                    }
                }

                return true;
            }, error);

        // 走査の失敗 (取消し、上限超過、解析失敗) は理由をerrorに入れて失敗にする
        if (!ok) {
            return false;
        }

        flush();
    }

    // 新しい順 (時刻降順、同時刻はファイルで後ろのグループが先)
    std::vector<std::size_t> order(summaries.size());
    std::iota(order.begin(), order.end(), std::size_t(0));
    std::sort(order.begin(), order.end(), [&summaries](std::size_t a, std::size_t b) {
        if (summaries[a].timestamp != summaries[b].timestamp) {
            return summaries[a].timestamp > summaries[b].timestamp;
        }

        return summaries[a].seq > summaries[b].seq;
    });

    HistoryGroupResult result;
    result.totalGroups = summaries.size();
    result.offset = options.offset;
    result.limit = options.limit;

    // offsetとlimitはグループ単位で適用する
    const std::size_t begin = std::min<std::size_t>(options.offset, order.size());
    const std::size_t end = std::min<std::size_t>(begin + options.limit, order.size());
    result.hasMore = end < order.size();

    // 返却対象グループへイベント数の割り当てを決める (応答全体の上限は新しいグループから先に消費する)
    std::map<quint64, std::size_t> indexBySeq;  // グループ出現番号から結果内の位置への対応
    std::vector<quint32> allowed;               // 結果内の位置ごとの返却イベント数の上限
    quint64 remaining = limits.maxTotalEvents;  // 応答全体で残りのイベント数
    quint64 maxSeq = 0;                         // 返却対象で最も後ろのグループ出現番号

    for (std::size_t i = begin; i < end; ++i) {
        const GroupSummary &summary = summaries[order[i]];
        HistoryGroup group;
        group.seq = summary.seq;
        group.timestamp = summary.timestamp;
        group.command = summary.command;
        group.requestedBy = summary.requestedBy;
        group.userdata = summary.userdata;
        group.artificial = summary.artificial;
        group.eventCount = summary.eventCount;
        group.installCount = summary.installCount;
        group.removeCount = summary.removeCount;

        const quint64 allow = std::min<quint64>({summary.eventCount, limits.maxEventsPerGroup, remaining});
        remaining -= allow;
        allowed.push_back(static_cast<quint32>(allow));
        indexBySeq[group.seq] = result.groups.size();
        maxSeq = std::max(maxSeq, group.seq);
        result.groups.push_back(std::move(group));
    }

    // 2回目: 返却対象グループのイベントだけを集める
    if (!result.groups.empty()) {
        GroupSplitter splitter;
        constexpr std::size_t kNone = static_cast<std::size_t>(-1);  // 現在のグループが返却対象外であることを示す値
        std::size_t currentIndex = kNone;                            // 現在のグループの結果内の位置
        quint64 scannedSecond = 0;                                   // 2回目の走査で読んだエントリ数

        const bool ok = scanHistoryEntries(path, cancel, heartbeat, limits.maxBytes, limits.maxEntries, scannedFirst, scannedSecond,
            [&](const HistoryEntry &entry) {
                const GroupSplitter::Step step = splitter.feed(entry);

                // 新しいグループが始まったら、返却対象かどうかを判定する
                if (step.newGroup) {
                    const HistoryGroup &head = splitter.head();

                    // 返却対象より後のグループは不要のため、ここで走査を打ち切る
                    if (head.seq > maxSeq) {
                        return false;
                    }

                    const auto found = indexBySeq.find(head.seq);

                    // 返却対象でないグループの配下イベントは保持しない
                    if (found == indexBySeq.end()) {
                        currentIndex = kNone;
                    }
                    else {
                        currentIndex = found->second;
                        const HistoryGroup &expected = result.groups[currentIndex];

                        // 1回目と2回目の間にファイルが書き換わった場合は、別のグループへ誤って割り当てずにエラーにする
                        if (expected.timestamp != head.timestamp || expected.command != head.command) {
                            error = "history file changed while reading";
                            return false;
                        }
                    }
                }

                // 返却対象グループの配下イベントを、割り当てた件数まで保持する
                if (step.child && currentIndex != kNone && filter.matches(entry)) {
                    HistoryGroup &group = result.groups[currentIndex];

                    // 上限を超えた分は保持せず、eventCountとhiddenCountで省略を示す
                    if (group.events.size() < allowed[currentIndex]) {
                        group.events.push_back(entry);
                    }
                }

                return true;
            }, error);

        // 走査の失敗 (取消し、上限超過、ファイル書き換え) は理由をerrorに入れて失敗にする
        if (!ok) {
            return false;
        }

        // 期待した件数を集められなかったときは、ファイルが書き換わったとみなして失敗にする
        for (std::size_t i = 0; i < result.groups.size(); ++i) {
            if (result.groups[i].events.size() != allowed[i]) {
                error = "history file changed while reading";
                return false;
            }
        }
    }

    // 上限で返却しなかったイベントの総数を集計し、省略があることを明示する
    for (const HistoryGroup &group : result.groups) {
        result.hiddenEvents += group.hiddenCount();
    }

    result.truncated = result.hiddenEvents > 0;
    out = std::move(result);
    return true;
}

/**
 * @brief zyppヒストリをグループ単位で取得する
 * @param options 検証済みのグループ取得オプション
 * @param result 結果 (成功時のみ有効)
 * @param error 失敗時のエラーメッセージ (英語)
 * @return 成功した場合はtrue
 */
bool ZyppManager::getHistoryGroups(const HistoryGroupOptions &options, HistoryGroupResult &result, std::string &error)
{
    // 履歴ファイルの読み込み中は、操作状態の通知 (heartbeat) でアイドルタイムアウトによる終了を防ぐ
    return readHistoryGroups("/var/log/zypp/history", options, m_cancelRequested, [this] { m_operation.heartbeat(); }, result, error);
}

} // namespace qZypper
