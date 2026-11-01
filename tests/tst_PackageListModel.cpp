#include <QtTest>
#include <QSignalSpy>
#include <initializer_list>
#include "PackageListModel.h"
#include "PackageFilterProxyModel.h"
#include "PackageRow.h"

using namespace qZypper;

namespace {

/**
 * @brief テスト用のパッケージ行を生成する
 * @param name パッケージ名
 * @param status 状態を表す整数値
 * @param arch アーキテクチャ (既定はx86_64)
 * @param repo リポジトリ別名 (既定はoss)
 * @param version バージョン (既定は1.0)
 * @return 一覧表示に使う属性を埋めた行
 */
PackageRow pkg(const QString &name, int status,
               const QString &arch = QStringLiteral("x86_64"),
               const QString &repo = QStringLiteral("oss"),
               const QString &version = QStringLiteral("1.0"))
{
    PackageRow row;
    row.name = name;
    row.arch = arch;
    row.repoAlias = repo;
    row.repoName = repo;
    row.version = version;
    row.installedVersion = QString();
    row.summary = QStringLiteral("summary of ") + name;
    row.size = 1024;
    row.status = status;
    return row;
}

/**
 * @brief 可変個のパッケージ行を一覧へまとめる
 * @param rows 変換するパッケージ行の一覧
 * @return 入力順を保ったPackageRows
 */
PackageRows toList(std::initializer_list<PackageRow> rows)
{
    PackageRows out;
    out.reserve(static_cast<int>(rows.size()));
    for (const PackageRow &row : rows)
        out.append(row);
    return out;
}

/**
 * @brief 既定アーキテクチャのパッケージ名から安定キーを生成する
 * @param name パッケージ名
 * @return x86_64を付加した安定キー
 */
QString keyForName(const QString &name)
{
    return name + QChar(0x1f) + QStringLiteral("x86_64");
}

/**
 * @brief 選択中のソース行の名前をソース順で返す
 * @param model 対象モデル
 * @return 選択中のパッケージ名の一覧
 */
QStringList selectedSourceNames(const PackageListModel &model)
{
    QStringList names;
    for (int row = 0; row < model.rowCount(); ++row) {
        if (!model.isRowSelected(row)) {
            continue;
        }
        names.append(model.data(model.index(row, 0),
                                PackageListModel::NameRole).toString());
    }
    return names;
}

/**
 * @brief 指定名のソース行が選択されているかを返す
 * @param model 対象モデル
 * @param name パッケージ名
 * @return 選択済みならtrue
 */
bool isNameSelected(const PackageListModel &model, const QString &name)
{
    const int row = model.rowForKey(keyForName(name));
    return row >= 0 && model.isRowSelected(row);
}

} // namespace

/**
 * @class PackageListModelTest
 * @brief PackageListModelの差分更新とPackageFilterProxyModelのフィルタ・ソートを検証する
 *
 * 初期挿入・追加・値変更・削除・クリア時のシグナル、安定キーの扱い、リポジトリ変更時の行の位置保持、状態マスクによるフィルタ、
 * 名前ソートと前方一致検索を対象にする
 *
 * root権限は不要
 */
class PackageListModelTest : public QObject
{
    Q_OBJECT

private slots:
    /** @brief 初期挿入でrowsInsertedが1回だけ送信し、modelResetは出ないことを検証する */
    void initialInsertEmitsRowsInserted()
    {
        PackageListModel model;
        QSignalSpy inserted(&model, &QAbstractItemModel::rowsInserted);
        QSignalSpy reset(&model, &QAbstractItemModel::modelReset);

        model.applyPackages(toList({pkg(QStringLiteral("a"), 3),
                                    pkg(QStringLiteral("b"), 1)}));

        QCOMPARE(model.rowCount(), 2);
        QCOMPARE(inserted.count(), 1);
        QCOMPARE(reset.count(), 0);
        QCOMPARE(inserted.first().at(1).toInt(), 0);
        QCOMPARE(inserted.first().at(2).toInt(), 1);
    }

    /** @brief 末尾追加でrowsInsertedのみが送信することを検証する */
    void appendEmitsRowsInsertedOnly()
    {
        PackageListModel model;
        model.applyPackages(toList({pkg(QStringLiteral("a"), 3),
                                    pkg(QStringLiteral("b"), 1)}));

        QSignalSpy inserted(&model, &QAbstractItemModel::rowsInserted);
        QSignalSpy removed(&model, &QAbstractItemModel::rowsRemoved);
        QSignalSpy reset(&model, &QAbstractItemModel::modelReset);

        model.applyPackages(toList({pkg(QStringLiteral("a"), 3),
                                    pkg(QStringLiteral("b"), 1),
                                    pkg(QStringLiteral("c"), 0)}));

        QCOMPARE(model.rowCount(), 3);
        QCOMPARE(inserted.count(), 1);
        QCOMPARE(removed.count(), 0);
        QCOMPARE(reset.count(), 0);
        QCOMPARE(inserted.first().at(1).toInt(), 2);
        QCOMPARE(inserted.first().at(2).toInt(), 2);
    }

    /** @brief 値変更でdataChangedのみを送信し、modelResetされないことを検証する */
    void valueChangeEmitsDataChangedNotReset()
    {
        PackageListModel model;
        model.applyPackages(toList({pkg(QStringLiteral("a"), 3),
                                    pkg(QStringLiteral("b"), 1)}));

        QSignalSpy changed(&model, &QAbstractItemModel::dataChanged);
        QSignalSpy reset(&model, &QAbstractItemModel::modelReset);
        QSignalSpy inserted(&model, &QAbstractItemModel::rowsInserted);

        model.applyPackages(toList({pkg(QStringLiteral("a"), 5),
                                    pkg(QStringLiteral("b"), 1)}));

        QCOMPARE(model.rowCount(), 2);
        QCOMPARE(changed.count(), 1);
        QCOMPARE(changed.first().at(0).toModelIndex().row(), 0);
        QCOMPARE(changed.first().at(1).toModelIndex().row(), 0);
        QCOMPARE(reset.count(), 0);
        QCOMPARE(inserted.count(), 0);
        QCOMPARE(model.data(model.index(0, 0), PackageListModel::StatusRole).toInt(), 5);
    }

