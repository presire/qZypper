#include <QtTest>
#include <QSignalSpy>
#include <initializer_list>
#include "PackageListModel.h"
#include "PackageFilterProxyModel.h"

using namespace qZypper;

namespace {

/**
 * @brief テスト用のパッケージ情報マップを生成する
 * @param name パッケージ名
 * @param status 状態を表す整数値
 * @param arch アーキテクチャ (既定はx86_64)
 * @param repo リポジトリ別名 (既定はoss)
 * @param version バージョン (既定は1.0)
 * @return PackageListModelが扱うキーを埋めたマップ
 */
QVariantMap pkg(const QString &name, int status,
                const QString &arch = QStringLiteral("x86_64"),
                const QString &repo = QStringLiteral("oss"),
                const QString &version = QStringLiteral("1.0"))
{
    QVariantMap m;
    m.insert(QStringLiteral("name"), name);
    m.insert(QStringLiteral("arch"), arch);
    m.insert(QStringLiteral("repoAlias"), repo);
    m.insert(QStringLiteral("version"), version);
    m.insert(QStringLiteral("installedVersion"), QString());
    m.insert(QStringLiteral("summary"), QStringLiteral("summary of ") + name);
    m.insert(QStringLiteral("repoName"), repo);
    m.insert(QStringLiteral("size"), QVariant::fromValue<qulonglong>(1024));
    m.insert(QStringLiteral("status"), status);
    return m;
}

/**
 * @brief 可変個のパッケージマップをQVariantListへまとめる
 * @param maps 変換するパッケージマップの一覧
 * @return 入力順を保ったQVariantList
 */
QVariantList toList(std::initializer_list<QVariantMap> maps)
{
    QVariantList out;
    out.reserve(static_cast<int>(maps.size()));
    for (const QVariantMap &m : maps)
        out.append(m);
    return out;
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
        QVariantList before;
        QVariantList after;
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
        const QVariantMap a = pkg(QStringLiteral("x"), 0,
                                  QStringLiteral("x86_64"), QStringLiteral("oss"));
        const QVariantMap b = pkg(QStringLiteral("x"), 0,
                                  QStringLiteral("i586"), QStringLiteral("oss"));
        const QVariantMap c = pkg(QStringLiteral("x"), 0,
                                  QStringLiteral("x86_64"), QStringLiteral("update"));
        QVERIFY(PackageListModel::stableKey(a) != PackageListModel::stableKey(b));
        QCOMPARE(PackageListModel::stableKey(a), PackageListModel::stableKey(c));
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
};

QTEST_GUILESS_MAIN(PackageListModelTest)

#include "tst_PackageListModel.moc"
