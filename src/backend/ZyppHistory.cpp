#include "ZyppHistory.h"

#include "ZyppManager.h"

#include <zypp/Date.h>
#include <zypp/PathInfo.h>
#include <zypp/HistoryLogData.h>
#include <zypp/parser/HistoryLogReader.h>

#include <algorithm>
#include <memory>
#include <set>
#include <string>
#include <vector>

namespace qZypper {

namespace {

/**
 * @brief 履歴フィルタで許可する操作種別の集合を返す。
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
 * @brief 操作種別名を zypp の HistoryActionID に変換する。
 * @param action 正規化済み操作種別名
 * @return 対応する HistoryActionID
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
 * @brief zypp の HistoryActionID を表示用の操作種別名に変換する。
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
 * @brief HistoryLogData から表示用の HistoryEntry を構築する。
 * @param data 履歴 1 行分のデータ
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
    } else if (auto p = zypp::dynamic_pointer_cast<HistoryLogDataRemove>(data)) {
        entry.name = QString::fromStdString(p->name());
        entry.edition = QString::fromStdString(p->edition().asString());
        entry.arch = QString::fromStdString(p->arch().asString());
        entry.requestedBy = QString::fromStdString(p->reqby());
        entry.userdata = QString::fromStdString(p->userdata());
    } else if (auto p = zypp::dynamic_pointer_cast<HistoryLogDataRepoAdd>(data)) {
        entry.name = QString::fromStdString(p->alias());
        entry.repoAlias = entry.name;
        entry.userdata = QString::fromStdString(p->userdata());
    } else if (auto p = zypp::dynamic_pointer_cast<HistoryLogDataRepoRemove>(data)) {
        entry.name = QString::fromStdString(p->alias());
        entry.repoAlias = entry.name;
        entry.userdata = QString::fromStdString(p->userdata());
    } else if (auto p = zypp::dynamic_pointer_cast<HistoryLogDataRepoAliasChange>(data)) {
        entry.name = QString::fromStdString(p->newAlias());
        entry.repoAlias = entry.name;
        entry.userdata = QString::fromStdString(p->userdata());
    } else if (auto p = zypp::dynamic_pointer_cast<HistoryLogDataRepoUrlChange>(data)) {
        entry.name = QString::fromStdString(p->alias());
        entry.repoAlias = entry.name;
        entry.userdata = QString::fromStdString(p->userdata());
    } else if (auto p = zypp::dynamic_pointer_cast<HistoryLogDataStampCommand>(data)) {
        entry.requestedBy = QString::fromStdString(p->executedBy());
        entry.command = QString::fromStdString(p->command());
        entry.userdata = QString::fromStdString(p->userdata());
    } else if (auto p = zypp::dynamic_pointer_cast<HistoryLogPatchStateChange>(data)) {
        entry.name = QString::fromStdString(p->name());
        entry.edition = QString::fromStdString(p->edition().asString());
        entry.arch = QString::fromStdString(p->arch().asString());
        entry.repoAlias = QString::fromStdString(p->repoAlias());
    }
    return entry;
}

} // namespace

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
    return map;
}

BoundedHistoryCollector::BoundedHistoryCollector(quint32 offset, quint32 limit)
    : m_capacity(static_cast<std::size_t>(offset) + static_cast<std::size_t>(limit))
    , m_offset(offset)
{
    m_kept.reserve(m_capacity);
}

bool BoundedHistoryCollector::newerThan(const Item &a, const Item &b)
{
    if (a.entry.timestamp != b.entry.timestamp) {
        return a.entry.timestamp > b.entry.timestamp;
    }
    return a.seq < b.seq;
}

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

    // ヒープ先頭は最終順序で最下位 (最初に捨てる候補)。より新しければ差し替える。
    if (newerThan(item, m_kept.front())) {
        std::pop_heap(m_kept.begin(), m_kept.end(), HeapCompare());
        m_kept.back() = item;
        std::push_heap(m_kept.begin(), m_kept.end(), HeapCompare());
    }
}

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
        options.since = value;
    }
    if (map.contains(QStringLiteral("until"))) {
        bool ok = false;
        const qlonglong value = map.value(QStringLiteral("until")).toLongLong(&ok);
        if (!ok || value < 0) {
            error = "invalid 'until'";
            return false;
        }
        options.until = value;
    }
    if (options.since > 0 && options.until > 0 && options.since > options.until) {
        error = "'since' must be <= 'until'";
        return false;
    }
    if (map.contains(QStringLiteral("limit"))) {
        bool ok = false;
        const qulonglong value = map.value(QStringLiteral("limit")).toULongLong(&ok);
        if (!ok || value == 0 || value > kMaxHistoryLimit) {
            error = "invalid 'limit'";
            return false;
        }
        options.limit = static_cast<quint32>(value);
    }
    if (map.contains(QStringLiteral("offset"))) {
        bool ok = false;
        const qulonglong value = map.value(QStringLiteral("offset")).toULongLong(&ok);
        if (!ok) {
            error = "invalid 'offset'";
            return false;
        }
        options.offset = static_cast<quint32>(value);
    }

    // 保持件数 offset + limit を上限内に制限する (メモリ使用量の安全弁)。
    if (options.offset > kMaxHistoryWindow
        || options.limit > kMaxHistoryWindow - options.offset) {
        error = "invalid 'offset'";
        return false;
    }

    const QVariant actionsVar = map.value(QStringLiteral("actions"));
    if (!actionsVar.isNull()) {
        QStringList actions;
        if (actionsVar.typeId() == QMetaType::QStringList) {
            actions = actionsVar.toStringList();
        } else {
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
    // 先にファイルサイズで上限を掛ける (フィルタ不一致や不正行も対象)。
    if (static_cast<quint64>(info.size()) > limits.maxBytes) {
        error = "history file is too large (" + std::to_string(info.size())
                + " bytes, limit " + std::to_string(limits.maxBytes) + ")";
        return false;
    }

    // ファイル走査中に watchdog へ進捗を伝える heartbeat 間隔 (エントリ数)。
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
                // HistoryLogReader はコールバックの false で読み込みを打ち切る。
                if (cancel.load()) {
                    cancelled = true;
                    return false;
                }
                // 上限で打ち切った結果は最新側を欠くため、部分結果は返さない。
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
        } else {
            reader.readAll();
        }
    } catch (const std::exception &e) {
        error = e.what();
        return false;
    }

    if (cancelled) {
        error = "Operation cancelled";
        return false;
    }
    if (tooMany) {
        error = "history has too many entries (limit " + std::to_string(limits.maxEntries) + ")";
        return false;
    }

    out = collector.take();
    return true;
}

bool ZyppManager::getHistory(const HistoryOptions &options, std::vector<HistoryEntry> &entries,
                             std::string &error)
{
    return readHistoryFile("/var/log/zypp/history", options, m_cancelRequested,
                           [this] { m_operation.heartbeat(); }, entries, error);
}

} // namespace qZypper