    /** @brief 削除でrowsRemovedを送信し、残りが詰められることを検証する */
    void removalEmitsRowsRemoved()
    {
        PackageListModel model;
        model.applyPackages(toList({pkg(QStringLiteral("a"), 3),
                                    pkg(QStringLiteral("b"), 1),
                                    pkg(QStringLiteral("c"), 0)}));

        QSignalSpy removed(&model, &QAbstractItemModel::rowsRemoved);

        model.applyPackages(toList({pkg(QStringLiteral("a"), 3),
                                    pkg(QStringLiteral("c"), 0)}));

        QCOMPARE(model.rowCount(), 2);
        QCOMPARE(removed.count(), 1);
        QCOMPARE(model.data(model.index(0, 0), PackageListModel::NameRole).toString(), QStringLiteral("a"));
        QCOMPARE(model.data(model.index(1, 0), PackageListModel::NameRole).toString(), QStringLiteral("c"));
    }

    /** @brief 一覧の入れ替え (リポジトリ切替等) では行単位の通知をせず、modelResetを1回だけ送信することを検証する */
    void largeReplacementResetsModel()
    {
        PackageRows before;
        PackageRows after;
        for (int i = 0; i < 200; ++i) {
            before.append(pkg(QStringLiteral("a%1").arg(i, 3, 10, QLatin1Char('0')), 1));
            after.append(pkg(QStringLiteral("b%1").arg(i, 3, 10, QLatin1Char('0')), 1));
        }

        PackageListModel model;
        model.applyPackages(before);

        QSignalSpy inserted(&model, &QAbstractItemModel::rowsInserted);
        QSignalSpy removed(&model, &QAbstractItemModel::rowsRemoved);
        QSignalSpy reset(&model, &QAbstractItemModel::modelReset);

        model.applyPackages(after);

        QCOMPARE(model.rowCount(), 200);
        QCOMPARE(reset.count(), 1);
        QCOMPARE(inserted.count(), 0);
        QCOMPARE(removed.count(), 0);
        QCOMPARE(model.data(model.index(0, 0), PackageListModel::NameRole).toString(), QStringLiteral("b000"));
        QCOMPARE(model.data(model.index(199, 0), PackageListModel::NameRole).toString(), QStringLiteral("b199"));
    }

    /** @brief clearでmodelResetを送信し、行数が0になることを検証する */
    void clearResetsModel()
    {
        PackageListModel model;
        model.applyPackages(toList({pkg(QStringLiteral("a"), 3),
                                    pkg(QStringLiteral("b"), 1)}));

        QSignalSpy reset(&model, &QAbstractItemModel::modelReset);
        model.clear();

        QCOMPARE(model.rowCount(), 0);
        QCOMPARE(reset.count(), 1);
    }

    /** @brief 安定キーがアーキテクチャで区別し、リポジトリでは区別しないことを検証する */
    void stableKeyDistinguishesArchButNotRepo()
    {
        const PackageRow a = pkg(QStringLiteral("x"), 0,
                                 QStringLiteral("x86_64"), QStringLiteral("oss"));
        const PackageRow b = pkg(QStringLiteral("x"), 0,
                                 QStringLiteral("i586"), QStringLiteral("oss"));
        const PackageRow c = pkg(QStringLiteral("x"), 0,
                                 QStringLiteral("x86_64"), QStringLiteral("update"));
        QVERIFY(PackageListModel::stableKey(a.toVariantMap())
                != PackageListModel::stableKey(b.toVariantMap()));
        QCOMPARE(PackageListModel::stableKey(a.toVariantMap()),
                 PackageListModel::stableKey(c.toVariantMap()));
    }

    /** @brief 対象リポジトリ変更で行を移動せず、dataChangedのみを出すことを検証する */
    void candidateRepoChangeKeepsRowInPlace()
    {
        PackageListModel model;
        model.applyPackages(toList({pkg(QStringLiteral("a"), 0, QStringLiteral("x86_64"), QStringLiteral("oss")),
                                    pkg(QStringLiteral("b"), 0, QStringLiteral("x86_64"), QStringLiteral("oss")),
                                    pkg(QStringLiteral("c"), 0, QStringLiteral("x86_64"), QStringLiteral("oss"))}));

        QSignalSpy removed(&model, &QAbstractItemModel::rowsRemoved);
        QSignalSpy inserted(&model, &QAbstractItemModel::rowsInserted);
        QSignalSpy changed(&model, &QAbstractItemModel::dataChanged);

        model.applyPackages(toList({pkg(QStringLiteral("a"), 0, QStringLiteral("x86_64"), QStringLiteral("oss")),
                                    pkg(QStringLiteral("b"), 1, QStringLiteral("x86_64"), QStringLiteral("update")),
                                    pkg(QStringLiteral("c"), 0, QStringLiteral("x86_64"), QStringLiteral("oss"))}));

        QCOMPARE(removed.count(), 0);
        QCOMPARE(inserted.count(), 0);
        QCOMPARE(changed.count(), 1);
        QCOMPARE(model.get(1).value(QStringLiteral("repoAlias")).toString(), QStringLiteral("update"));
    }

    /** @brief 状態マスクでプロキシが行を絞り込むことを検証する */
    void proxyFiltersByStatusMask()
    {
        PackageListModel model;
        model.applyPackages(toList({pkg(QStringLiteral("a"), 3),
                                    pkg(QStringLiteral("b"), 1),
                                    pkg(QStringLiteral("c"), 0)}));

        PackageFilterProxyModel proxy;
        proxy.setSourceModel(&model);

        // 既定マスクは全表示
        QCOMPARE(proxy.rowCount(), 3);

        // status 3のみ表示
        proxy.setStatusMask(1 << 3);
        QCOMPARE(proxy.rowCount(), 1);
        QCOMPARE(proxy.index(0, 0).data(PackageListModel::NameRole).toString(), QStringLiteral("a"));
    }

    /** @brief 名前列の昇順・降順ソートを検証する */
    void proxySortsByName()
    {
        PackageListModel model;
        model.applyPackages(toList({pkg(QStringLiteral("ccc"), 0),
                                    pkg(QStringLiteral("aaa"), 0),
                                    pkg(QStringLiteral("bbb"), 0)}));

        PackageFilterProxyModel proxy;
        proxy.setSourceModel(&model);
        proxy.setSortColumn(PackageFilterProxyModel::NameColumn);
        proxy.setSortAscending(true);

        QCOMPARE(proxy.index(0, 0).data(PackageListModel::NameRole).toString(), QStringLiteral("aaa"));
        QCOMPARE(proxy.index(2, 0).data(PackageListModel::NameRole).toString(), QStringLiteral("ccc"));

        proxy.setSortAscending(false);
        QCOMPARE(proxy.index(0, 0).data(PackageListModel::NameRole).toString(), QStringLiteral("ccc"));
        QCOMPARE(proxy.index(2, 0).data(PackageListModel::NameRole).toString(), QStringLiteral("aaa"));
    }

