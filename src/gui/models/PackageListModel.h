#ifndef QZYPPER_GUI_MODELS_PACKAGELISTMODEL_H
#define QZYPPER_GUI_MODELS_PACKAGELISTMODEL_H

#include <QAbstractListModel>
#include <QList>
#include <QSet>
#include <QVariantList>
#include <QVariantMap>
#include <QVector>

namespace qZypper {

/**
 * @class PackageListModel
 * @brief パッケージ一覧を保持するQAbstractListModel
 *
 * 従来QMLのListViewにQVariantListを丸ごと差し替えていた表示集合を
 * 差分更新 (dataChanged / rowsInserted / rowsRemoved) で維持する
 *
 * 差分の同一性は、名前とアーキテクチャを区切り文字で連結した安定キーで判定する
 * 候補バージョンの変更でリポジトリが変わっても同じ行として扱うため、リポジトリエイリアスはキーに含めない
 * 並び順が保存されている通常の更新では行の再生成を行わず、値の変化のみをdataChangedで通知する
 * 一覧の入れ替え (リポジトリ切替等) で構造変更が多い場合は、行単位の通知がO(n^2)になるためリセットする
 */
class PackageListModel : public QAbstractListModel
{
    Q_OBJECT

public:
    /**
     * @brief QMLへ公開するロール (QVariantMapのキーと1:1)
     */
    enum Roles {
        NameRole = Qt::UserRole + 1,  // パッケージ名
        SummaryRole,                  // 概要
        VersionRole,                  // 候補バージョン
        InstalledVersionRole,         // インストール済みバージョン
        ArchRole,                     // アーキテクチャ
        RepoAliasRole,                // リポジトリアライアス
        RepoNameRole,                 // リポジトリ表示名
        SizeRole,                     // インストールサイズ (bytes)
        StatusRole,                   // パッケージ状態 (PackageStatus)
        SelectedRole,                 // 選択状態 (bool)
        UpdatableRole                 // 導入済みより新しい更新候補があるか (bool)
    };
    Q_ENUM(Roles)

    /**
     * @brief 空のパッケージ一覧モデルを初期化する
     * @param parent 親オブジェクト
     */
    explicit PackageListModel(QObject *parent = nullptr);

    /**
     * @brief 保持する行数を返す
     * @param parent 親インデックス (リストモデルのため有効な親では0を返す)
     * @return 行数
     */
    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    /**
     * @brief 指定ロールの行データを返す
     * @param index 対象行のインデックス
     * @param role 取得するロール (Roles)
     * @return ロールに対応する値、範囲外や未知のロールでは無効なQVariant
     */
    QVariant data(const QModelIndex &index, int role) const override;
    /**
     * @brief QMLへ公開するロール名を返す
     * @return ロール値とロール名 (QVariantMapのキー) の対応表
     */
    QHash<int, QByteArray> roleNames() const override;

    /**
     * @brief パッケージ一覧を差分適用する
     *
     * 既存行と安定キーで突き合わせ、可能な限りリセットせずに
     * 行の挿入・削除・値の更新のみを通知する
     * 並び順の変化時、および挿入・削除行数が多い場合はモデルリセットする
     * @param packages PackageInfo::toVariantMap()形式のQVariantList
     */
    Q_INVOKABLE void applyPackages(const QVariantList &packages);

    /** @brief 保持している全行を削除する */
    Q_INVOKABLE void clear();

    /**
     * @brief 指定行の生のQVariantMapを返す (コンテキストメニュー等で使用)
     * @param row 行番号
     * @return 行データ (範囲外は空マップ)
     */
    Q_INVOKABLE QVariantMap get(int row) const;

    /**
     * @brief 指定行が選択されているかを返す
     * @param row 行番号
     * @return 選択済みならtrue、範囲外はfalse
     */
    bool isRowSelected(int row) const;

    /**
     * @brief 指定行の選択状態を設定する
     * @param row 行番号
     * @param selected 選択するならtrue
     */
    void setRowSelected(int row, bool selected);

    /**
     * @brief 複数行の選択状態をまとめて設定する
     *
     * 実際に変化した行だけをdataChangedで通知し、変化がある場合のみselectionChangedを発行する
     * @param rows 行番号の一覧
     * @param selected 選択するならtrue
     */
    void setRowsSelected(const QList<int> &rows, bool selected);

    /**
     * @brief すべての行の選択を解除する
     */
    void clearSelection();

    /**
     * @brief 選択中の行数を返す
     * @return 選択済みキーの数
     */
    int selectedCount() const;

    /**
     * @brief 指定行の安定キーを返す
     * @param row 行番号
     * @return 安定キー、範囲外は空文字列
     */
    QString keyAt(int row) const;

    /**
     * @brief 安定キーに対応する現在の行番号を返す
     * @param key 安定キー
     * @return 行番号、存在しなければ-1
     */
    int rowForKey(const QString &key) const;

    /**
     * @brief 指定ステータスの行数を返す
     * @param status PackageStatusの整数値
     * @return ステータスが一致する行数
     */
    Q_INVOKABLE int countWithStatus(int status) const;

    /**
     * @brief 行データから安定キーを生成する
     * @param map パッケージマップ
     * @return 安定キー文字列
     */
    static QString stableKey(const QVariantMap &map);

signals:
    /**
     * @brief 選択状態の集合が変化したことを通知する
     */
    void selectionChanged();

private:
    /**
     * @brief 現在の行に存在しない安定キーを選択集合から除去する
     *
     * 除去が発生した場合のみselectionChangedを発行する
     */
    void purgeMissingSelection();

    /** @brief 安定キーと表示データをまとめた内部行 */
    struct Entry {
        QString key;       // 安定キー
        QVariantMap data;  // 行データ
    };

    QVector<Entry> m_entries;         // 現在の行
    QSet<QString> m_selectedKeys;     // 選択中の行の安定キー
};

} // namespace qZypper

#endif // QZYPPER_GUI_MODELS_PACKAGELISTMODEL_H
