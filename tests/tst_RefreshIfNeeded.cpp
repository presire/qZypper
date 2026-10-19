#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QStringList>
#include <QTemporaryDir>
#include <QtTest>
#include "ZyppManager.h"

using namespace qZypper;

namespace {

/**
 * @brief ローカルrpm-mdリポジトリを生成する
 * @param dir リポジトリのルートディレクトリ
 * @param packageName 収録するパッケージ名
 * @return 生成に成功した場合はtrue
 */
bool writeRpmMdRepo(const QString &dir, const QString &packageName)
{
    QDir().mkpath(dir + QStringLiteral("/repodata"));

    const QString checksum = QString(60, QLatin1Char('0')) + QStringLiteral("0001");
    const QString primary = QStringLiteral(
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        "<metadata xmlns=\"http://linux.duke.edu/metadata/common\""
        " xmlns:rpm=\"http://linux.duke.edu/metadata/rpm\" packages=\"1\">\n"
        "<package type=\"rpm\"><name>%1</name><arch>noarch</arch>\n"
        " <version epoch=\"0\" ver=\"1.0\" rel=\"1\"/>\n"
        " <checksum type=\"sha256\" pkgid=\"YES\">%2</checksum>\n"
        " <summary>Qz Refresh Test</summary><description>qz test</description>\n"
        " <size package=\"1\" installed=\"1\" archive=\"1\"/>\n"
        " <location href=\"%1.rpm\"/>\n"
        " <format><rpm:vendor>openSUSE</rpm:vendor></format></package>\n"
        "</metadata>\n").arg(packageName, checksum);

    const QString repomd = QStringLiteral(
        "<?xml version=\"1.0\"?>\n"
        "<repomd xmlns=\"http://linux.duke.edu/metadata/repo\">\n"
        " <data type=\"primary\"><location href=\"repodata/primary.xml\"/></data>\n"
        "</repomd>\n");

    QFile primaryFile(dir + QStringLiteral("/repodata/primary.xml"));
    if (!primaryFile.open(QIODevice::WriteOnly) || primaryFile.write(primary.toUtf8()) != primary.toUtf8().size())
        return false;

    QFile repomdFile(dir + QStringLiteral("/repodata/repomd.xml"));
    if (!repomdFile.open(QIODevice::WriteOnly) || repomdFile.write(repomd.toUtf8()) != repomd.toUtf8().size())
        return false;

    return true;
}

/**
 * @brief .repo設定を書き出す
 * @param path 出力先
 * @param alias リポジトリエイリアス
 * @param url リポジトリURL
 * @param autoRefresh 自動更新の有効状態
 * @return 書き出しに成功した場合はtrue
 */
bool writeRepoFile(const QString &path, const QString &alias, const QString &url, bool autoRefresh)
{
    const QString content = QStringLiteral(
        "[%1]\nname=%1\nenabled=1\nautorefresh=%2\nbaseurl=%3\ntype=rpm-md\n"
        "gpgcheck=0\nrepo_gpgcheck=0\npkg_gpgcheck=0\n")
        .arg(alias, autoRefresh ? QStringLiteral("1") : QStringLiteral("0"), url);

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        return false;
    return file.write(content.toUtf8()) == content.toUtf8().size();
}

/**
 * @brief ソースのrepomd.xmlへマーカーを追加してリモート側だけを更新する
 * @param dir リポジトリのルートディレクトリ
 * @param marker 追加するXMLコメント
 * @return 書き換えに成功した場合はtrue
 */
bool addSourceMarker(const QString &dir, const QByteArray &marker)
{
    const QString path = dir + QStringLiteral("/repodata/repomd.xml");
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return false;
    QByteArray content = file.readAll();
    file.close();

    content.replace("</repomd>", marker + "\n</repomd>");

    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;
    if (file.write(content) != content.size())
        return false;
    return true;
}

/**
 * @brief ファイルの更新時刻を指定したepoch秒へ戻す
 * @param path 対象ファイル
 * @param epochSeconds 設定する更新時刻
 * @return 設定に成功した場合はtrue
 */
bool backdateFile(const QString &path, qint64 epochSeconds)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return false;
    const bool changed = file.setFileTime(QDateTime::fromSecsSinceEpoch(epochSeconds),
                                          QFileDevice::FileModificationTime);
    file.close();
    return changed;
}

/**
 * @brief ファイル内容を読み出す
 * @param path 対象ファイル
 * @return ファイル内容 (読み出し失敗時は空)
 */
QByteArray readFile(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return file.readAll();
}

/**
 * @brief ファイルの更新時刻 (ミリ秒) を返す
 * @param path 対象ファイル
 * @return 更新時刻 (存在しない場合は-1)
 */
qint64 mtimeOf(const QString &path)
{
    const QFileInfo info(path);
    if (!info.exists())
        return -1;
    return info.lastModified().toMSecsSinceEpoch();
}

} // namespace