    /** @brief 名前の前方一致検索が大文字小文字を無視し、見つからなければ-1を返すことを検証する */
    void proxyIndexOfNamePrefix()
    {
        PackageListModel model;
        model.applyPackages(toList({pkg(QStringLiteral("alpha"), 0),
                                    pkg(QStringLiteral("beta"), 0),
                                    pkg(QStringLiteral("gamma"), 0)}));

        PackageFilterProxyModel proxy;
        proxy.setSourceModel(&model);

        QCOMPARE(proxy.indexOfNamePrefix(QStringLiteral("be")), 1);
        QCOMPARE(proxy.indexOfNamePrefix(QStringLiteral("GAM")), 2);
        QCOMPARE(proxy.indexOfNamePrefix(QStringLiteral("zz")), -1);
        QCOMPARE(proxy.indexOfNamePrefix(QString()), -1);
    }

    /** @brief nameAtがソート順に従い範囲外では空文字を返すことを検証する */
    void proxyNameAtFollowsSortOrder()
    {
        PackageListModel model;
        model.applyPackages(toList({pkg(QStringLiteral("ccc"), 0),
                                    pkg(QStringLiteral("aaa"), 0)}));

        PackageFilterProxyModel proxy;
        proxy.setSourceModel(&model);
        proxy.setSortColumn(PackageFilterProxyModel::NameColumn);
        proxy.setSortAscending(true);

        QCOMPARE(proxy.nameAt(0), QStringLiteral("aaa"));
        QCOMPARE(proxy.nameAt(1), QStringLiteral("ccc"));
        QCOMPARE(proxy.nameAt(-1), QString());
        QCOMPARE(proxy.nameAt(2), QString());
    }

    /** @brief 選択ロールとselectionChangedの送信条件を検証する */
    void selectionRoleAndSignal()
    {
        PackageListModel model;
        model.applyPackages(toList({pkg(QStringLiteral("a"), 3),
                                    pkg(QStringLiteral("b"), 3)}));

        QSignalSpy selection(&model, &PackageListModel::selectionChanged);
        QSignalSpy changed(&model, &QAbstractItemModel::dataChanged);

        QCOMPARE(model.roleNames().value(PackageListModel::SelectedRole),
                 QByteArray("selected"));
        QVERIFY(!model.data(model.index(1, 0),
                            PackageListModel::SelectedRole).toBool());

        model.setRowSelected(1, true);

        QCOMPARE(model.selectedCount(), 1);
        QVERIFY(model.isRowSelected(1));
        QVERIFY(!model.isRowSelected(0));
        QVERIFY(model.data(model.index(1, 0),
                           PackageListModel::SelectedRole).toBool());
        QCOMPARE(selection.count(), 1);
        QCOMPARE(changed.count(), 1);

        // 状態が変わらない再設定では信号を送らない
        model.setRowSelected(1, true);
        QCOMPARE(selection.count(), 1);
        QCOMPARE(changed.count(), 1);

        model.setRowSelected(1, false);
        QCOMPARE(model.selectedCount(), 0);
        QCOMPARE(selection.count(), 2);
    }

    /** @brief リセット後も存在するキーの選択が維持されることを検証する */
    void selectionSurvivesResetForPresentKeys()
    {
        PackageListModel model;
        model.applyPackages(toList({pkg(QStringLiteral("a"), 3),
                                    pkg(QStringLiteral("b"), 3),
                                    pkg(QStringLiteral("c"), 3)}));
        model.setRowsSelected(QList<int>{0, 1, 2}, true);
        QCOMPARE(model.selectedCount(), 3);

        QSignalSpy reset(&model, &QAbstractItemModel::modelReset);
        QSignalSpy selection(&model, &PackageListModel::selectionChanged);

        // 逆順にすると差分適用できずリセットになる
        model.applyPackages(toList({pkg(QStringLiteral("c"), 3),
                                    pkg(QStringLiteral("b"), 3),
                                    pkg(QStringLiteral("a"), 3)}));

        QCOMPARE(reset.count(), 1);
        QCOMPARE(model.selectedCount(), 3);
        QCOMPARE(selectedSourceNames(model),
                 QStringList({QStringLiteral("c"), QStringLiteral("b"), QStringLiteral("a")}));

        // 存在するキーだけを残すため、何も削除されず選択変更も出ない
        QCOMPARE(selection.count(), 0);
    }

    /** @brief 削除された行の選択キーが除去されることを検証する */
    void staleSelectionKeysPurgedOnRemoval()
    {
        PackageListModel model;
        model.applyPackages(toList({pkg(QStringLiteral("a"), 3),
                                    pkg(QStringLiteral("b"), 3),
                                    pkg(QStringLiteral("c"), 3)}));
        model.setRowsSelected(QList<int>{0, 1}, true);

        QSignalSpy selection(&model, &PackageListModel::selectionChanged);
        model.applyPackages(toList({pkg(QStringLiteral("a"), 3),
                                    pkg(QStringLiteral("c"), 3)}));

        QCOMPARE(model.rowCount(), 2);
        QCOMPARE(model.selectedCount(), 1);
        QVERIFY(isNameSelected(model, QStringLiteral("a")));
        QVERIFY(!isNameSelected(model, QStringLiteral("b")));
        QCOMPARE(selection.count(), 1);
    }

    /** @brief リセットで消えた行の選択キーが除去されることを検証する */
    void staleSelectionKeysPurgedOnReset()
    {
        PackageRows before;
        PackageRows after;
        for (int i = 0; i < 200; ++i) {
            before.append(pkg(QStringLiteral("a%1").arg(i, 3, 10, QLatin1Char('0')), 3));
            after.append(pkg(QStringLiteral("b%1").arg(i, 3, 10, QLatin1Char('0')), 3));
        }

        PackageListModel model;
        model.applyPackages(before);
        model.setRowSelected(0, true);
        QCOMPARE(model.selectedCount(), 1);

        QSignalSpy reset(&model, &QAbstractItemModel::modelReset);
        QSignalSpy selection(&model, &PackageListModel::selectionChanged);
        model.applyPackages(after);

        QCOMPARE(reset.count(), 1);
        QCOMPARE(model.selectedCount(), 0);
        QCOMPARE(selection.count(), 1);
    }

    /** @brief clearで選択が破棄されることを検証する */
    void clearPurgesSelection()
    {
        PackageListModel model;
        model.applyPackages(toList({pkg(QStringLiteral("a"), 3),
                                    pkg(QStringLiteral("b"), 3)}));
        model.setRowsSelected(QList<int>{0, 1}, true);
        QCOMPARE(model.selectedCount(), 2);

        model.clear();

        QCOMPARE(model.rowCount(), 0);
        QCOMPARE(model.selectedCount(), 0);
    }

