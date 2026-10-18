#include "RebootCheck.h"
#include <QtTest>
#include <QTemporaryDir>
#include <QDir>
#include <QFile>

using namespace qZypper;

namespace {

/**
 * @brief 合成プロセスディレクトリを作成する。
 * @param procRoot /proc 相当のルート
 * @param pid プロセス ID
 * @param maps maps ファイル内容
 * @param cgroup cgroup ファイル内容
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

class RebootCheckTest : public QObject {
    Q_OBJECT
private slots:
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

    void serviceUnitParsesSystemdSlices()
    {
        QCOMPARE(serviceUnitFromCgroup(QByteArray("0::/system.slice/sshd.service")),
                 QStringLiteral("sshd.service"));
        QCOMPARE(serviceUnitFromCgroup(
                     QByteArray("0::/user.slice/user-1000.slice/session-1.scope")),
                 QStringLiteral("session-1.scope"));
        QCOMPARE(serviceUnitFromCgroup(QByteArray("0::/")), QString());
        QCOMPARE(serviceUnitFromCgroup(QByteArray()), QString());
    }

    void serviceUnitSkipsInitScope()
    {
        QCOMPARE(serviceUnitFromCgroup(QByteArray("0::/init.scope")), QString());
    }

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

    void mapsScanRespectsUnitCap()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString procRoot = dir.path();
        for (int i = 0; i < 5; ++i) {
            writeProcess(procRoot, QString::number(500 + i),
                         QByteArray("/lib/x.so (deleted)\n"),
                         QByteArray("0::/system.slice/svc") + QByteArray::number(i)
                             + QByteArray(".service\n"));
        }
        QCOMPARE(servicesUsingDeletedFiles(procRoot, 4000, 2).size(), 2);
    }

    void mapsScanMissingRootIsEmpty()
    {
        QVERIFY(servicesUsingDeletedFiles(QStringLiteral("/nonexistent/proc")).isEmpty());
    }
};

QTEST_APPLESS_MAIN(RebootCheckTest)
#include "tst_RebootCheck.moc"