/**
 * @class RefreshIfNeededTest
 * @brief refreshReposIfNeededの対象選択と鮮度判定を一時ルートで検証する
 *
 * repo.refresh.delayはZConfigの初回生成時に一度だけ読まれるため、
 * ZYPP_CONFをこの実行ファイルの最初のzypp呼び出しより前に設定する。
 * 期限切れ判定を再取得の有無から区別するため、自動更新有効のリポジトリは
 * ループバックHTTPで配信し、repo.refresh.delayを10分に設定する。
 */
class RefreshIfNeededTest : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();
    void refreshIfNeededPolicy();

private:
    bool startHttpServer(const QString &docRoot);
    void stopHttpServer();

    QTemporaryDir m_root;
    QProcess m_http;
    QString m_baseUrl;
};

/** @brief 自動更新の有効/無効が異なるリポジトリを持つ一時ルートで初期化する */
void RefreshIfNeededTest::initTestCase()
{
    qputenv("ZYPP_READONLY_HACK", "1");
    // ループバックへの取得が環境のプロキシ設定に横取りされないようにする
    qunsetenv("http_proxy");
    qunsetenv("HTTP_PROXY");
    qunsetenv("https_proxy");
    qunsetenv("HTTPS_PROXY");
    qputenv("no_proxy", "127.0.0.1,localhost");
    qputenv("NO_PROXY", "127.0.0.1,localhost");

    QVERIFY(m_root.isValid());

    const QString root = m_root.path();
    QVERIFY(QDir().mkpath(root + QStringLiteral("/etc/zypp/repos.d")));
    QVERIFY(writeRpmMdRepo(root + QStringLiteral("/repo-on"), QStringLiteral("qz-on")));
    QVERIFY(writeRpmMdRepo(root + QStringLiteral("/repo-off"), QStringLiteral("qz-off")));

    QFile conf(root + QStringLiteral("/etc/zypp/zypp.conf"));
    QVERIFY(conf.open(QIODevice::WriteOnly));
    const QByteArray confContent("repo.refresh.delay = 10\n");
    QCOMPARE(conf.write(confContent), qint64(confContent.size()));
    conf.close();
    qputenv("ZYPP_CONF", conf.fileName().toUtf8());

    // 準備 : 自動更新有効のリポジトリをループバックHTTPで配信する
    QVERIFY2(startHttpServer(root + QStringLiteral("/repo-on")), qPrintable(m_http.readAll()));

    QVERIFY(writeRepoFile(root + QStringLiteral("/etc/zypp/repos.d/on.repo"),
                          QStringLiteral("on"),
                          m_baseUrl,
                          true));
    QVERIFY(writeRepoFile(root + QStringLiteral("/etc/zypp/repos.d/off.repo"),
                          QStringLiteral("off"),
                          QStringLiteral("dir:") + root + QStringLiteral("/repo-off"),
                          false));

    auto &manager = ZyppManager::instance();
    QVERIFY2(manager.initialize(root.toStdString(), true), manager.lastError().c_str());
}

/**
 * @brief ループバックHTTPサーバを起動し、待ち受けポートからベースURLを組み立てる
 * @param docRoot 配信するリポジトリのルートディレクトリ
 * @return 起動とポート取得に成功した場合はtrue
 */
bool RefreshIfNeededTest::startHttpServer(const QString &docRoot)
{
    const QString portFile = m_root.path() + QStringLiteral("/http.port");
    QFile::remove(portFile);

    const QString script = QStringLiteral(R"(
import http.server
import os
import socketserver
import sys

root, port_file = sys.argv[1], sys.argv[2]

class Handler(http.server.SimpleHTTPRequestHandler):
    def __init__(self, *args, **kwargs):
        super().__init__(*args, directory=root, **kwargs)
    def log_message(self, *args):
        pass

server = socketserver.ThreadingTCPServer(("127.0.0.1", 0), Handler)
server.daemon_threads = True
tmp = port_file + ".tmp"
with open(tmp, "w") as f:
    f.write(str(server.server_address[1]))
os.replace(tmp, port_file)
server.serve_forever()
)");

    m_http.setProcessChannelMode(QProcess::MergedChannels);
    m_http.start(QStringLiteral("python3"),
                 QStringList{QStringLiteral("-u"), QStringLiteral("-c"), script, docRoot, portFile});
    if (!m_http.waitForStarted(5000))
        return false;

    QElapsedTimer timer;
    timer.start();
    while (!QFileInfo::exists(portFile)) {
        if (m_http.state() != QProcess::Running || timer.elapsed() > 5000)
            return false;
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    }

    QFile file(portFile);
    if (!file.open(QIODevice::ReadOnly))
        return false;
    bool ok = false;
    const uint port = file.readAll().trimmed().toUInt(&ok);
    file.close();
    if (!ok || port == 0)
        return false;

    m_baseUrl = QStringLiteral("http://127.0.0.1:%1/").arg(port);
    return true;
}

/** @brief ループバックHTTPサーバを停止する */
void RefreshIfNeededTest::stopHttpServer()
{
    if (m_http.state() == QProcess::NotRunning)
        return;
    m_http.terminate();
    if (!m_http.waitForFinished(3000))
        m_http.kill();
}

/**
 * @brief 自動更新無効の除外、鮮度内の再取得抑止、期限切れの再取得を検証する
 */