    /** @brief toggleSelected・selectOnly・clearSelectionを検証する */
    void toggleSelectOnlyAndClear()
    {
        PackageListModel model;
        model.applyPackages(toList({pkg(QStringLiteral("a"), 3),
                                    pkg(QStringLiteral("b"), 3),
                                    pkg(QStringLiteral("c"), 3)}));

        PackageFilterProxyModel proxy;
        proxy.setSourceModel(&model);

        proxy.toggleSelected(0);
        QCOMPARE(model.selectedCount(), 1);
        QCOMPARE(proxy.selectedVisibleCount(), 1);
        QCOMPARE(proxy.selectedVisibleNames(), QStringList({QStringLiteral("a")}));

        proxy.toggleSelected(0);
        QCOMPARE(model.selectedCount(), 0);

        proxy.selectOnly(1);
        QCOMPARE(proxy.selectedVisibleNames(), QStringList({QStringLiteral("b")}));
        QCOMPARE(proxy.anchorRow(), 1);

        proxy.toggleSelected(2);
        QCOMPARE(model.selectedCount(), 2);
        QCOMPARE(proxy.selectedVisibleNames(),
                 QStringList({QStringLiteral("b"), QStringLiteral("c")}));

        proxy.clearSelection();
        QCOMPARE(model.selectedCount(), 0);
        QVERIFY(proxy.selectedVisibleNames().isEmpty());
    }

    /** @brief selectRangeとextendRangeの範囲構築・縮小を検証する */
    void selectRangeAndExtendRange()
    {
        PackageListModel model;
        model.applyPackages(toList({pkg(QStringLiteral("a"), 3),
                                    pkg(QStringLiteral("b"), 3),
                                    pkg(QStringLiteral("c"), 3),
                                    pkg(QStringLiteral("d"), 3),
                                    pkg(QStringLiteral("e"), 3)}));

        PackageFilterProxyModel proxy;
        proxy.setSourceModel(&model);

        proxy.setAnchorRow(1);
        proxy.selectRange(3);
        QCOMPARE(proxy.selectedVisibleNames(),
                 QStringList({QStringLiteral("b"), QStringLiteral("c"), QStringLiteral("d")}));

        // Shiftで範囲を縮めた場合は、はみ出した行の選択を削除する
        proxy.extendRange(2);
        QCOMPARE(proxy.selectedVisibleNames(),
                 QStringList({QStringLiteral("b"), QStringLiteral("c")}));
        QCOMPARE(model.selectedCount(), 2);

        proxy.extendRange(4);
        QCOMPARE(proxy.selectedVisibleNames(),
                 QStringList({QStringLiteral("b"), QStringLiteral("c"),
                             QStringLiteral("d"), QStringLiteral("e")}));
    }

    /** @brief ソート順が変わってもキーで起点を追跡することを検証する */
    void rangeAfterSortOrderTracksAnchorKey()
    {
        PackageListModel model;
        model.applyPackages(toList({pkg(QStringLiteral("c"), 3),
                                    pkg(QStringLiteral("a"), 3),
                                    pkg(QStringLiteral("b"), 3)}));

        PackageFilterProxyModel proxy;
        proxy.setSourceModel(&model);
        proxy.setSortColumn(PackageFilterProxyModel::NameColumn);
        proxy.setSortAscending(true);

        proxy.setAnchorRow(0);
        QCOMPARE(proxy.anchorKey(), keyForName(QStringLiteral("a")));

        // 降順では起点のaが末尾 (行2) へ移動する
        proxy.setSortAscending(false);
        QCOMPARE(proxy.anchorRow(), 2);

        proxy.extendRange(1);
        QCOMPARE(proxy.selectedVisibleNames(),
                 QStringList({QStringLiteral("b"), QStringLiteral("a")}));
        QCOMPARE(model.selectedCount(), 2);
        QVERIFY(!isNameSelected(model, QStringLiteral("c")));
    }

    /** @brief 起点がフィルタで消えた場合に現在行へ再設定することを検証する */
    void extendRangeReanchorsWhenAnchorFiltered()
    {
        PackageListModel model;
        model.applyPackages(toList({pkg(QStringLiteral("a"), 3),
                                    pkg(QStringLiteral("b"), 0),
                                    pkg(QStringLiteral("c"), 3)}));

        PackageFilterProxyModel proxy;
        proxy.setSourceModel(&model);
        proxy.setAnchorRow(1);
        QCOMPARE(proxy.anchorRow(), 1);

        // status 0のbを非表示にすると起点が解決できなくなる
        proxy.setStatusMask(1 << 3);
        QCOMPARE(proxy.anchorRow(), -1);

        proxy.extendRange(1);
        QCOMPARE(proxy.anchorRow(), 1);
        QCOMPARE(proxy.selectedVisibleNames(), QStringList({QStringLiteral("c")}));
        QCOMPARE(model.selectedCount(), 1);
    }

    /** @brief hideDevelが未インストール/変更なしの行だけを除外することを検証する */
    void hideDevelFiltersNonChangedStatusesOnly()
    {
        PackageListModel model;
        model.applyPackages(toList({pkg(QStringLiteral("qz-app-devel"), 0),
                                    pkg(QStringLiteral("qz-app"), 1),
                                    pkg(QStringLiteral("qz-del-devel"), 6),
                                    pkg(QStringLiteral("qz-lib-devel-32bit"), 0),
                                    pkg(QStringLiteral("qz-keep-devel"), 3),
                                    pkg(QStringLiteral("qz-visible"), 3)}));

        PackageFilterProxyModel proxy;
        proxy.setSourceModel(&model);

        QCOMPARE(proxy.visibleNames(),
                 QStringList({QStringLiteral("qz-app"), QStringLiteral("qz-del-devel"),
                             QStringLiteral("qz-visible")}));
        QVERIFY(!proxy.visibleNames().contains(QStringLiteral("qz-app-devel")));
        QVERIFY(!proxy.visibleNames().contains(QStringLiteral("qz-lib-devel-32bit")));
        QVERIFY(!proxy.visibleNames().contains(QStringLiteral("qz-keep-devel")));

        // 削除予定の行はdevel名でも常に表示する
        QVERIFY(proxy.visibleNames().contains(QStringLiteral("qz-del-devel")));

        QSignalSpy changed(&proxy, &PackageFilterProxyModel::hideDevelChanged);
        proxy.setHideDevel(false);
        QCOMPARE(changed.count(), 1);
        QVERIFY(proxy.visibleNames().contains(QStringLiteral("qz-app-devel")));
        QVERIFY(proxy.visibleNames().contains(QStringLiteral("qz-lib-devel-32bit")));

        proxy.setHideDevel(true);
        QVERIFY(!proxy.visibleNames().contains(QStringLiteral("qz-app-devel")));
    }

