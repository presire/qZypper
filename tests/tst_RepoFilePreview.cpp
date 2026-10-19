#include <QtTest>
#include <QElapsedTimer>
#include <QFile>
#include <QTemporaryDir>
#include <sys/stat.h>
#include "RepoFilePreview.h"

using namespace qZypper;

/**
 * @class RepoFilePreviewTest
 * @brief readRepoFileForPreview()の入力検証とサイズ上限を検証する
 *
 * 名前付きパイプ (FIFO) やディレクトリなど通常ファイル以外の入力を拒否し、上限を超えるファイルを読み切らずに拒否できることを確認する
 */
class RepoFilePreviewTest : public QObject
{
    Q_OBJECT

private slots:
    /** @brief 上限内の通常ファイルを読み取り内容を返すことを検証する */
    void regularFileWithinLimit_readsContent();
    /** @brief ちょうど上限サイズのファイルを受理することを検証する */
    void regularFileAtLimit_accepted();
    /** @brief 上限を超えるファイルを内容なしで拒否することを検証する */
    void fileLargerThanLimit_rejected();
    /** @brief ディレクトリのパスを通常ファイルでないため拒否することを検証する */
    void directory_rejected();
    /** @brief 存在しないパスをエラーとして拒否することを検証する */
    void missingFile_rejected();
    /** @brief 名前付きパイプをブロックせず即座に拒否することを検証する */
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
    // 実行 : 上限内の通常ファイルを読み取る
    const bool ok = readRepoFileForPreview(path, 4096, content, error);

    // 確認 : 内容が返りエラーは報告されない
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
    // 実行 : 上限とちょうどのサイズのファイルを読み取る
    const bool ok = readRepoFileForPreview(path, payload.size(), content, error);

    // 確認 : エラーなく受理される
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
    // 実行 : 上限より大きいファイルを読み取る
    const bool ok = readRepoFileForPreview(path, 64 * 1024, content, error);

    // 確認 : エラーになり内容は返らない
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
    // 実行 : ディレクトリのパスを渡す
    const bool ok = readRepoFileForPreview(dir.path(), 4096, content, error);

    // 確認 : 通常ファイルでないため拒否される
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
    // 実行 : 存在しないパスを渡す
    const bool ok = readRepoFileForPreview(path, 4096, content, error);

    // 確認 : エラーとして拒否される
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
    // 準備 : 書き手のいない名前付きパイプを用意する
    QCOMPARE(::mkfifo(nativePath.constData(), 0600), 0);

    QByteArray content;
    QString error;
    QElapsedTimer timer;
    timer.start();
    // 実行 : 名前付きパイプのパスを渡す
    const bool ok = readRepoFileForPreview(path, 4096, content, error);
    const qint64 elapsed = timer.elapsed();

    // 確認 : 内容を読まずに即座に拒否される (非ブロッキング)
    QVERIFY(!ok);
    QVERIFY(!error.isEmpty());
    QVERIFY(content.isEmpty());
    QVERIFY2(elapsed < 2000, "reading a named pipe must not block on open");
}

QTEST_GUILESS_MAIN(RepoFilePreviewTest)

#include "tst_RepoFilePreview.moc"
