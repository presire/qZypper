#include "PackageFilterProxyModel.h"
#include "PackageListModel.h"

#include <QRegularExpression>
#include <QSet>

namespace qZypper {

namespace {
// develパッケージ名の末尾パターン (-devel / -devel-32bit 等)
const QRegularExpression kDevelPattern(QStringLiteral("-devel(-\\d+bit)?$"));

// debuginfo/debugsourceパッケージ名の末尾パターン (-32bit付きも対象)
const QRegularExpression kDebugPattern(
    QStringLiteral("-(debuginfo|debugsource)(-\\d+bit)?$"));

// 未インストール状態 (PackageStatus::NoInst)
constexpr int kNoInstStatus = 0;

// インストール済みで変更なしの状態 (PackageStatus::KeepInstalled)
constexpr int kKeepInstalledStatus = 3;
} // namespace

/**
 * @brief PackageFilterProxyModelのコンストラクタ
 * @param parent 親オブジェクト
 */
PackageFilterProxyModel::PackageFilterProxyModel(QObject *parent)
    : QSortFilterProxyModel(parent)
{
    // ソースの値変化に追従して再フィルタ/再ソートする
    setDynamicSortFilter(true);

    // プロキシ自身の行構成が変わったら選択表示の変化を通知する
    connect(this, &QAbstractItemModel::rowsInserted, this, [this]() {
        emit selectionChanged();
    });
    connect(this, &QAbstractItemModel::rowsRemoved, this, [this]() {
        emit selectionChanged();
    });
    connect(this, &QAbstractItemModel::modelReset, this, [this]() {
        emit selectionChanged();
    });
    connect(this, &QAbstractItemModel::layoutChanged, this, [this]() {
        emit selectionChanged();
    });
}

/**
 * @brief ソースモデルを設定する
 *
 * 旧ソースの選択変更接続を解除し、PackageListModelなら選択変更を転送する
 * @param sourceModel 新しいソースモデル
 */
void PackageFilterProxyModel::setSourceModel(QAbstractItemModel *sourceModel)
{
    if (m_selectionConnection) {
        disconnect(m_selectionConnection);
    }

    QSortFilterProxyModel::setSourceModel(sourceModel);

    if (PackageListModel *model = qobject_cast<PackageListModel *>(sourceModel)) {
        // ソースの選択変更をプロキシの選択変更として通知する
        m_selectionConnection = connect(model, &PackageListModel::selectionChanged,
                                        this, [this]() {
            emit selectionChanged();
        });
    }

    // ソースが変わると旧キーは解決できないため起点を解除する
    if (!m_anchorKey.isEmpty()) {
        m_anchorKey.clear();
        emit anchorKeyChanged();
    }
}

/**
 * @brief ステータスフィルタマスクを設定する
 * @param mask ビットnが1の時にステータスnを表示
 */
void PackageFilterProxyModel::setStatusMask(int mask)
{
    if (m_statusMask == mask)
        return;
    m_statusMask = mask;
    invalidateFilter();
    emit statusMaskChanged();
    emit selectionChanged();
}

/**
 * @brief ソート列を設定する
 *
 * -1を指定するとソース順 (挿入順) に戻す
 * @param column ソート列
 */
void PackageFilterProxyModel::setSortColumn(int column)
{
    if (m_sortColumn == column)
        return;
    m_sortColumn = column;
    if (column < 0) {
        QSortFilterProxyModel::sort(-1);
    } else {
        // PackageListModelは単一カラム (0) のリストモデルのため、
        // 論理ソート列 (CheckColumn/NameColumn) ではなく実カラム0に対してソートを有効化する
        // ソートキーの選択は、lessThan()が行う
        QSortFilterProxyModel::sort(0,
            m_sortAscending ? Qt::AscendingOrder : Qt::DescendingOrder);
    }
    emit sortColumnChanged();
}

/**
 * @brief ソート方向を設定する
 * @param ascending 昇順ならtrue
 */
void PackageFilterProxyModel::setSortAscending(bool ascending)
{
    if (m_sortAscending == ascending)
        return;
    m_sortAscending = ascending;
    if (m_sortColumn < 0)
        QSortFilterProxyModel::sort(-1);
    else
        QSortFilterProxyModel::sort(0,
            ascending ? Qt::AscendingOrder : Qt::DescendingOrder);
    emit sortAscendingChanged();
}

/**
 * @brief develパッケージの非表示設定を変更する
 * @param hide 非表示にするならtrue
 */
void PackageFilterProxyModel::setHideDevel(bool hide)
{
    if (m_hideDevel == hide) {
        return;
    }

    m_hideDevel = hide;
    invalidateFilter();
    emit hideDevelChanged();
    emit selectionChanged();
}

/**
 * @brief debuginfo/debugsourceパッケージの非表示設定を変更する
 * @param hide 非表示にするならtrue
 */
void PackageFilterProxyModel::setHideDebug(bool hide)
{
    if (m_hideDebug == hide) {
        return;
    }

    m_hideDebug = hide;
    invalidateFilter();
    emit hideDebugChanged();
    emit selectionChanged();
}

/**
 * @brief 指定プロキシ行を選択範囲の起点にする
 * @param proxyRow プロキシ行番号 (無効な場合は起点を解除する)
 */
void PackageFilterProxyModel::setAnchorRow(int proxyRow)
{
    const QString key = sourceKeyAt(proxyRow);
    if (m_anchorKey == key) {
        return;
    }

    m_anchorKey = key;
    emit anchorKeyChanged();
}

/**
 * @brief 起点キーに対応する現在のプロキシ行番号を返す
 * @return プロキシ行番号、非表示または存在しなければ-1
 */
int PackageFilterProxyModel::anchorRow() const
{
    if (m_anchorKey.isEmpty()) {
        return -1;
    }

    const PackageListModel *model = packageModel();
    if (!model) {
        return -1;
    }

    const int sourceRow = model->rowForKey(m_anchorKey);
    if (sourceRow < 0) {
        return -1;
    }

    const QModelIndex proxyIndex = mapFromSource(model->index(sourceRow, 0));
    if (!proxyIndex.isValid()) {
        return -1;
    }

    return proxyIndex.row();
}

/**
 * @brief 指定行だけを選択する
 * @param row プロキシ行番号
 */
void PackageFilterProxyModel::selectOnly(int row)
{
    PackageListModel *model = packageModel();
    const QModelIndex sourceIndex = mapToSource(index(row, 0));
    if (!model || !sourceIndex.isValid()) {
        return;
    }

    model->clearSelection();
    model->setRowSelected(sourceIndex.row(), true);
    setAnchorRow(row);
}

/**
 * @brief 指定行の選択状態を反転する
 * @param row プロキシ行番号
 */
void PackageFilterProxyModel::toggleSelected(int row)
{
    PackageListModel *model = packageModel();
    const QModelIndex sourceIndex = mapToSource(index(row, 0));
    if (!model || !sourceIndex.isValid()) {
        return;
    }

    const int sourceRow = sourceIndex.row();
    model->setRowSelected(sourceRow, !model->isRowSelected(sourceRow));
}

/**
 * @brief 起点から指定行までの範囲で選択を置き換える
 * @param row プロキシ行番号
 */
void PackageFilterProxyModel::selectRange(int row)
{
    if (row < 0 || row >= rowCount()) {
        return;
    }

    // 起点が非表示または消滅している場合は、現在行を新しい起点にする
    if (anchorRow() < 0) {
        setAnchorRow(row);
    }

    const int anchor = anchorRow();
    if (anchor < 0) {
        return;
    }

    PackageListModel *model = packageModel();
    if (!model) {
        return;
    }

    const int first = qMin(anchor, row);
    const int last = qMax(anchor, row);
    const QList<int> sourceRows = sourceRowsInRange(first, last);
    model->clearSelection();
    model->setRowsSelected(sourceRows, true);
}

/**
 * @brief 起点から指定行までの範囲で選択を再構築する
 *
 * 前回より範囲が縮んだ場合も選択を削除する
 * @param row プロキシ行番号
 */
void PackageFilterProxyModel::extendRange(int row)
{
    // 追加ではなく常に起点から作り直し、縮小方向の操作でも選択を削除する
    selectRange(row);
}

/**
 * @brief 表示中の全行を選択する
 */
void PackageFilterProxyModel::selectAllVisible()
{
    PackageListModel *model = packageModel();
    if (!model) {
        return;
    }

    const QList<int> sourceRows = sourceRowsInRange(0, rowCount() - 1);
    model->clearSelection();
    model->setRowsSelected(sourceRows, true);
}

/**
 * @brief すべての選択を解除する
 */
void PackageFilterProxyModel::clearSelection()
{
    PackageListModel *model = packageModel();
    if (model) {
        model->clearSelection();
    }
}

/**
 * @brief 選択かつ表示中のパッケージ名をプロキシ順で返す
 * @return 重複を除いたパッケージ名の一覧
 */
QStringList PackageFilterProxyModel::selectedVisibleNames() const
{
    QStringList names;
    const PackageListModel *model = packageModel();
    if (!model) {
        return names;
    }

    QSet<QString> seen;
    const int rows = rowCount();
    for (int row = 0; row < rows; ++row) {
        const QModelIndex proxyIndex = index(row, 0);
        const QModelIndex sourceIndex = mapToSource(proxyIndex);
        if (!sourceIndex.isValid() || !model->isRowSelected(sourceIndex.row())) {
            continue;
        }

        const QString name = data(proxyIndex, PackageListModel::NameRole).toString();
        if (seen.contains(name)) {
            continue;
        }

        seen.insert(name);
        names.append(name);
    }

    return names;
}

/**
 * @brief 表示中のパッケージ名をプロキシ順で返す
 * @return 重複を除いたパッケージ名の一覧
 */
QStringList PackageFilterProxyModel::visibleNames() const
{
    QStringList names;
    QSet<QString> seen;
    const int rows = rowCount();
    for (int row = 0; row < rows; ++row) {
        const QString name =
            data(index(row, 0), PackageListModel::NameRole).toString();
        if (seen.contains(name)) {
            continue;
        }

        seen.insert(name);
        names.append(name);
    }

    return names;
}

/**
 * @brief 選択かつ表示中の行数を返す
 * @return 重複を除いた選択行数
 */
int PackageFilterProxyModel::selectedVisibleCount() const
{
    return selectedVisibleNames().size();
}

/**
 * @brief 指定プロキシ行のパッケージ名を返す
 * @param row プロキシ行番号
 * @return パッケージ名、範囲外なら空文字列
 */
QString PackageFilterProxyModel::nameAt(int row) const
{
    if (row < 0 || row >= rowCount())
        return {};
    return data(index(row, 0), PackageListModel::NameRole).toString();
}

/**
 * @brief 表示中の行から指定プレフィックスで始まる最初の名前を探す
 * @param prefix 検索プレフィックス (大文字小文字は区別しない)
 * @return プロキシ行番号、見つからなければ-1
 */
int PackageFilterProxyModel::indexOfNamePrefix(const QString &prefix) const
{
    const QString p = prefix.toLower();
    if (p.isEmpty())
        return -1;
    const int rows = rowCount();
    for (int row = 0; row < rows; ++row) {
        const QString name =
            data(index(row, 0), PackageListModel::NameRole).toString().toLower();
        if (name.startsWith(p))
            return row;
    }
    return -1;
}

/**
 * @brief 行を表示するかをステータスフィルタで判定する
 * @param sourceRow ソース行
 * @param sourceParent ソース親
 * @return 表示するならtrue
 */
bool PackageFilterProxyModel::filterAcceptsRow(int sourceRow,
                                               const QModelIndex &sourceParent) const
{
    const QAbstractItemModel *source = sourceModel();
    if (!source) {
        return true;
    }

    const QModelIndex idx = source->index(sourceRow, 0, sourceParent);
    const int status = source->data(idx, PackageListModel::StatusRole).toInt();
    if (status >= 0 && status <= 30) {
        if (((m_statusMask >> status) & 1) == 0) {
            return false;
        }
    }

    // 名前による除外は状態が変わらない行 (未インストール/変更なし) だけに適用する
    // インストール・削除予定などの行は操作対象のため常に表示する
    if (status == kNoInstStatus || status == kKeepInstalledStatus) {
        const QString name = source->data(idx, PackageListModel::NameRole).toString();
        if (m_hideDevel && kDevelPattern.match(name).hasMatch()) {
            return false;
        }

        if (m_hideDebug && kDebugPattern.match(name).hasMatch()) {
            return false;
        }
    }

    return true;
}

/**
 * @brief 2行の大小比較 (チェック状態またはパッケージ名)
 * @param left 左辺
 * @param right 右辺
 * @return left < rightならtrue
 */
bool PackageFilterProxyModel::lessThan(const QModelIndex &left,
                                       const QModelIndex &right) const
{
    const QAbstractItemModel *source = sourceModel();
    if (!source)
        return false;

    if (m_sortColumn == CheckColumn) {
        const int a = isCheckedStatus(
            source->data(left, PackageListModel::StatusRole).toInt()) ? 1 : 0;
        const int b = isCheckedStatus(
            source->data(right, PackageListModel::StatusRole).toInt()) ? 1 : 0;
        return a < b;
    }

    const QString a = source->data(left, PackageListModel::NameRole).toString().toLower();
    const QString b = source->data(right, PackageListModel::NameRole).toString().toLower();
    return a < b;
}

/**
 * @brief ソースモデルをPackageListModelとして返す
 * @return キャストできればモデル、できなければnullptr
 */
PackageListModel *PackageFilterProxyModel::packageModel() const
{
    return qobject_cast<PackageListModel *>(sourceModel());
}

/**
 * @brief 指定プロキシ行に対応するソース行の安定キーを返す
 * @param proxyRow プロキシ行番号
 * @return 安定キー、範囲外は空文字列
 */
QString PackageFilterProxyModel::sourceKeyAt(int proxyRow) const
{
    if (proxyRow < 0 || proxyRow >= rowCount()) {
        return QString();
    }

    const QModelIndex sourceIndex = mapToSource(index(proxyRow, 0));
    if (!sourceIndex.isValid()) {
        return QString();
    }

    const PackageListModel *model = packageModel();
    if (!model) {
        return QString();
    }

    return model->keyAt(sourceIndex.row());
}

/**
 * @brief 指定プロキシ行範囲に対応するソース行番号を返す
 * @param firstProxyRow 開始プロキシ行
 * @param lastProxyRow 終了プロキシ行
 * @return 有効なソース行番号の一覧
 */
QList<int> PackageFilterProxyModel::sourceRowsInRange(int firstProxyRow,
                                                      int lastProxyRow) const
{
    QList<int> sourceRows;
    if (firstProxyRow < 0) {
        firstProxyRow = 0;
    }

    const int rows = rowCount();
    for (int row = firstProxyRow; row <= lastProxyRow && row < rows; ++row) {
        const QModelIndex sourceIndex = mapToSource(index(row, 0));
        if (sourceIndex.isValid()) {
            sourceRows.append(sourceIndex.row());
        }
    }

    return sourceRows;
}

/**
 * @brief 一覧のチェック状態 (インストール済み/予定) を返す
 *
 * PackageStatusInfo.isChecked (QML) と同一の判定
 * @param status パッケージ状態
 * @return チェック状態ならtrue
 */
bool PackageFilterProxyModel::isCheckedStatus(int status)
{
    return status == 3 || status == 1 || status == 2
        || status == 4 || status == 5 || status == 9;
}

} // namespace qZypper
