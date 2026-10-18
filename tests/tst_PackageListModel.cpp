#include "PackageListModel.h"
#include "PackageFilterProxyModel.h"

#include <QtTest>
#include <QSignalSpy>

#include <initializer_list>

using namespace qZypper;

namespace {

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
 * @brief PackageListModel の差分更新と PackageFilterProxyModel の
 *        フィルタ/ソートを検証する (root 不要)。
 */
class PackageListModelTest : public QObject
{
    Q_OBJECT

private slots:
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
        QCOMPARE(model.data(model.index(0, 0), PackageListModel::NameRole).toString(),
                 QStringLiteral("a"));
        QCOMPARE(model.data(model.index(1, 0), PackageListModel::NameRole).toString(),
                 QStringLiteral("c"));
    }

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

        // status 3 のみ表示
        proxy.setStatusMask(1 << 3);
        QCOMPARE(proxy.rowCount(), 1);
        QCOMPARE(proxy.index(0, 0).data(PackageListModel::NameRole).toString(),
                 QStringLiteral("a"));
    }

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

        QCOMPARE(proxy.index(0, 0).data(PackageListModel::NameRole).toString(),
                 QStringLiteral("aaa"));
        QCOMPARE(proxy.index(2, 0).data(PackageListModel::NameRole).toString(),
                 QStringLiteral("ccc"));

        proxy.setSortAscending(false);
        QCOMPARE(proxy.index(0, 0).data(PackageListModel::NameRole).toString(),
                 QStringLiteral("ccc"));
        QCOMPARE(proxy.index(2, 0).data(PackageListModel::NameRole).toString(),
                 QStringLiteral("aaa"));
    }

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
