#include "PackageListModel.h"

#include <QHash>
#include <QSet>
#include <algorithm>
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
 *
 * SelectedRoleは行データではなく選択集合から求め、SortKeyRoleは行に保持したソートキーを返す
 * @param index 対象インデックス
 * @param role ロール
 * @return ロール値、範囲外や未知のロールでは無効なQVariant
 */
QVariant PackageListModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_entries.size())
        return QVariant();

    const Entry &entry = m_entries.at(index.row());
    const PackageRow &row = entry.row;

    // 戻り値の型はPackageRow::toVariantMap()と揃える (sizeはquint64のままQVariantに格納する)
    switch (role) {
        case NameRole:             return row.name;
        case SummaryRole:          return row.summary;
        case VersionRole:          return row.version;
        case InstalledVersionRole: return row.installedVersion;
        case ArchRole:             return row.arch;
        case RepoAliasRole:        return row.repoAlias;
        case RepoNameRole:         return row.repoName;
        case SizeRole:             return QVariant::fromValue(row.size);
        case StatusRole:           return row.status;
        case SelectedRole:         return m_selectedKeys.contains(entry.key);
        case UpdatableRole:        return row.updatable;
        case SortKeyRole:          return entry.sortKey;
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
        {StatusRole,           "status"},
        {SelectedRole,         "selected"},
        {UpdatableRole,        "updatable"}
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
 * 各行のソートキー (小文字化したパッケージ名) は、新一覧の行ごとにここで算出する
 * m_entriesを変更するたびに、end*Rows()・endResetModel()の前にrebuildRowIndex()でキー索引を再構築する
 * 通知を受けたハンドラがrowForKey()などを呼んでも、最新の行番号を引けるようにするため
 * 最後に、一覧から消えた行の選択をpurgeMissingSelection()で解除する
 * @param rows 表示するパッケージ行
 */
void PackageListModel::applyPackages(const PackageRows &rows)
{
    QVector<Entry> incoming;
    incoming.reserve(rows.size());
    for (const PackageRow &row : rows) {
        // 安定キーはstableKey()と同じ形式 (名前 + 区切り文字 + アーキ)
        Entry entry;
        entry.key = row.name + kKeySeparator + row.arch;
        entry.row = row;
        entry.sortKey = row.name.toLower();
        incoming.append(entry);
    }

    // 初回 (空 → 何か) はまとめて挿入
    if (m_entries.isEmpty()) {
        if (incoming.isEmpty())
            return;
        beginInsertRows(QModelIndex(), 0, incoming.size() - 1);
        m_entries = incoming;

        // endInsertRows()が発行するシグナルのハンドラが最新の行番号を引けるよう、通知の前に索引を再構築する
        rebuildRowIndex();
        endInsertRows();
        purgeMissingSelection();
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

        // endResetModel()が発行するシグナルのハンドラが最新の行番号を引けるよう、通知の前に索引を再構築する
        rebuildRowIndex();
        endResetModel();
        purgeMissingSelection();
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

        // endResetModel()が発行するシグナルのハンドラが最新の行番号を引けるよう、通知の前に索引を再構築する
        rebuildRowIndex();
        endResetModel();
        purgeMissingSelection();
        return;
    }

    for (int i = m_entries.size() - 1; i >= 0; --i) {
        if (!newKeys.contains(m_entries.at(i).key)) {
            beginRemoveRows(QModelIndex(), i, i);
            m_entries.removeAt(i);

            // 1行削除するたびに通知されるため、その都度、通知の前に索引を再構築する
            rebuildRowIndex();
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

        // endResetModel()が発行するシグナルのハンドラが最新の行番号を引けるよう、通知の前に索引を再構築する
        rebuildRowIndex();
        endResetModel();
        purgeMissingSelection();
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

        // 1行挿入するたびに通知されるため、その都度、通知の前に索引を再構築する
        rebuildRowIndex();
        endInsertRows();
        ++cursor;
    }

    // 4. 値が変化した行を通知
    // ここまでで行の並びは新一覧と一致しており、キー・名前は変わらないため索引の再構築は不要
    const int count = qMin(m_entries.size(), incoming.size());
    for (int i = 0; i < count; ++i) {
        if (!(m_entries.at(i).row == incoming.at(i).row)) {
            m_entries[i] = incoming.at(i);
            emit dataChanged(index(i), index(i));
        }
    }

    // 削除された行の安定キーを選択集合から除去する
    purgeMissingSelection();
}

/**
 * @brief 一覧の構造を変えずに、行の内容だけを取り直した結果で置き換える
 *
 * 安定キーで対応する行のうち、内容が変化した行だけを置き換え、1行ごとにdataChangedで通知する
 * 行の挿入・削除・並び替えと選択状態の変更は行わず、モデルに存在しない名前の行は無視する
 *
 * 次の場合は置き換える行を特定できないため、モデルを変更せずNeedsFullReloadを返す
 * 呼び出し元 (PackageController) は一覧全体を取り直す
 *  - 同じ名前でアーキテクチャが異なる行がある
 *  - モデル内で名前が重複している行がある
 *  - 引数の中に、同じ行を指す要素が2つ以上ある
 * 置き換えでは行のキーと名前が変わらないため、キー索引・名前索引・ソートキーは更新しない
 * @param rows 取り直したパッケージ行
 * @return 適用結果 (Applied : 通知済み、NeedsFullReload : モデルは未変更)
 */
RowUpdateResult PackageListModel::updateRows(const PackageRows &rows)
{
    // 1. 全行を検査する
    // 置き換えられない行が途中で見つかっても、モデルが中途半端に更新された状態にならないよう、検査を終えるまでモデルは変更しない
    QVector<Entry> incoming;
    incoming.reserve(rows.size());
    QSet<int> targets;
    targets.reserve(rows.size());
    for (const PackageRow &row : rows) {
        // モデル内に同名の行が複数あると、どの行を置き換えるべきか決められない
        if (m_duplicateNames.contains(row.name)) {
            return RowUpdateResult::NeedsFullReload;
        }

        Entry entry;
        entry.key = row.name + kKeySeparator + row.arch;
        entry.row = row;
        entry.sortKey = row.name.toLower();
        incoming.append(entry);

        const int target = m_rowByKey.value(entry.key, -1);
        if (target < 0) {
            // 名前だけが一致する行がある = アーキテクチャが変わった行
            if (m_rowByName.contains(row.name)) {
                return RowUpdateResult::NeedsFullReload;
            }
            // モデルに無い名前の行は、置き換え対象がないため無視する
            continue;
        }

        // 同じ行を2回置き換える入力は、どちらの内容を採るか決められない
        if (targets.contains(target)) {
            return RowUpdateResult::NeedsFullReload;
        }
        targets.insert(target);
    }

    // 2. 内容が変化した行だけを置き換えて通知する (行の追加・削除・並び替えは行わない)
    // 検査で重複や不整合がないと分かっているため、ここからは途中で失敗しない
    for (const Entry &entry : std::as_const(incoming)) {
        // 検査で無視すると決めた行 (モデルに無い名前) を飛ばす
        const int target = m_rowByKey.value(entry.key, -1);
        if (target < 0) {
            continue;
        }

        Entry &current = m_entries[target];
        // 内容が同じ行は通知しない
        if (current.row == entry.row) {
            continue;
        }

        current.row = entry.row;
        emit dataChanged(index(target), index(target));
    }

    return RowUpdateResult::Applied;
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

    // endResetModel()が発行するシグナルのハンドラが最新の索引を参照できるよう、通知の前に索引を空にする
    rebuildRowIndex();
    endResetModel();

    // 全行削除に伴い選択集合も破棄する
    purgeMissingSelection();
}

/**
 * @brief 指定行の内容をQVariantMapへ変換して返す
 * @param row 行番号
 * @return PackageRow::toVariantMap()の形式の行データ (範囲外は空マップ)
 */
QVariantMap PackageListModel::get(int row) const
{
    if (row < 0 || row >= m_entries.size())
        return QVariantMap();
    return m_entries.at(row).row.toVariantMap();
}

/**
 * @brief 指定行が選択されているかを返す
 * @param row 行番号
 * @return 選択済みならtrue、範囲外はfalse
 */
bool PackageListModel::isRowSelected(int row) const
{
    if (row < 0 || row >= m_entries.size()) {
        return false;
    }

    return m_selectedKeys.contains(m_entries.at(row).key);
}

/**
 * @brief 指定行の選択状態を設定する
 * @param row 行番号
 * @param selected 選択するならtrue
 */
void PackageListModel::setRowSelected(int row, bool selected)
{
    setRowsSelected(QList<int>{row}, selected);
}

/**
 * @brief 複数行の選択状態をまとめて設定する
 *
 * 実際に変化した行だけをdataChangedで通知し、変化がある場合のみselectionChangedを発行する
 * @param rows 行番号の一覧
 * @param selected 選択するならtrue
 */
void PackageListModel::setRowsSelected(const QList<int> &rows, bool selected)
{
    QList<int> changedRows;
    for (int row : rows) {
        if (row < 0 || row >= m_entries.size()) {
            continue;
        }

        const QString &key = m_entries.at(row).key;
        if (selected) {
            if (!m_selectedKeys.contains(key)) {
                m_selectedKeys.insert(key);
                changedRows.append(row);
            }
            continue;
        }

        if (m_selectedKeys.remove(key)) {
            changedRows.append(row);
        }
    }

    if (changedRows.isEmpty()) {
        return;
    }

    std::sort(changedRows.begin(), changedRows.end());
    changedRows.erase(std::unique(changedRows.begin(), changedRows.end()), changedRows.end());

    // 連続する行をまとめて選択状態の変化として通知する
    int rangeStart = changedRows.first();
    int rangeEnd = rangeStart;
    for (int i = 1; i < changedRows.size(); ++i) {
        if (changedRows.at(i) == rangeEnd + 1) {
            rangeEnd = changedRows.at(i);
            continue;
        }

        emit dataChanged(index(rangeStart), index(rangeEnd), {SelectedRole});
        rangeStart = changedRows.at(i);
        rangeEnd = rangeStart;
    }
    emit dataChanged(index(rangeStart), index(rangeEnd), {SelectedRole});

    emit selectionChanged();
}

/**
 * @brief すべての行の選択を解除する
 */
void PackageListModel::clearSelection()
{
    if (m_selectedKeys.isEmpty()) {
        return;
    }

    QList<int> rows;
    rows.reserve(m_entries.size());
    for (int i = 0; i < m_entries.size(); ++i) {
        if (m_selectedKeys.contains(m_entries.at(i).key)) {
            rows.append(i);
        }
    }

    setRowsSelected(rows, false);
}

/**
 * @brief 選択中の行数を返す
 * @return 選択済みキーの数
 */
int PackageListModel::selectedCount() const
{
    return m_selectedKeys.size();
}

/**
 * @brief 指定行の安定キーを返す
 * @param row 行番号
 * @return 安定キー、範囲外は空文字列
 */
QString PackageListModel::keyAt(int row) const
{
    if (row < 0 || row >= m_entries.size()) {
        return QString();
    }

    return m_entries.at(row).key;
}

/**
 * @brief 安定キーに対応する現在の行番号を返す
 *
 * キー索引 (m_rowByKey) を引くため、行数に依存せず一定時間で求まる
 * @param key 安定キー
 * @return 行番号、存在しなければ-1
 */
int PackageListModel::rowForKey(const QString &key) const
{
    if (key.isEmpty()) {
        return -1;
    }

    return m_rowByKey.value(key, -1);
}

/**
 * @brief 指定ステータスの行数を返す
 * @param status PackageStatusの整数値
 * @return ステータスが一致する行数
 */
int PackageListModel::countWithStatus(int status) const
{
    int count = 0;
    for (const Entry &entry : std::as_const(m_entries)) {
        if (entry.row.status == status) {
            ++count;
        }
    }

    return count;
}

/**
 * @brief 行番号引き当て用のキー索引と名前索引を現在の行から再構築する
 *
 * m_entriesを変更するたびに呼び出す
 * 名前が2行以上に現れる場合は、行を特定できないため名前索引から除外し、重複名として記録する
 */
void PackageListModel::rebuildRowIndex()
{
    m_rowByKey.clear();
    m_rowByKey.reserve(m_entries.size());
    m_rowByName.clear();
    m_rowByName.reserve(m_entries.size());
    m_duplicateNames.clear();
    for (int i = 0; i < m_entries.size(); ++i) {
        const Entry &entry = m_entries.at(i);
        m_rowByKey.insert(entry.key, i);

        const QString &name = entry.row.name;
        if (m_rowByName.contains(name)) {
            m_rowByName.remove(name);
            m_duplicateNames.insert(name);
        }
        else if (!m_duplicateNames.contains(name)) {
            // 3行目以降の同名行は、すでに重複名として記録済みのため索引へ戻さない
            m_rowByName.insert(name, i);
        }
    }
}

/**
 * @brief 現在の行に存在しない安定キーを選択集合から除去する
 *
 * 除去が発生した場合のみselectionChangedを発行する
 */
void PackageListModel::purgeMissingSelection()
{
    if (m_selectedKeys.isEmpty()) {
        return;
    }

    QSet<QString> presentKeys;
    presentKeys.reserve(m_entries.size());
    for (const Entry &entry : std::as_const(m_entries)) {
        presentKeys.insert(entry.key);
    }

    const int previousCount = m_selectedKeys.size();
    for (auto it = m_selectedKeys.begin(); it != m_selectedKeys.end();) {
        if (presentKeys.contains(*it)) {
            ++it;
            continue;
        }

        it = m_selectedKeys.erase(it);
    }

    if (m_selectedKeys.size() != previousCount) {
        emit selectionChanged();
    }
}

} // namespace qZypper
