#ifndef QZYPPER_GUI_MODELS_PACKAGEFILTERPROXYMODEL_H
#define QZYPPER_GUI_MODELS_PACKAGEFILTERPROXYMODEL_H

#include <QSortFilterProxyModel>
#include <QStringList>

namespace qZypper {

class PackageListModel;

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
    Q_PROPERTY(bool hideDevel READ hideDevel WRITE setHideDevel NOTIFY hideDevelChanged)           // develパッケージの非表示
    Q_PROPERTY(bool hideDebug READ hideDebug WRITE setHideDebug NOTIFY hideDebugChanged)           // debuginfo/debugsourceの非表示
    Q_PROPERTY(QString anchorKey READ anchorKey NOTIFY anchorKeyChanged)                          // 選択範囲の起点キー
    Q_PROPERTY(int selectedVisibleCount READ selectedVisibleCount NOTIFY selectionChanged)         // 表示中の選択行数

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

    /**
     * @brief develパッケージを非表示にするかを返す
     * @return 非表示ならtrue
     */
    bool hideDevel() const { return m_hideDevel; }

    /**
     * @brief develパッケージの非表示設定を変更する
     * @param hide 非表示にするならtrue
     */
    void setHideDevel(bool hide);

    /**
     * @brief debuginfo/debugsourceパッケージを非表示にするかを返す
     * @return 非表示ならtrue
     */
    bool hideDebug() const { return m_hideDebug; }

    /**
     * @brief debuginfo/debugsourceパッケージの非表示設定を変更する
     * @param hide 非表示にするならtrue
     */
    void setHideDebug(bool hide);

    /**
     * @brief 選択範囲の起点となる安定キーを返す
     * @return 起点キー、未設定なら空文字列
     */
    QString anchorKey() const { return m_anchorKey; }

    /**
     * @brief 指定プロキシ行を選択範囲の起点にする
     * @param proxyRow プロキシ行番号 (無効な場合は起点を解除する)
     */
    Q_INVOKABLE void setAnchorRow(int proxyRow);

    /**
     * @brief 起点キーに対応する現在のプロキシ行番号を返す
     * @return プロキシ行番号、非表示または存在しなければ-1
     */
    Q_INVOKABLE int anchorRow() const;

    /**
     * @brief 指定行だけを選択する
     * @param row プロキシ行番号
     */
    Q_INVOKABLE void selectOnly(int row);

    /**
     * @brief 指定行の選択状態を反転する
     * @param row プロキシ行番号
     */
    Q_INVOKABLE void toggleSelected(int row);

    /**
     * @brief 起点から指定行までの範囲で選択を置き換える
     * @param row プロキシ行番号
     */
    Q_INVOKABLE void selectRange(int row);

    /**
     * @brief 起点から指定行までの範囲で選択を再構築する
     *
     * 前回より範囲が縮んだ場合も選択を削除する
     * @param row プロキシ行番号
     */
    Q_INVOKABLE void extendRange(int row);

    /**
     * @brief 表示中の全行を選択する
     */
    Q_INVOKABLE void selectAllVisible();

    /**
     * @brief すべての選択を解除する
     */
    Q_INVOKABLE void clearSelection();

    /**
     * @brief 選択かつ表示中のパッケージ名をプロキシ順で返す
     * @return 重複を除いたパッケージ名の一覧
     */
    Q_INVOKABLE QStringList selectedVisibleNames() const;

    /**
     * @brief 表示中のパッケージ名をプロキシ順で返す
     * @return 重複を除いたパッケージ名の一覧
     */
    Q_INVOKABLE QStringList visibleNames() const;

    /**
     * @brief 選択かつ表示中の行数を返す
     * @return 重複を除いた選択行数
     */
    int selectedVisibleCount() const;

    /**
     * @brief ソースモデルを設定する
     * @param sourceModel 新しいソースモデル
     */
    void setSourceModel(QAbstractItemModel *sourceModel) override;

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
    /**
     * @brief develパッケージ非表示設定の変更を通知する
     */
    void hideDevelChanged();
    /**
     * @brief debugパッケージ非表示設定の変更を通知する
     */
    void hideDebugChanged();
    /**
     * @brief 選択範囲の起点キーの変更を通知する
     */
    void anchorKeyChanged();
    /**
     * @brief 選択状態または表示行の変更を通知する
     */
    void selectionChanged();

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
    /**
     * @brief ソースモデルをPackageListModelとして返す
     * @return キャストできればモデル、できなければnullptr
     */
    PackageListModel *packageModel() const;

    /**
     * @brief 指定プロキシ行に対応するソース行の安定キーを返す
     * @param proxyRow プロキシ行番号
     * @return 安定キー、範囲外は空文字列
     */
    QString sourceKeyAt(int proxyRow) const;

    /**
     * @brief 指定プロキシ行範囲に対応するソース行番号を返す
     * @param firstProxyRow 開始プロキシ行
     * @param lastProxyRow 終了プロキシ行
     * @return 有効なソース行番号の一覧
     */
    QList<int> sourceRowsInRange(int firstProxyRow, int lastProxyRow) const;

    // 一覧のチェック状態 (インストール済み/インストール・更新予定) を返す
    static bool isCheckedStatus(int status);

    int m_statusMask = 0x3FF;                       // 既定: 全ステータス表示
    int m_sortColumn = NoSort;                      // 既定: ソートなし
    bool m_sortAscending = true;                    // 既定: 昇順
    bool m_hideDevel = true;                        // 既定: develパッケージを非表示
    bool m_hideDebug = true;                        // 既定: debugパッケージを非表示
    QString m_anchorKey;                            // 選択範囲の起点となる安定キー
    QMetaObject::Connection m_selectionConnection;  // ソース選択変更の接続
};

} // namespace qZypper

#endif // QZYPPER_GUI_MODELS_PACKAGEFILTERPROXYMODEL_H