    /** @brief hideDebugがdebuginfo/debugsourceを除外することを検証する */
    void hideDebugFiltersDebugPackages()
    {
        PackageListModel model;
        model.applyPackages(toList({pkg(QStringLiteral("qz-app-debuginfo"), 0),
                                    pkg(QStringLiteral("qz-app-debugsource"), 3),
                                    pkg(QStringLiteral("qz-app-debuginfo-32bit"), 0),
                                    pkg(QStringLiteral("qz-del-debuginfo"), 6),
                                    pkg(QStringLiteral("qz-app"), 1)}));

        PackageFilterProxyModel proxy;
        proxy.setSourceModel(&model);

        QCOMPARE(proxy.visibleNames(),
                 QStringList({QStringLiteral("qz-del-debuginfo"), QStringLiteral("qz-app")}));
        QVERIFY(!proxy.visibleNames().contains(QStringLiteral("qz-app-debuginfo")));
        QVERIFY(!proxy.visibleNames().contains(QStringLiteral("qz-app-debugsource")));
        QVERIFY(!proxy.visibleNames().contains(QStringLiteral("qz-app-debuginfo-32bit")));

        QSignalSpy changed(&proxy, &PackageFilterProxyModel::hideDebugChanged);
        proxy.setHideDebug(false);
        QCOMPARE(changed.count(), 1);
        QVERIFY(proxy.visibleNames().contains(QStringLiteral("qz-app-debugsource")));

        proxy.setHideDebug(true);
        QVERIFY(!proxy.visibleNames().contains(QStringLiteral("qz-app-debugsource")));
    }

    /** @brief 表示中かつ選択中の名前だけを返すことを検証する */
    void selectedVisibleNamesExcludesFilteredRows()
    {
        PackageListModel model;
        model.applyPackages(toList({pkg(QStringLiteral("a"), 3),
                                    pkg(QStringLiteral("b"), 3),
                                    pkg(QStringLiteral("c"), 3)}));

        PackageFilterProxyModel proxy;
        proxy.setSourceModel(&model);
        proxy.selectAllVisible();
        QCOMPARE(proxy.selectedVisibleCount(), 3);

        // status 3だけを表示するマスクに切り替え、bを状態変更なしにすると表示から外れる
        proxy.setStatusMask(1 << 3);
        model.applyPackages(toList({pkg(QStringLiteral("a"), 3),
                                    pkg(QStringLiteral("b"), 0),
                                    pkg(QStringLiteral("c"), 3)}));

        QCOMPARE(proxy.visibleNames(),
                 QStringList({QStringLiteral("a"), QStringLiteral("c")}));
        QCOMPARE(proxy.selectedVisibleNames(),
                 QStringList({QStringLiteral("a"), QStringLiteral("c")}));
        QCOMPARE(proxy.selectedVisibleCount(), 2);
        QCOMPARE(model.selectedCount(), 3);
    }

    /** @brief 表示名一覧が重複を除いてプロキシ順に返ることを検証する */
    void visibleNamesAreDeDuplicated()
    {
        PackageListModel model;
        model.applyPackages(toList({pkg(QStringLiteral("a"), 3, QStringLiteral("i586")),
                                    pkg(QStringLiteral("b"), 3),
                                    pkg(QStringLiteral("a"), 3)}));

        PackageFilterProxyModel proxy;
        proxy.setSourceModel(&model);
        proxy.selectAllVisible();

        QCOMPARE(proxy.visibleNames(),
                 QStringList({QStringLiteral("a"), QStringLiteral("b")}));
        QCOMPARE(proxy.selectedVisibleNames(),
                 QStringList({QStringLiteral("a"), QStringLiteral("b")}));
        QCOMPARE(proxy.selectedVisibleCount(), 2);
    }

    /** @brief hideDevelが有効でも削除予定のdevelパッケージを表示することを検証する */
    void hideDevelKeepsDeleteMarkedDevel()
    {
        PackageListModel model;
        model.applyPackages(toList({pkg(QStringLiteral("qz-app-devel"), 6)}));

        PackageFilterProxyModel proxy;
        proxy.setSourceModel(&model);

        QVERIFY(proxy.hideDevel());
        QCOMPARE(proxy.rowCount(), 1);
        QCOMPARE(proxy.visibleNames(), QStringList({QStringLiteral("qz-app-devel")}));
    }

    /** @brief countWithStatusが指定状態の行数を返すことを検証する */
    void countWithStatus()
    {
        PackageListModel model;
        model.applyPackages(toList({pkg(QStringLiteral("a"), 0),
                                    pkg(QStringLiteral("b"), 3),
                                    pkg(QStringLiteral("c"), 3),
                                    pkg(QStringLiteral("d"), 6),
                                    pkg(QStringLiteral("e"), 0)}));

        QCOMPARE(model.countWithStatus(0), 2);
        QCOMPARE(model.countWithStatus(3), 2);
        QCOMPARE(model.countWithStatus(6), 1);
        QCOMPARE(model.countWithStatus(1), 0);
    }

    /** @brief 旧形式のマップにupdatableキーが無い場合、行とロールがfalseになることを検証する */
    void updatableDefaultsToFalseWhenKeyMissing()
    {
        // 準備 : 旧版のバックエンドが返すupdatableキーを持たないマップ
        QVariantMap legacy;
        legacy.insert(QStringLiteral("name"), QStringLiteral("a"));
        legacy.insert(QStringLiteral("arch"), QStringLiteral("x86_64"));
        legacy.insert(QStringLiteral("status"), 3);

        // 確認 : fromVariantMapはキー無しをfalseとして扱う
        const PackageRow row = PackageRow::fromVariantMap(legacy);
        QVERIFY(!row.updatable);

        // 確認 : モデルもupdatableロールとしてfalseを返す
        PackageListModel model;
        model.applyPackages(PackageRows{row});
        QCOMPARE(model.roleNames().value(PackageListModel::UpdatableRole), QByteArray("updatable"));
        QVERIFY(!model.data(model.index(0, 0), PackageListModel::UpdatableRole).toBool());
    }