void RefreshIfNeededTest::refreshIfNeededPolicy()
{
    auto &manager = ZyppManager::instance();
    const QString root = m_root.path();
    const QString onRaw = root + QStringLiteral("/var/cache/zypp/raw/on/repodata/repomd.xml");
    const QString onSolv = root + QStringLiteral("/var/cache/zypp/solv/on/solv");
    const QString onSolvCookie = root + QStringLiteral("/var/cache/zypp/solv/on/cookie");
    const QString offRaw = root + QStringLiteral("/var/cache/zypp/raw/off/repodata/repomd.xml");
    const QString offSolv = root + QStringLiteral("/var/cache/zypp/solv/off/solv");
    const QByteArray freshMarker("<!-- marker-fresh -->");
    const QByteArray forcedMarker("<!-- marker-forced -->");
    const QByteArray offMarker("<!-- marker-off -->");
    const auto refreshIfNeeded = [&manager]() {
        QStringList seen;
        const bool ok = manager.refreshReposIfNeeded([&seen](const std::string &alias, int) {
            if (!alias.empty())
                seen.append(QString::fromStdString(alias));
        });
        return std::make_pair(ok, seen);
    };

    // 準備 : 初回の強制リフレッシュで両リポジトリのキャッシュを作る
    QVERIFY2(manager.refreshRepos(), manager.lastError().c_str());
    const QByteArray initialOnRaw = readFile(onRaw);
    QVERIFY(!initialOnRaw.isEmpty());
    QVERIFY(!initialOnRaw.contains(freshMarker));
    QVERIFY(QFileInfo::exists(onSolvCookie));
    const qint64 freshSolv = mtimeOf(onSolv);

    // 準備 : 自動更新無効のリポジトリはソースだけを更新する
    QVERIFY(addSourceMarker(root + QStringLiteral("/repo-off"), offMarker));
    const QByteArray offRawBefore = readFile(offRaw);
    const qint64 offSolvBefore = mtimeOf(offSolv);

    // 準備 : 自動更新有効のリポジトリもソースだけを更新する (キャッシュは直後で鮮度内)
    QVERIFY(addSourceMarker(root + QStringLiteral("/repo-on"), freshMarker));

    // 実行 : 必要時リフレッシュを行う
    const auto fresh = refreshIfNeeded();
    // 確認 : 自動更新有効のリポジトリだけが対象になり、鮮度内なので取得されない
    QVERIFY2(fresh.first, manager.lastError().c_str());
    QVERIFY(fresh.second.contains(QStringLiteral("on")));
    QVERIFY(!fresh.second.contains(QStringLiteral("off")));
    QVERIFY(!readFile(onRaw).contains(freshMarker));
    QCOMPARE(readFile(onRaw), initialOnRaw);
    QCOMPARE(mtimeOf(onSolv), freshSolv);
    // 確認 : 自動更新無効のリポジトリは進捗にも現れず、取得もされない
    QCOMPARE(readFile(offRaw), offRawBefore);
    QCOMPARE(mtimeOf(offSolv), offSolvBefore);

    // 準備 : libzyppが鮮度判定に使うキャッシュrepomdの更新時刻を10分より古くする
    const QByteArray solvCookieBefore = readFile(onSolvCookie);
    QVERIFY(backdateFile(onRaw, QDateTime::currentSecsSinceEpoch() - 11 * 60));

    // 実行 : 必要時リフレッシュを行う
    const auto stale = refreshIfNeeded();
    // 確認 : 期限切れのため再取得され、ソース側のマーカーがキャッシュへ取り込まれる
    QVERIFY2(stale.first, manager.lastError().c_str());
    QVERIFY(stale.second.contains(QStringLiteral("on")));
    QVERIFY(readFile(onRaw).contains(freshMarker));
    QVERIFY(readFile(onSolvCookie) != solvCookieBefore);

    // 準備 : キャッシュは直後で鮮度内のまま、ソースへ別のマーカーを追加する
    QVERIFY(addSourceMarker(root + QStringLiteral("/repo-on"), forcedMarker));

    // 実行 : 同じ状態で必要時リフレッシュを行う
    const auto controlSkip = refreshIfNeeded();
    // 確認 : 鮮度内のため、新しいマーカーは取得されない
    QVERIFY2(controlSkip.first, manager.lastError().c_str());
    QVERIFY(!readFile(onRaw).contains(forcedMarker));

    // 実行 : 強制リフレッシュの対照実験を行う
    QVERIFY2(manager.refreshRepos(), manager.lastError().c_str());
    // 確認 : 鮮度内でも強制なら新しいマーカーが取得される
    QVERIFY(readFile(onRaw).contains(freshMarker));
    QVERIFY(readFile(onRaw).contains(forcedMarker));
}

/** @brief zyppの資源を解放する */
void RefreshIfNeededTest::cleanupTestCase()
{
    stopHttpServer();
    ZyppManager::instance().shutdown();
}

QTEST_GUILESS_MAIN(RefreshIfNeededTest)
#include "tst_RefreshIfNeeded.moc"
