#include "LocalRpm.h"

#include <QtTest>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTextStream>

#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

using namespace qZypper;

class LocalRpmTest : public QObject {
    Q_OBJECT
private slots:
    void validatesPathSyntax_data();
    void validatesPathSyntax();
    void checksCallerReadability_data();
    void checksCallerReadability();
    void rejectsSymlink();
    void acceptsRegularRpm();
    void enforcesSizeLimit();
    void rejectsNonRegularFile();
    void copiesToDestination();
    void copyStopsWhenCancelled();
    void adoptsRegularFd();
    void adoptRejectsBadNames_data();
    void adoptRejectsBadNames();
    void adoptRejectsFifoAndWriteOnly();
};

void LocalRpmTest::validatesPathSyntax_data()
{
    QTest::addColumn<QString>("path");
    QTest::addColumn<bool>("valid");
    QTest::newRow("absolute") << QStringLiteral("/tmp/a.rpm") << true;
    QTest::newRow("relative") << QStringLiteral("tmp/a.rpm") << false;
    QTest::newRow("empty") << QString() << false;
    QTest::newRow("dotdot") << QStringLiteral("/tmp/../a.rpm") << false;
    QTest::newRow("dotdot-suffix-ok") << QStringLiteral("/tmp/..a.rpm") << true;
    QTest::newRow("wrong-extension") << QStringLiteral("/tmp/a.txt") << false;
    QTest::newRow("control") << QStringLiteral("/tmp/a\n.rpm") << false;
    QTest::newRow("short-name") << QStringLiteral("/tmp/rpm") << false;
}

void LocalRpmTest::validatesPathSyntax()
{
    QFETCH(QString, path);
    QFETCH(bool, valid);
    std::string err;
    QCOMPARE(validateLocalRpmPath(path.toStdString(), err), valid);
    if (!valid)
        QVERIFY(!err.empty());
}

void LocalRpmTest::checksCallerReadability_data()
{
    QTest::addColumn<mode_t>("mode");
    QTest::addColumn<uid_t>("fileUid");
    QTest::addColumn<uid_t>("callerUid");
    QTest::addColumn<bool>("readable");
    QTest::newRow("world-readable") << mode_t(0644) << uid_t(1000) << uid_t(2000) << true;
    QTest::newRow("owner-match") << mode_t(0600) << uid_t(1000) << uid_t(1000) << true;
    QTest::newRow("foreign-private") << mode_t(0600) << uid_t(1000) << uid_t(2000) << false;
}

void LocalRpmTest::checksCallerReadability()
{
    QFETCH(mode_t, mode);
    QFETCH(uid_t, fileUid);
    QFETCH(uid_t, callerUid);
    QFETCH(bool, readable);
    QCOMPARE(isCallerReadable(mode, fileUid, callerUid), readable);
}

void LocalRpmTest::rejectsSymlink()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString target = dir.filePath(QStringLiteral("real.rpm"));
    QFile file(target);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write("payload");
    file.close();

    const QString link = dir.filePath(QStringLiteral("link.rpm"));
    QVERIFY(QFile::link(target, link));

    LocalRpmFile staged;
    std::string err;
    QVERIFY(!openLocalRpm(link.toStdString(), ::getuid(), kMaxLocalRpmBytes, staged, err));
    QVERIFY(!err.empty());
}

void LocalRpmTest::acceptsRegularRpm()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("pkg.rpm"));
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    const QByteArray payload("rpm-content");
    file.write(payload);
    file.close();

    LocalRpmFile staged;
    std::string err;
    QVERIFY(openLocalRpm(path.toStdString(), ::getuid(), kMaxLocalRpmBytes, staged, err));
    QCOMPARE(staged.size, static_cast<std::uintmax_t>(payload.size()));
    QCOMPARE(staged.filename, std::string("pkg.rpm"));
    QVERIFY(staged.fd >= 0);
}

void LocalRpmTest::enforcesSizeLimit()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("big.rpm"));
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(QByteArray(32, 'x'));
    file.close();

    LocalRpmFile staged;
    std::string err;
    QVERIFY(!openLocalRpm(path.toStdString(), ::getuid(), 8, staged, err));
    QVERIFY(err.find("size") != std::string::npos);
}

void LocalRpmTest::rejectsNonRegularFile()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("d.rpm"));
    QVERIFY(QDir().mkdir(path));

    LocalRpmFile staged;
    std::string err;
    QVERIFY(!openLocalRpm(path.toStdString(), ::getuid(), kMaxLocalRpmBytes, staged, err));
    QVERIFY(err.find("regular") != std::string::npos);
}

void LocalRpmTest::copiesToDestination()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString srcPath = dir.filePath(QStringLiteral("src.rpm"));
    const QByteArray payload(4096 + 7, 'z');
    QFile src(srcPath);
    QVERIFY(src.open(QIODevice::WriteOnly));
    src.write(payload);
    src.close();

    LocalRpmFile staged;
    std::string err;
    QVERIFY(openLocalRpm(srcPath.toStdString(), ::getuid(), kMaxLocalRpmBytes, staged, err));

    const QString destPath = dir.filePath(QStringLiteral("copy.rpm"));
    QVERIFY(copyLocalRpmTo(staged, destPath.toStdString(), kMaxLocalRpmBytes, err));

    QFile copy(destPath);
    QVERIFY(copy.open(QIODevice::ReadOnly));
    QCOMPARE(copy.readAll(), payload);
}