    /** @brief 大文字小文字の混在した名前が従来と同じ小文字比較の順で並ぶことを検証する */
    void mixedCaseNamesSortLikeLowercase()
    {
        PackageListModel model;
        model.applyPackages(toList({pkg(QStringLiteral("Charlie"), 3),
                                    pkg(QStringLiteral("alpha"), 3),
                                    pkg(QStringLiteral("Bravo"), 3)}));

        PackageFilterProxyModel proxy;
        proxy.setSourceModel(&model);
        proxy.setSortColumn(PackageFilterProxyModel::NameColumn);
        proxy.setSortAscending(true);

        // 確認 : 小文字へ揃えた比較と同じ順序になる
        QCOMPARE(proxy.visibleNames(),
                 QStringList({QStringLiteral("alpha"), QStringLiteral("Bravo"),
                              QStringLiteral("Charlie")}));
    }

    /** @brief rowForKeyが挿入・削除・リセット後も現在の行番号を返すことを検証する */
    void rowForKeyFollowsStructuralChanges()
    {
        PackageListModel model;
        model.applyPackages(toList({pkg(QStringLiteral("a"), 3),
                                    pkg(QStringLiteral("c"), 3)}));
        QCOMPARE(model.rowForKey(keyForName(QStringLiteral("a"))), 0);
        QCOMPARE(model.rowForKey(keyForName(QStringLiteral("c"))), 1);

        // 中間への挿入
        model.applyPackages(toList({pkg(QStringLiteral("a"), 3),
                                    pkg(QStringLiteral("b"), 3),
                                    pkg(QStringLiteral("c"), 3)}));
        QCOMPARE(model.rowForKey(keyForName(QStringLiteral("b"))), 1);
        QCOMPARE(model.rowForKey(keyForName(QStringLiteral("c"))), 2);

        // 先頭の削除
        model.applyPackages(toList({pkg(QStringLiteral("b"), 3),
                                    pkg(QStringLiteral("c"), 3)}));
        QCOMPARE(model.rowForKey(keyForName(QStringLiteral("a"))), -1);
        QCOMPARE(model.rowForKey(keyForName(QStringLiteral("b"))), 0);
        QCOMPARE(model.rowForKey(keyForName(QStringLiteral("c"))), 1);

        // 並び順の変更 (リセット)
        model.applyPackages(toList({pkg(QStringLiteral("c"), 3),
                                    pkg(QStringLiteral("b"), 3)}));
        QCOMPARE(model.rowForKey(keyForName(QStringLiteral("c"))), 0);
        QCOMPARE(model.rowForKey(keyForName(QStringLiteral("b"))), 1);

        // 不明キーと空モデル
        QCOMPARE(model.rowForKey(QStringLiteral("missing")), -1);
        PackageListModel empty;
        QCOMPARE(empty.rowForKey(keyForName(QStringLiteral("a"))), -1);
    }

    /** @brief selectedVisibleCountが選択・フィルタ・行削除の後に正しくなることを検証する */
    void selectedVisibleCountFollowsChanges()
    {
        PackageListModel model;
        model.applyPackages(toList({pkg(QStringLiteral("a"), 3),
                                    pkg(QStringLiteral("b"), 3),
                                    pkg(QStringLiteral("c"), 6)}));

        PackageFilterProxyModel proxy;
        proxy.setSourceModel(&model);
        QCOMPARE(proxy.selectedVisibleCount(), 0);

        // 選択変更 : 2件を選択する
        proxy.toggleSelected(0);
        proxy.toggleSelected(1);
        QCOMPARE(proxy.selectedVisibleCount(), 2);

        // フィルタ変更 : 状態6の行だけを除外しても表示中の選択件数は変わらない
        proxy.setStatusMask(1 << 3);
        QCOMPARE(proxy.selectedVisibleCount(), 2);

        // フィルタ変更 : 状態6の行だけを表示し、選択済みの行を隠す
        proxy.setStatusMask(1 << 6);
        QCOMPARE(proxy.selectedVisibleCount(), 0);

        // 行削除 : 表示へ戻した後、選択済みの行を一覧から消す
        proxy.setStatusMask(0x3FF);
        QCOMPARE(proxy.selectedVisibleCount(), 2);
        model.applyPackages(toList({pkg(QStringLiteral("a"), 3),
                                    pkg(QStringLiteral("c"), 6)}));
        QCOMPARE(proxy.selectedVisibleCount(), 1);
    }

    /** @brief indexOfNamePrefixが大文字の入力でも同じ行を返すことを検証する */
    void indexOfNamePrefixIsCaseInsensitive()
    {
        PackageListModel model;
        model.applyPackages(toList({pkg(QStringLiteral("alpha"), 3),
                                    pkg(QStringLiteral("Bravo"), 3),
                                    pkg(QStringLiteral("charlie"), 3)}));

        PackageFilterProxyModel proxy;
        proxy.setSourceModel(&model);

        const int lowercase = proxy.indexOfNamePrefix(QStringLiteral("bra"));
        QCOMPARE(lowercase, 1);
        QCOMPARE(proxy.indexOfNamePrefix(QStringLiteral("BRA")), lowercase);
        QCOMPARE(proxy.indexOfNamePrefix(QStringLiteral("Zzz")), -1);
    }

