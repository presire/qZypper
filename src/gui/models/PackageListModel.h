#ifndef QZYPPER_GUI_MODELS_PACKAGELISTMODEL_H
#define QZYPPER_GUI_MODELS_PACKAGELISTMODEL_H

#include <QAbstractListModel>
#include <QVariantList>
#include <QVariantMap>
#include <QVector>

namespace qZypper {

/**
 * @brief パッケージ一覧を保持する QAbstractListModel。
 *
 * 従来 QML の ListView に QVariantList を丸ごと差し替えていた表示集合を
 * 差分更新 (dataChanged / rowsInserted / rowsRemoved) で維持する。
 *
 * 差分の同一性は「名前 + アーキテクチャ + リポジトリアライアス」を連結した
 * 安定キーで判定する。並び順が保存されている通常の更新では行の再生成を
 * 行わず、値の変化のみを dataChanged で通知する。
 */
class PackageListModel : public QAbstractListModel
{
    Q_OBJECT

public:
    /**
     * @brief QML へ公開するロール (QVariantMap のキーと 1:1)。
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

    explicit PackageListModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    /**
     * @brief パッケージ一覧を差分適用する。
     *
     * 既存行と安定キーで突き合わせ、可能な限りリセットせずに
     * 行の挿入・削除・値の更新のみを通知する。
     * @param packages PackageInfo::toVariantMap() 形式の QVariantList
     */
    Q_INVOKABLE void applyPackages(const QVariantList &packages);

    /** @brief 保持している全行を削除する。 */
    Q_INVOKABLE void clear();

    /**
     * @brief 指定行の生の QVariantMap を返す (コンテキストメニュー等で使用)。
     * @param row 行番号
     * @return 行データ (範囲外は空マップ)
     */
    Q_INVOKABLE QVariantMap get(int row) const;

    /**
     * @brief 行データから安定キーを生成する。
     * @param map パッケージマップ
     * @return 安定キー文字列
     */
    static QString stableKey(const QVariantMap &map);

private:
    struct Entry {
        QString key;      // 安定キー
        QVariantMap data; // 行データ
    };

    QVector<Entry> m_entries;  // 現在の行
};

} // namespace qZypper

#endif // QZYPPER_GUI_MODELS_PACKAGELISTMODEL_H
