#ifndef QZYPPER_GUI_MODELS_PACKAGEFILTERPROXYMODEL_H
#define QZYPPER_GUI_MODELS_PACKAGEFILTERPROXYMODEL_H

#include <QSortFilterProxyModel>

namespace qZypper {

/**
 * @class PackageFilterProxyModel
 * @brief PackageListModel用のフィルタ/ソートプロキシ
 *
 *  - ステータスフィルタ (statusMaskのビット判定)
 *  - チェックボックス列 / パッケージ名列のソート
 */
class PackageFilterProxyModel : public QSortFilterProxyModel
{
    Q_OBJECT
    Q_PROPERTY(int statusMask READ statusMask WRITE setStatusMask NOTIFY statusMaskChanged)
    Q_PROPERTY(int sortColumn READ sortColumn WRITE setSortColumn NOTIFY sortColumnChanged)
    Q_PROPERTY(bool sortAscending READ sortAscending WRITE setSortAscending NOTIFY sortAscendingChanged)

public:
    /**
     * @brief ソート列の意味
     */
    enum SortColumn {
        NoSort = -1,      // ソートなし (ソース順)
        CheckColumn = 0,  // チェックボックス列 (ステータス)
        NameColumn = 1    // パッケージ名列
    };
    Q_ENUM(SortColumn)

    /**
     * @brief プロキシモデルを初期化する
     * @param parent 親オブジェクト
     */
    explicit PackageFilterProxyModel(QObject *parent = nullptr);

    /**
     * @brief 現在のステータスフィルタを返す
     * @return PackageStatusのビットマスク
     */
    int statusMask() const { return m_statusMask; }

    /**
     * @brief ステータスフィルタを設定する
     * @param mask 表示するPackageStatusのビットマスク
     */
    void setStatusMask(int mask);

    /**
     * @brief 現在の論理ソート列を返す
     * @return 論理ソート列の番号
     */
    int sortColumn() const { return m_sortColumn; }

    /**
     * @brief 論理ソート列を設定する
     * @param column ソート列 (SortColumn)
     */
    void setSortColumn(int column);

    /**
     * @brief 表示中の行から名前が指定プレフィックスで始まる最初の行を探す
     *
     * QML側のtypeaheadジャンプ (パッケージ名の先頭一致) 用
     * @param prefix 検索プレフィックス (大文字小文字は区別しない)
     * @return プロキシ行番号、見つからなければ-1
     */
    Q_INVOKABLE int indexOfNamePrefix(const QString &prefix) const;

    /**
     * @brief 指定プロキシ行のパッケージ名を返す
     * @param row プロキシ行番号
     * @return パッケージ名、範囲外なら空文字列
     */
    Q_INVOKABLE QString nameAt(int row) const;

    /**
     * @brief 現在のソート方向が昇順か返す
     * @return 昇順の場合はtrue
     */
    bool sortAscending() const { return m_sortAscending; }

    /**
     * @brief ソート方向を設定する
     * @param ascending 昇順の場合はtrue
     */
    void setSortAscending(bool ascending);

signals:
    /**
     * @brief ステータスフィルタの変更を通知する
     */
    void statusMaskChanged();
    /**
     * @brief ソート列の変更を通知する
     */
    void sortColumnChanged();
    /**
     * @brief ソート方向の変更を通知する
     */
    void sortAscendingChanged();

protected:
    /**
     * @brief 行の表示可否を判定する
     * @param sourceRow ソースモデルの行番号
     * @param sourceParent ソースモデルの親インデックス
     * @return 表示する場合true
     */
    bool filterAcceptsRow(int sourceRow, const QModelIndex &sourceParent) const override;

    /**
     * @brief 2行の並び順を判定する
     * @param left 比較元のインデックス
     * @param right 比較先のインデックス
     * @return leftがrightより前に並ぶ場合はtrue
     */
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
