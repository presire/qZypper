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

std::vector<HistoryEntry> ZyppManager::getHistory(const HistoryOptions &options) const
{
    std::vector<HistoryEntry> result;

    const zypp::Pathname historyPath("/var/log/zypp/history");
    if (!zypp::PathInfo(historyPath).isExist()) {
        m_lastError = "history file not found";
        return result;
    }

    try {
        zypp::parser::HistoryLogReader reader(
            historyPath,
            zypp::parser::HistoryLogReader::Options(),
            [&result](const zypp::HistoryLogData::Ptr &data) -> bool {
                result.push_back(buildEntry(data));
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
        m_lastError = e.what();
        return {};
    }

    std::stable_sort(result.begin(), result.end(),
                     [](const HistoryEntry &lhs, const HistoryEntry &rhs) {
                         return lhs.timestamp > rhs.timestamp;
                     });

    const std::size_t skip = options.offset;
    if (skip >= result.size()) {
        return {};
    }
    const std::size_t take = std::min<std::size_t>(options.limit, result.size() - skip);
    return std::vector<HistoryEntry>(result.begin() + static_cast<std::ptrdiff_t>(skip),
                                     result.begin() + static_cast<std::ptrdiff_t>(skip + take));
}

} // namespace qZypper
