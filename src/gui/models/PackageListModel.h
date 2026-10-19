#ifndef QZYPPER_GUI_MODELS_PACKAGELISTMODEL_H
#define QZYPPER_GUI_MODELS_PACKAGELISTMODEL_H

#include <QAbstractListModel>
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
        StatusRole                    // パッケージ状態 (PackageStatus)
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
     * @brief 行データから安定キーを生成する
     * @param map パッケージマップ
     * @return 安定キー文字列
     */
    static QString stableKey(const QVariantMap &map);

private:
    /** @brief 安定キーと表示データをまとめた内部行 */
    struct Entry {
        QString key;       // 安定キー
        QVariantMap data;  // 行データ
    };

    QVector<Entry> m_entries;  // 現在の行
};

} // namespace qZypper

#endif // QZYPPER_GUI_MODELS_PACKAGELISTMODEL_H