    /**
     * @brief 構造変更シグナルの送出時点でrowForKeyが現在の行番号を返すことを検証する
     *
     * rowsInserted/rowsRemoved/modelResetのスロットはend*()の内部で同期的に呼ばれるため、
     * その時点の索引がm_entriesと一致していなければならない
     */
    void rowIndexIsCurrentInsideStructuralSignals()
    {
        // rowsInserted : [a,b] の先頭へ x を挿入し、シグナル送出中の索引を確認する
        {
            PackageListModel model;
            int rowOfB = -2;
            int rowOfX = -2;
            // rowsInsertedの同期通知中に挿入後の索引を記録する
            const QMetaObject::Connection connection = connect(
                &model, &QAbstractItemModel::rowsInserted, &model,
                [&model, &rowOfB, &rowOfX]() {
                    rowOfB = model.rowForKey(keyForName(QStringLiteral("b")));
                    rowOfX = model.rowForKey(keyForName(QStringLiteral("x")));
                },
                Qt::DirectConnection);
            QVERIFY(connection);

            model.applyPackages(toList({pkg(QStringLiteral("a"), 3),
                                        pkg(QStringLiteral("b"), 3)}));
            model.applyPackages(toList({pkg(QStringLiteral("x"), 3),
                                        pkg(QStringLiteral("a"), 3),
                                        pkg(QStringLiteral("b"), 3)}));
            disconnect(connection);

            QCOMPARE(rowOfB, 2);
            QCOMPARE(rowOfX, 0);
        }

        // rowsRemoved : [a,b] から a を削除し、シグナル送出中の索引を確認する
        {
            PackageListModel model;
            int rowOfB = -2;
            int rowOfA = -2;
            // rowsRemovedの同期通知中に削除後の索引を記録する
            const QMetaObject::Connection connection = connect(
                &model, &QAbstractItemModel::rowsRemoved, &model,
                [&model, &rowOfB, &rowOfA]() {
                    rowOfB = model.rowForKey(keyForName(QStringLiteral("b")));
                    rowOfA = model.rowForKey(keyForName(QStringLiteral("a")));
                },
                Qt::DirectConnection);
            QVERIFY(connection);

            model.applyPackages(toList({pkg(QStringLiteral("a"), 3),
                                        pkg(QStringLiteral("b"), 3)}));
            model.applyPackages(toList({pkg(QStringLiteral("b"), 3)}));
            disconnect(connection);

            QCOMPARE(rowOfB, 0);
            QCOMPARE(rowOfA, -1);
        }

        // modelReset : kMaxIncrementalRowChanges (64) を超える入れ替えでリセット経路を通る
        {
            PackageListModel model;
            PackageRows before;
            PackageRows after;
            for (int i = 0; i < 100; ++i) {
                before.append(pkg(QStringLiteral("old%1").arg(i), 3));
                after.append(pkg(QStringLiteral("new%1").arg(i), 3));
            }

            int rowOfNew = -2;
            int rowOfOld = -2;
            // modelResetの同期通知中に置き換え後の索引を記録する
            const QMetaObject::Connection connection = connect(
                &model, &QAbstractItemModel::modelReset, &model,
                [&model, &rowOfNew, &rowOfOld]() {
                    rowOfNew = model.rowForKey(keyForName(QStringLiteral("new0")));
                    rowOfOld = model.rowForKey(keyForName(QStringLiteral("old0")));
                },
                Qt::DirectConnection);
            QVERIFY(connection);

            model.applyPackages(before);
            model.applyPackages(after);
            disconnect(connection);

            QCOMPARE(rowOfNew, 0);
            QCOMPARE(rowOfOld, -1);
        }
    }

    /**
     * @brief 状態だけが変わった行の置き換えでdataChangedが1回だけ送られることを検証する
     */
    void updateRowsStatusChangeEmitsDataChanged()
    {
        PackageListModel model;
        model.applyPackages(toList({pkg(QStringLiteral("a"), 0),
                                    pkg(QStringLiteral("b"), 0)}));

        QSignalSpy changed(&model, &QAbstractItemModel::dataChanged);
        QSignalSpy reset(&model, &QAbstractItemModel::modelReset);

        QVERIFY(model.updateRows(toList({pkg(QStringLiteral("a"), 3)}))
                == RowUpdateResult::Applied);

        QCOMPARE(changed.count(), 1);
        QCOMPARE(changed.first().at(0).toModelIndex().row(), 0);
        QCOMPARE(changed.first().at(1).toModelIndex().row(), 0);
        QCOMPARE(reset.count(), 0);
        QCOMPARE(model.rowCount(), 2);
        QCOMPARE(model.data(model.index(0, 0), PackageListModel::StatusRole).toInt(), 3);
    }

    /**
     * @brief 変化した行が複数ある場合に、行ごとに1回のdataChangedを送ることを検証する
     */
    void updateRowsNotifiesEachChangedRow()
    {
        PackageListModel model;
        model.applyPackages(toList({pkg(QStringLiteral("a"), 0),
                                    pkg(QStringLiteral("b"), 0)}));

        QSignalSpy changed(&model, &QAbstractItemModel::dataChanged);

        QVERIFY(model.updateRows(toList({pkg(QStringLiteral("a"), 3),
                                         pkg(QStringLiteral("b"), 6)}))
                == RowUpdateResult::Applied);

        QCOMPARE(changed.count(), 2);
        QCOMPARE(changed.at(0).at(0).toModelIndex().row(), 0);
        QCOMPARE(changed.at(1).at(0).toModelIndex().row(), 1);
    }

    /**
     * @brief モデルに無い名前の行を無視し、行数を変えないことを検証する
     */
    void updateRowsIgnoresUnknownNames()
    {
        PackageListModel model;
        model.applyPackages(toList({pkg(QStringLiteral("a"), 3)}));

        QSignalSpy changed(&model, &QAbstractItemModel::dataChanged);
        QSignalSpy removed(&model, &QAbstractItemModel::rowsRemoved);

        QVERIFY(model.updateRows(toList({pkg(QStringLiteral("unknown"), 0)}))
                == RowUpdateResult::Applied);
        QCOMPARE(changed.count(), 0);
        QCOMPARE(model.rowCount(), 1);

        // 未知の名前と既知の名前が混在する場合は、既知の行だけを置き換える
        QVERIFY(model.updateRows(toList({pkg(QStringLiteral("a"), 0),
                                         pkg(QStringLiteral("unknown"), 0)}))
                == RowUpdateResult::Applied);
        QCOMPARE(changed.count(), 1);
        QCOMPARE(removed.count(), 0);
        QCOMPARE(model.rowCount(), 1);
        QCOMPARE(model.data(model.index(0, 0), PackageListModel::StatusRole).toInt(), 0);
    }

    /**
     * @brief 内容が同一の行ではdataChangedを送らないことを検証する
     */
    void updateRowsIgnoresIdenticalRows()
    {
        PackageListModel model;
        model.applyPackages(toList({pkg(QStringLiteral("a"), 3),
                                    pkg(QStringLiteral("b"), 3)}));

        QSignalSpy changed(&model, &QAbstractItemModel::dataChanged);

        QVERIFY(model.updateRows(toList({pkg(QStringLiteral("a"), 3),
                                         pkg(QStringLiteral("b"), 3)}))
                == RowUpdateResult::Applied);

        QCOMPARE(changed.count(), 0);
        QCOMPARE(model.rowCount(), 2);
    }

    /**
     * @brief 置き換え後も選択件数と状態別行数が現在の行と一致することを検証する
     */
    void updateRowsKeepsSelectionCounts()
    {
        PackageListModel model;
        model.applyPackages(toList({pkg(QStringLiteral("a"), 3),
                                    pkg(QStringLiteral("b"), 3)}));

        PackageFilterProxyModel proxy;
        proxy.setSourceModel(&model);

        model.setRowSelected(0, true);
        QCOMPARE(proxy.selectedVisibleCount(), 1);
        QCOMPARE(model.countWithStatus(3), 2);
        QCOMPARE(model.countWithStatus(0), 0);

        QVERIFY(model.updateRows(toList({pkg(QStringLiteral("a"), 0)}))
                == RowUpdateResult::Applied);

        QCOMPARE(model.countWithStatus(3), 1);
        QCOMPARE(model.countWithStatus(0), 1);
        // 選択行は表示されたままなので表示中の選択件数は変わらない
        QCOMPARE(proxy.selectedVisibleCount(), 1);
        QCOMPARE(model.selectedCount(), 1);
    }

