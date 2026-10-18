#ifndef QZYPPER_GUI_MODELS_PACKAGEFILTERPROXYMODEL_H
#define QZYPPER_GUI_MODELS_PACKAGEFILTERPROXYMODEL_H

#include <QSortFilterProxyModel>

namespace qZypper {

/**
 * @brief PackageListModel 用のフィルタ/ソートプロキシ。
 *
 * 従来 QML の JavaScript で行っていた
 *  - ステータスフィルタ (statusMask のビット判定)
 *  - チェックボックス列 / パッケージ名列のソート
 * を C++ 側へ移したもの。
 */
class PackageFilterProxyModel : public QSortFilterProxyModel
{
    Q_OBJECT
    Q_PROPERTY(int statusMask READ statusMask WRITE setStatusMask NOTIFY statusMaskChanged)
    Q_PROPERTY(int sortColumn READ sortColumn WRITE setSortColumn NOTIFY sortColumnChanged)
    Q_PROPERTY(bool sortAscending READ sortAscending WRITE setSortAscending NOTIFY sortAscendingChanged)

public:
    /**
     * @brief ソート列の意味。
     */
    enum SortColumn {
        NoSort = -1,      // ソートなし (ソース順)
        CheckColumn = 0,  // チェックボックス列 (ステータス)
        NameColumn = 1    // パッケージ名列
    };
    Q_ENUM(SortColumn)

    explicit PackageFilterProxyModel(QObject *parent = nullptr);

    int statusMask() const { return m_statusMask; }
    void setStatusMask(int mask);

    int sortColumn() const { return m_sortColumn; }
    void setSortColumn(int column);

    /**
     * @brief 表示中の行から名前が指定プレフィックスで始まる最初の行を探す。
     *
     * QML 側の typeahead ジャンプ (パッケージ名の先頭一致) 用。
     * @param prefix 検索プレフィックス (大文字小文字は区別しない)
     * @return プロキシ行番号、見つからなければ -1
     */
    Q_INVOKABLE int indexOfNamePrefix(const QString &prefix) const;

    /// プロキシ行のパッケージ名を返す (範囲外は空文字列)
    Q_INVOKABLE QString nameAt(int row) const;

    bool sortAscending() const { return m_sortAscending; }
    void setSortAscending(bool ascending);

signals:
    void statusMaskChanged();
    void sortColumnChanged();
    void sortAscendingChanged();

protected:
    bool filterAcceptsRow(int sourceRow, const QModelIndex &sourceParent) const override;
    bool lessThan(const QModelIndex &left, const QModelIndex &right) const override;

private:
    // 一覧のチェック状態 (インストール済み/インストール・更新予定) を返す
    static bool isCheckedStatus(int status);

    int m_statusMask = 0x3FF;   // 既定: 全ステータス表示
    int m_sortColumn = NoSort;  // 既定: ソートなし
    bool m_sortAscending = true;
};

} // namespace qZypper

#endif // QZYPPER_GUI_MODELS_PACKAGEFILTERPROXYMODEL_H
