#include <QtTest>
#include <QTemporaryDir>
#include <QDir>
#include <QFile>
#include "RebootCheck.h"

using namespace qZypper;

namespace {

/**
 * @brief 合成プロセスディレクトリを作成する
 * @param procRoot /proc相当のルート
 * @param pid プロセスID
 * @param maps mapsファイル内容
 * @param cgroup cgroupファイル内容
 */
void writeProcess(const QString &procRoot, const QString &pid, const QByteArray &maps,
                  const QByteArray &cgroup)
{
    QDir dir(procRoot);
    dir.mkpath(pid);
    QFile mapsFile(dir.filePath(pid + QStringLiteral("/maps")));
    if (mapsFile.open(QIODevice::WriteOnly))
        mapsFile.write(maps);
    QFile cgroupFile(dir.filePath(pid + QStringLiteral("/cgroup")));
    if (cgroupFile.open(QIODevice::WriteOnly))
        cgroupFile.write(cgroup);
}

} // namespace

/**
 * @class RebootCheckTest
 * @brief 再起動要否の判定と削除済みファイルを使うサービスの検出を検証する
 *
 * reboot-neededフラグの有無、cgroupからのサービスユニット名抽出とinit.scopeの除外、
 * /proc相当のmaps走査による削除済みファイル利用サービスの検出と件数上限、
 * ルート不在時の空結果を対象にする
 */
class RebootCheckTest : public QObject {
    Q_OBJECT
private slots:
    /** @brief フラグファイルの有無から再起動の要否を判定することを検証する */
    void rebootFlagPresentChecksExistence()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString flag = dir.filePath(QStringLiteral("reboot-needed"));
        QVERIFY(!rebootFlagPresent(flag));
        QFile file(flag);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.close();
        QVERIFY(rebootFlagPresent(flag));
        QVERIFY(!rebootFlagPresent(QString()));
    }

    /** @brief cgroupパスから末尾のサービスユニット名を取り出すことを検証する */
    void serviceUnitParsesSystemdSlices()
    {
        QCOMPARE(serviceUnitFromCgroup(QByteArray("0::/system.slice/sshd.service")), QStringLiteral("sshd.service"));
        QCOMPARE(serviceUnitFromCgroup(QByteArray("0::/user.slice/user-1000.slice/session-1.scope")), QStringLiteral("session-1.scope"));
        QCOMPARE(serviceUnitFromCgroup(QByteArray("0::/")), QString());
        QCOMPARE(serviceUnitFromCgroup(QByteArray()), QString());
    }

    /** @brief init.scopeをサービスとして扱わないことを検証する */
    void serviceUnitSkipsInitScope()
    {
        QCOMPARE(serviceUnitFromCgroup(QByteArray("0::/init.scope")), QString());
    }

    /** @brief 削除済みファイルを使うサービスだけを重複なく検出することを検証する */
    void mapsScanFindsDeletedFileServices()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString procRoot = dir.path();

        writeProcess(procRoot, QStringLiteral("100"),
                     QByteArray("7f00-7f01 r-xp 00000000 08:01 1 /usr/lib/libfoo.so (deleted)\n"),
                     QByteArray("0::/system.slice/sshd.service\n"));
        writeProcess(procRoot, QStringLiteral("200"),
                     QByteArray("7f00-7f01 r-xp 00000000 08:01 1 /usr/lib/libbar.so\n"),
                     QByteArray("0::/system.slice/cron.service\n"));
        writeProcess(procRoot, QStringLiteral("300"),
                     QByteArray("7f00-7f01 r-xp 00000000 08:01 1 /usr/lib/libbaz.so (deleted)\n"),
                     QByteArray("0::/init.scope\n"));
        writeProcess(procRoot, QStringLiteral("400"),
                     QByteArray("7f00-7f01 r-xp 00000000 08:01 1 /usr/lib/libqux.so (deleted)\n"),
                     QByteArray("0::/system.slice/sshd.service\n"));
        QDir(procRoot).mkpath(QStringLiteral("self"));

        const QStringList units = servicesUsingDeletedFiles(procRoot);
        QCOMPARE(units, QStringList{QStringLiteral("sshd.service")});
    }

    /** @brief 検出するユニット数を上限で打ち切ることを検証する */
    void mapsScanRespectsUnitCap()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString procRoot = dir.path();
        for (int i = 0; i < 5; ++i) {
            writeProcess(procRoot, QString::number(500 + i), QByteArray("/lib/x.so (deleted)\n"),
                         QByteArray("0::/system.slice/svc") + QByteArray::number(i) + QByteArray(".service\n"));
        }
        QCOMPARE(servicesUsingDeletedFiles(procRoot, 4000, 2).size(), 2);
    }

    /** @brief procルートが存在しない場合は空を返すことを検証する */
    void mapsScanMissingRootIsEmpty()
    {
        QVERIFY(servicesUsingDeletedFiles(QStringLiteral("/nonexistent/proc")).isEmpty());
    }
};

QTEST_APPLESS_MAIN(RebootCheckTest)
#include "tst_RebootCheck.moc"
