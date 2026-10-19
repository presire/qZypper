#include "PackageListModel.h"

#include <QHash>
#include <QSet>
#include <utility>

namespace qZypper {

namespace {
// 安定キーの区切り文字 (パッケージ名・アーキ・リポジトリに出現しない制御文字)
constexpr QChar kKeySeparator = QChar(0x1f);

// 行単位の差分通知 (削除行数 + 挿入行数) の上限
// 超えた場合は一括リセットする
// 行単位のbeginRemoveRows / beginInsertRowsは、ソート有効なQSortFilterProxyModelとListViewで1件ごとにO(n)の再計算を誘発するため、
// リポジトリ切替のような一覧の入れ替えではO(n^2)となってGUIスレッドが長時間ブロックされる
constexpr int kMaxIncrementalRowChanges = 64;
} // namespace

/**
 * @brief PackageListModelのコンストラクタ
 * @param parent 親オブジェクト
 */
PackageListModel::PackageListModel(QObject *parent)
    : QAbstractListModel(parent)
{
}

/**
 * @brief 行数を返す
 * @param parent 親インデックス (リストモデルでは常に無効)
 * @return 行数
 */
int PackageListModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid())
        return 0;
    return m_entries.size();
}

/**
 * @brief 指定インデックスのロール値を返す
 * @param index 対象インデックス
 * @param role ロール
 * @return ロール値
 */
QVariant PackageListModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_entries.size())
        return QVariant();

    const QVariantMap &map = m_entries.at(index.row()).data;
    switch (role) {
        case NameRole:             return map.value(QStringLiteral("name"));
        case SummaryRole:          return map.value(QStringLiteral("summary"));
        case VersionRole:          return map.value(QStringLiteral("version"));
        case InstalledVersionRole: return map.value(QStringLiteral("installedVersion"));
        case ArchRole:             return map.value(QStringLiteral("arch"));
        case RepoAliasRole:        return map.value(QStringLiteral("repoAlias"));
        case RepoNameRole:         return map.value(QStringLiteral("repoName"));
        case SizeRole:             return map.value(QStringLiteral("size"));
        case StatusRole:           return map.value(QStringLiteral("status"));
        default:                   return QVariant();
    }
}

/**
 * @brief ロール名の対応表を返す
 * @return ロール番号とロール名のマップ
 */
QHash<int, QByteArray> PackageListModel::roleNames() const
{
    return {
        {NameRole,             "name"},
        {SummaryRole,          "summary"},
        {VersionRole,          "version"},
        {InstalledVersionRole, "installedVersion"},
        {ArchRole,             "arch"},
        {RepoAliasRole,        "repoAlias"},
        {RepoNameRole,         "repoName"},
        {SizeRole,             "size"},
        {StatusRole,           "status"}
    };
}

/**
 * @brief パッケージマップから安定キーを生成する
 * @param map パッケージマップ
 * @return 安定キー文字列
 */
QString PackageListModel::stableKey(const QVariantMap &map)
{
    // repoAliasは候補バージョンの変更で変わる属性のためキーに含めない
    // (含めると版変更で行が削除→再挿入され、選択行がずれる)
    return map.value(QStringLiteral("name")).toString() + kKeySeparator
         + map.value(QStringLiteral("arch")).toString();
}

/**
 * @brief パッケージ一覧を差分適用する
 *
 * 新一覧に無い行を削除し、相対順序が保存されていれば新規行を挿入、値が変化した行をdataChangedで通知する
 * 順序が変化した場合、および削除行数と挿入行数の合計がkMaxIncrementalRowChangesを超える場合 (一覧の入れ替え) は、
 * モデルリセットにフォールバックする
 *
 * @param packages PackageInfoマップのQVariantList
 */
