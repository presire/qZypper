#include "RepoFilePreview.h"

#include <QtTest>
#include <QElapsedTimer>
#include <QFile>
#include <QTemporaryDir>

#include <sys/stat.h>

using namespace qZypper;

/**
 * @brief readRepoFileForPreview() の入力検証とサイズ上限を検証するテスト。
 *
 * 名前付きパイプ (FIFO) やディレクトリなど通常ファイル以外の入力を拒否し、
 * 上限を超えるファイルを読み切らずに拒否できることを確認する。
 */
class RepoFilePreviewTest : public QObject
{
    Q_OBJECT

private slots:
    void regularFileWithinLimit_readsContent();
    void regularFileAtLimit_accepted();
    void fileLargerThanLimit_rejected();
    void directory_rejected();
    void missingFile_rejected();
    void namedPipe_rejectedWithoutBlocking();
};

void RepoFilePreviewTest::regularFileWithinLimit_readsContent()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("repo.repo"));
    const QByteArray payload("hello repo file");
    {
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly));
        QCOMPARE(f.write(payload), static_cast<qint64>(payload.size()));
    }

    QByteArray content;
    QString error;
    // When: 615 regular file within the limit is read
    const bool ok = readRepoFileForPreview(path, 4096, content, error);

    // Then: the content is returned and no error is reported
    QVERIFY(ok);
    QCOMPARE(content, payload);
    QVERIFY(error.isEmpty());
}

void RepoFilePreviewTest::regularFileAtLimit_accepted()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("exact.repo"));
    const QByteArray payload(256, 'x');
    {
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly));
        QCOMPARE(f.write(payload), static_cast<qint64>(payload.size()));
    }

    QByteArray content;
    QString error;
    // When: a file exactly at the size limit is read
    const bool ok = readRepoFileForPreview(path, payload.size(), content, error);

    // Then: it is accepted without error
    QVERIFY(ok);
    QCOMPARE(content.size(), payload.size());
    QVERIFY(error.isEmpty());
}

void RepoFilePreviewTest::fileLargerThanLimit_rejected()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("big.repo"));
    const QByteArray payload(64 * 1024 + 1, 'y');
    {
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly));
        QCOMPARE(f.write(payload), static_cast<qint64>(payload.size()));
    }

    QByteArray content;
    QString error;
    // When: a file larger than the limit is read
    const bool ok = readRepoFileForPreview(path, 64 * 1024, content, error);

    // Then: it is rejected with an error and no content
    QVERIFY(!ok);
    QVERIFY(!error.isEmpty());
    QVERIFY(content.isEmpty());
}

void RepoFilePreviewTest::directory_rejected()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    QByteArray content;
    QString error;
    // When: a directory path is passed
    const bool ok = readRepoFileForPreview(dir.path(), 4096, content, error);

    // Then: it is rejected because it is not a regular file
    QVERIFY(!ok);
    QVERIFY(!error.isEmpty());
    QVERIFY(content.isEmpty());
}

void RepoFilePreviewTest::missingFile_rejected()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("does-not-exist.repo"));

    QByteArray content;
    QString error;
    // When: a non-existent path is passed
    const bool ok = readRepoFileForPreview(path, 4096, content, error);

    // Then: it is rejected with an error
    QVERIFY(!ok);
    QVERIFY(!error.isEmpty());
    QVERIFY(content.isEmpty());
}

void RepoFilePreviewTest::namedPipe_rejectedWithoutBlocking()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("fifo.repo"));
    QByteArray nativePath = QFile::encodeName(path);
    // Given: a named pipe with no writer attached
    QCOMPARE(::mkfifo(nativePath.constData(), 0600), 0);

    QByteArray content;
    QString error;
    QElapsedTimer timer;
    timer.start();
    // When: the named pipe is passed
    const bool ok = readRepoFileForPreview(path, 4096, content, error);
    const qint64 elapsed = timer.elapsed();

    // Then: it is rejected promptly (non-blocking) without reading content
    QVERIFY(!ok);
    QVERIFY(!error.isEmpty());
    QVERIFY(content.isEmpty());
    QVERIFY2(elapsed < 2000, "reading a named pipe must not block on open");
}

QTEST_GUILESS_MAIN(RepoFilePreviewTest)

#include "tst_RepoFilePreview.moc"