void LocalRpmTest::copyStopsWhenCancelled()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString srcPath = dir.filePath(QStringLiteral("src.rpm"));
    QFile src(srcPath);
    QVERIFY(src.open(QIODevice::WriteOnly));
    src.write(QByteArray(3 * 1024 * 1024, 'c'));
    src.close();

    LocalRpmFile staged;
    std::string err;
    QVERIFY(openLocalRpm(srcPath.toStdString(), ::getuid(), kMaxLocalRpmBytes, staged, err));

    // 1回目の読み込み後に取消しを要求し、残りを読まずに止まることを確認する
    std::atomic<bool> cancel{false};
    int beats = 0;
    const QString destPath = dir.filePath(QStringLiteral("copy.rpm"));
    QVERIFY(!copyLocalRpmTo(staged, destPath.toStdString(), kMaxLocalRpmBytes, err, &cancel,
                            [&] { if (++beats == 2) cancel = true; }));
    QCOMPARE(err, std::string(kLocalRpmCopyCancelled));
    QCOMPARE(beats, 2);
    QVERIFY(QFileInfo(destPath).size() < 3 * 1024 * 1024);

    // 取消し要求前なら最後までコピーし、読み込みごとに heartbeat を呼ぶ
    cancel = false;
    beats = 0;
    const QString fullPath = dir.filePath(QStringLiteral("full.rpm"));
    QVERIFY2(copyLocalRpmTo(staged, fullPath.toStdString(), kMaxLocalRpmBytes, err, &cancel,
                            [&] { ++beats; }), err.c_str());
    QCOMPARE(QFileInfo(fullPath).size(), qint64(3 * 1024 * 1024));
    QVERIFY(beats >= 4);
}

void LocalRpmTest::adoptsRegularFd()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("pkg.rpm"));
    QFile f(path);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write("rpmdata");
    f.close();

    const int fd = ::open(path.toLocal8Bit().constData(), O_RDONLY | O_CLOEXEC);
    QVERIFY(fd >= 0);
    LocalRpmFile out;
    std::string err;
    QVERIFY2(adoptLocalRpmFd(fd, "pkg.rpm", kMaxLocalRpmBytes, out, err), err.c_str());
    ::close(fd);  // 呼出側の fd を閉じても複製 fd で読める
    const QString destPath = dir.filePath(QStringLiteral("copy.rpm"));
    QVERIFY2(copyLocalRpmTo(out, destPath.toStdString(), kMaxLocalRpmBytes, err), err.c_str());
    QFile copy(destPath);
    QVERIFY(copy.open(QIODevice::ReadOnly));
    QCOMPARE(copy.readAll(), QByteArray("rpmdata"));

    // サイズ上限
    const int fd2 = ::open(path.toLocal8Bit().constData(), O_RDONLY | O_CLOEXEC);
    LocalRpmFile small;
    QVERIFY(!adoptLocalRpmFd(fd2, "pkg.rpm", 3, small, err));
    ::close(fd2);
}

void LocalRpmTest::adoptRejectsBadNames_data()
{
    QTest::addColumn<QString>("name");
    QTest::newRow("empty") << QString();
    QTest::newRow("slash") << QStringLiteral("a/b.rpm");
    QTest::newRow("hidden") << QStringLiteral(".a.rpm");
    QTest::newRow("extension") << QStringLiteral("a.txt");
    QTest::newRow("control") << QStringLiteral("a\n.rpm");
}

void LocalRpmTest::adoptRejectsBadNames()
{
    QFETCH(QString, name);
    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("pkg.rpm"));
    QFile f(path);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write("x");
    f.close();
    const int fd = ::open(path.toLocal8Bit().constData(), O_RDONLY | O_CLOEXEC);
    QVERIFY(fd >= 0);
    LocalRpmFile out;
    std::string err;
    QVERIFY(!adoptLocalRpmFd(fd, name.toStdString(), kMaxLocalRpmBytes, out, err));
    ::close(fd);
}

void LocalRpmTest::adoptRejectsFifoAndWriteOnly()
{
    QTemporaryDir dir;
    const QString fifo = dir.filePath(QStringLiteral("pipe.rpm"));
    QCOMPARE(::mkfifo(fifo.toLocal8Bit().constData(), 0600), 0);
    // O_NONBLOCK により書き手不在でもブロックしない (GUI 側と同じ開き方)
    const int fd = ::open(fifo.toLocal8Bit().constData(), O_RDONLY | O_NONBLOCK | O_CLOEXEC);
    QVERIFY(fd >= 0);
    LocalRpmFile out;
    std::string err;
    QVERIFY(!adoptLocalRpmFd(fd, "pipe.rpm", kMaxLocalRpmBytes, out, err));
    ::close(fd);

    const QString path = dir.filePath(QStringLiteral("w.rpm"));
    const int wfd = ::open(path.toLocal8Bit().constData(), O_WRONLY | O_CREAT | O_CLOEXEC, 0600);
    QVERIFY(wfd >= 0);
    QVERIFY(!adoptLocalRpmFd(wfd, "w.rpm", kMaxLocalRpmBytes, out, err));
    ::close(wfd);
}

QTEST_GUILESS_MAIN(LocalRpmTest)
#include "tst_LocalRpm.moc"