void PackageListModel::applyPackages(const QVariantList &packages)
{
    QVector<Entry> incoming;
    incoming.reserve(packages.size());
    for (const QVariant &value : packages) {
        const QVariantMap map = value.toMap();
        Entry entry;
        entry.key = stableKey(map);
        entry.data = map;
        incoming.append(entry);
    }

    // 初回 (空 → 何か) はまとめて挿入
    if (m_entries.isEmpty()) {
        if (incoming.isEmpty())
            return;
        beginInsertRows(QModelIndex(), 0, incoming.size() - 1);
        m_entries = incoming;
        endInsertRows();
        return;
    }

    // 1. 新一覧に存在しない行を後ろから削除
    QSet<QString> newKeys;
    newKeys.reserve(incoming.size());
    for (const Entry &entry : incoming)
        newKeys.insert(entry.key);

    // キー重複がある一覧は差分適用できないため、リセットする
    if (newKeys.size() != incoming.size()) {
        beginResetModel();
        m_entries = incoming;
        endResetModel();
        return;
    }

    // 構造変更が多い場合 (リポジトリ切替等の一覧の入れ替え) は、行単位で通知せずにリセットする
    QSet<QString> currentKeys;
    currentKeys.reserve(m_entries.size());
    int removals = 0;
    for (const Entry &entry : std::as_const(m_entries)) {
        currentKeys.insert(entry.key);
        if (!newKeys.contains(entry.key))
            ++removals;
    }
    int insertions = 0;
    for (const Entry &entry : std::as_const(incoming)) {
        if (!currentKeys.contains(entry.key))
            ++insertions;
    }
    if (removals + insertions > kMaxIncrementalRowChanges) {
        beginResetModel();
        m_entries = incoming;
        endResetModel();
        return;
    }

    for (int i = m_entries.size() - 1; i >= 0; --i) {
        if (!newKeys.contains(m_entries.at(i).key)) {
            beginRemoveRows(QModelIndex(), i, i);
            m_entries.removeAt(i);
            endRemoveRows();
        }
    }

    // 2. 残存行の相対順序が新一覧と一致するか検証 (不一致ならリセット)
    QHash<QString, int> currentIndex;
    currentIndex.reserve(m_entries.size());
    for (int i = 0; i < m_entries.size(); ++i)
        currentIndex.insert(m_entries.at(i).key, i);

    int scan = 0;
    bool orderPreserved = true;
    for (const Entry &entry : incoming) {
        const int existing = currentIndex.value(entry.key, -1);
        if (existing < 0)
            continue;  // 新規挿入行
        if (existing != scan) {
            orderPreserved = false;
            break;
        }
        ++scan;
    }

    if (!orderPreserved) {
        beginResetModel();
        m_entries = incoming;
        endResetModel();
        return;
    }

    // 3. 新規行を正しい位置へ挿入 (残存行は順序保存されている)
    int cursor = 0;
    for (const Entry &entry : incoming) {
        if (cursor < m_entries.size() && m_entries.at(cursor).key == entry.key) {
            ++cursor;
            continue;
        }
        beginInsertRows(QModelIndex(), cursor, cursor);
        m_entries.insert(cursor, entry);
        endInsertRows();
        ++cursor;
    }

    // 4. 値が変化した行を通知
    const int count = qMin(m_entries.size(), incoming.size());
    for (int i = 0; i < count; ++i) {
        if (m_entries.at(i).data != incoming.at(i).data) {
            m_entries[i].data = incoming.at(i).data;
            emit dataChanged(index(i), index(i));
        }
    }
}

/**
 * @brief 保持している全行を削除する
 */
void PackageListModel::clear()
{
    if (m_entries.isEmpty())
        return;
    beginResetModel();
    m_entries.clear();
    endResetModel();
}

/**
 * @brief 指定行の生データを返す
 * @param row 行番号
 * @return 行データ (範囲外は空マップ)
 */
QVariantMap PackageListModel::get(int row) const
{
    if (row < 0 || row >= m_entries.size())
        return QVariantMap();
    return m_entries.at(row).data;
}

} // namespace qZypper
