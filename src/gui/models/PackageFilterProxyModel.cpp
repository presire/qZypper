#include "PackageFilterProxyModel.h"
#include "PackageListModel.h"

namespace qZypper {

/**
 * @brief PackageFilterProxyModelのコンストラクタ
 * @param parent 親オブジェクト
 */
PackageFilterProxyModel::PackageFilterProxyModel(QObject *parent)
    : QSortFilterProxyModel(parent)
{
    // ソースの値変化に追従して再フィルタ/再ソートする
    setDynamicSortFilter(true);
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
    if (!source)
        return true;
    const QModelIndex idx = source->index(sourceRow, 0, sourceParent);
    const int status = source->data(idx, PackageListModel::StatusRole).toInt();
    if (status < 0 || status > 30)
        return true;
    return ((m_statusMask >> status) & 1) == 1;
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