    /**
     * @brief 同じ名前でアーキテクチャが変わった場合はNeedsFullReloadで何も変更しないことを検証する
     */
    void updateRowsRequiresFullReloadOnArchChange()
    {
        PackageListModel model;
        model.applyPackages(toList({pkg(QStringLiteral("a"), 3)}));

        QSignalSpy changed(&model, &QAbstractItemModel::dataChanged);
        QSignalSpy reset(&model, &QAbstractItemModel::modelReset);

        QVERIFY(model.updateRows(toList({pkg(QStringLiteral("a"), 3,
                                             QStringLiteral("noarch"))}))
                == RowUpdateResult::NeedsFullReload);

        QCOMPARE(changed.count(), 0);
        QCOMPARE(reset.count(), 0);
        QCOMPARE(model.rowCount(), 1);
        QCOMPARE(model.data(model.index(0, 0), PackageListModel::ArchRole).toString(),
                 QStringLiteral("x86_64"));
    }

    /**
     * @brief モデル内で名前が重複している場合はNeedsFullReloadで何も変更しないことを検証する
     */
    void updateRowsRequiresFullReloadForDuplicateNames()
    {
        PackageListModel model;
        model.applyPackages(toList({pkg(QStringLiteral("a"), 3,
                                        QStringLiteral("x86_64")),
                                    pkg(QStringLiteral("a"), 3,
                                        QStringLiteral("noarch"))}));
        QCOMPARE(model.rowCount(), 2);

        QSignalSpy changed(&model, &QAbstractItemModel::dataChanged);

        // 安定キーが一致する行があっても、名前が重複しているため特定できない
        QVERIFY(model.updateRows(toList({pkg(QStringLiteral("a"), 0,
                                             QStringLiteral("x86_64"))}))
                == RowUpdateResult::NeedsFullReload);
        QCOMPARE(changed.count(), 0);
        QCOMPARE(model.rowCount(), 2);
        QCOMPARE(model.data(model.index(0, 0), PackageListModel::StatusRole).toInt(), 3);

        // 一覧を取り直して名前の重複が解消すれば、再び置き換えできる
        model.applyPackages(toList({pkg(QStringLiteral("a"), 3)}));
        QVERIFY(model.updateRows(toList({pkg(QStringLiteral("a"), 0)}))
                == RowUpdateResult::Applied);
        QCOMPARE(model.data(model.index(0, 0), PackageListModel::StatusRole).toInt(), 0);
    }

    /**
     * @brief バージョン・リポジトリ・インストール済み版・更新可否だけの変化を置き換えることを検証する
     */
    void updateRowsReplacesAttributeChanges()
    {
        PackageListModel model;
        model.applyPackages(toList({pkg(QStringLiteral("a"), 3)}));

        QSignalSpy changed(&model, &QAbstractItemModel::dataChanged);
        QSignalSpy inserted(&model, &QAbstractItemModel::rowsInserted);
        QSignalSpy removed(&model, &QAbstractItemModel::rowsRemoved);
        int expectedChanges = 0;

        PackageRow row = pkg(QStringLiteral("a"), 3);
        row.version = QStringLiteral("2.0");
        QVERIFY(model.updateRows(toList({row})) == RowUpdateResult::Applied);
        QCOMPARE(changed.count(), ++expectedChanges);
        QCOMPARE(model.data(model.index(0, 0), PackageListModel::VersionRole).toString(),
                 QStringLiteral("2.0"));

        row.repoAlias = QStringLiteral("update");
        QVERIFY(model.updateRows(toList({row})) == RowUpdateResult::Applied);
        QCOMPARE(changed.count(), ++expectedChanges);
        QCOMPARE(model.data(model.index(0, 0), PackageListModel::RepoAliasRole).toString(),
                 QStringLiteral("update"));

        row.installedVersion = QStringLiteral("1.0");
        QVERIFY(model.updateRows(toList({row})) == RowUpdateResult::Applied);
        QCOMPARE(changed.count(), ++expectedChanges);
        QCOMPARE(model.data(model.index(0, 0),
                            PackageListModel::InstalledVersionRole).toString(),
                 QStringLiteral("1.0"));

        row.updatable = true;
        QVERIFY(model.updateRows(toList({row})) == RowUpdateResult::Applied);
        QCOMPARE(changed.count(), ++expectedChanges);
        QCOMPARE(model.data(model.index(0, 0), PackageListModel::UpdatableRole).toBool(), true);

        QCOMPARE(inserted.count(), 0);
        QCOMPARE(removed.count(), 0);
        QCOMPARE(model.rowCount(), 1);
    }

    /**
     * @brief 状態フィルタ付きプロキシで、置き換えた行が表示から外れる/戻ることを検証する
     */
    void updateRowsFollowsStatusFilteredProxy()
    {
        PackageListModel model;
        model.applyPackages(toList({pkg(QStringLiteral("a"), 3),
                                    pkg(QStringLiteral("b"), 3)}));

        PackageFilterProxyModel proxy;
        proxy.setSourceModel(&model);
        proxy.setStatusMask(1 << 3);
        QCOMPARE(proxy.rowCount(), 2);

        model.setRowSelected(0, true);
        QCOMPARE(proxy.selectedVisibleCount(), 1);

        // 状態3 -> 0 : フィルタから外れる
        QSignalSpy removed(&proxy, &QAbstractItemModel::rowsRemoved);
        QVERIFY(model.updateRows(toList({pkg(QStringLiteral("a"), 0)}))
                == RowUpdateResult::Applied);
        QCOMPARE(removed.count(), 1);
        QCOMPARE(proxy.rowCount(), 1);
        QCOMPARE(proxy.selectedVisibleCount(), 0);

        // 状態0 -> 3 : フィルタへ戻る (選択は維持されている)
        QSignalSpy inserted(&proxy, &QAbstractItemModel::rowsInserted);
        QVERIFY(model.updateRows(toList({pkg(QStringLiteral("a"), 3)}))
                == RowUpdateResult::Applied);
        QCOMPARE(inserted.count(), 1);
        QCOMPARE(proxy.rowCount(), 2);
        QCOMPARE(proxy.selectedVisibleCount(), 1);
    }
};

QTEST_GUILESS_MAIN(PackageListModelTest)

#include "tst_PackageListModel.moc"
