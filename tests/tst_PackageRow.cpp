/**
 * @file tst_PackageRow.cpp
 * @brief PackageRowの変換とPackageListReplyのD-Busマーシャリングを検証する
 *
 * QVariantMap往復、PackageInfoからの変換、ワイヤ形式 (a(ss)a(sssssitib)) の
 * リポジトリ表重複排除、範囲外索引の空文字化、peer-to-peer接続での往復、
 * 遅延応答の署名、イントロスペクションの戻り値型を確認する
 * バックエンドやroot権限、実バスには依存しない
 *
 * 入力 : なし
 * 出力 : QTestの合否
 * 依存 : Qt Test、Qt DBus、PackageRow、PackageRowDBus
 */
#include <QtTest>
#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusContext>
#include <QDBusError>
#include <QDBusMessage>
#include <QDBusMetaType>
#include <QDBusPendingCall>
#include <QDBusPendingCallWatcher>
#include <QDBusServer>
#include <QSignalSpy>
#include <QEventLoop>
#include <QTimer>
#include <QVariantList>
#include <memory>
#include <optional>
#include "PackageListDecode.h"
#include "PackageRow.h"
#include "PackageRowDBus.h"

using namespace qZypper;

namespace testwire {

/**
 * @brief テスト専用のリポジトリ表要素 (alias, name)
 */
struct Repo {
    QString alias;   // リポジトリエイリアス
    QString name;    // リポジトリ名
};

/**
 * @brief テスト専用のワイヤ行 (任意のrepoIndexを送るため)
 */
struct Row {
    QString name;              // パッケージ名
    QString summary;           // 概要
    QString version;           // 候補バージョン
    QString installedVersion;  // インストール済みバージョン
    QString arch;              // アーキテクチャ
    int repoIndex = -1;        // リポジトリ表への索引
    quint64 size = 0;          // インストールサイズ (bytes)
    int status = 0;            // パッケージ状態
    bool updatable = false;    // 更新候補があるか
};

/**
 * @brief 範囲外索引を送るためのテスト専用応答
 */
struct BadReply {
    QVector<Repo> repos;   // リポジトリ表
    QVector<Row> rows;     // 行
};

/** @brief リポジトリ表要素をD-Bus構造体へ変換する */
QDBusArgument &operator<<(QDBusArgument &argument, const Repo &repo)
{
    argument.beginStructure();
    argument << repo.alias << repo.name;
    argument.endStructure();
    return argument;
}

/** @brief リポジトリ表要素をD-Bus構造体から読む */
const QDBusArgument &operator>>(const QDBusArgument &argument, Repo &repo)
{
    argument.beginStructure();
    argument >> repo.alias >> repo.name;
    argument.endStructure();
    return argument;
}

/** @brief ワイヤ行をD-Bus構造体へ変換する */
QDBusArgument &operator<<(QDBusArgument &argument, const Row &row)
{
    argument.beginStructure();
    argument << row.name << row.summary << row.version << row.installedVersion << row.arch
             << row.repoIndex << row.size << row.status << row.updatable;
    argument.endStructure();
    return argument;
}

/** @brief ワイヤ行をD-Bus構造体から読む */
const QDBusArgument &operator>>(const QDBusArgument &argument, Row &row)
{
    argument.beginStructure();
    argument >> row.name >> row.summary >> row.version >> row.installedVersion >> row.arch
             >> row.repoIndex >> row.size >> row.status >> row.updatable;
    argument.endStructure();
    return argument;
}

/** @brief テスト専用応答をD-Bus構造体へ変換する */
QDBusArgument &operator<<(QDBusArgument &argument, const BadReply &reply)
{
    argument.beginStructure();
    argument << reply.repos;
    argument << reply.rows;
    argument.endStructure();
    return argument;
}

/** @brief テスト専用応答をD-Bus構造体から読む */
const QDBusArgument &operator>>(const QDBusArgument &argument, BadReply &reply)
{
    argument.beginStructure();
    argument >> reply.repos;
    argument >> reply.rows;
    argument.endStructure();
    return argument;
}

} // namespace testwire

Q_DECLARE_METATYPE(testwire::Repo)
Q_DECLARE_METATYPE(testwire::Row)
Q_DECLARE_METATYPE(testwire::BadReply)

namespace {

const QString wireSignature = QStringLiteral("(a(ss)a(sssssitib))");  // ワイヤ形式の期待署名

/**
 * @brief 検証用のパッケージ行を生成する
 * @param name パッケージ名
 * @param repoAlias リポジトリエイリアス
 * @param repoName リポジトリ名
 * @return 全属性を埋めた行
 */
PackageRow makeRow(const QString &name, const QString &repoAlias = QStringLiteral("oss"),
                   const QString &repoName = QStringLiteral("openSUSE"))
{
    PackageRow row;
    row.name             = name;
    row.summary          = QStringLiteral("summary of ") + name;
    row.version          = QStringLiteral("2.0");
    row.installedVersion = QStringLiteral("1.0");
    row.arch             = QStringLiteral("x86_64");
    row.repoAlias        = repoAlias;
    row.repoName         = repoName;
    row.size             = 4096;
    row.status           = 3;
    row.updatable        = true;
    return row;
}

} // namespace

/**
 * @brief 一覧応答を返す検証用オブジェクト
 */
class RowServer : public QObject
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.presire.qzypper.tests.PackageRow")

public:
    PackageListReply reply;          // Get が返す応答
    testwire::BadReply badReply;     // GetBad が返す応答
    QVariantList legacyRows;         // GetLegacy が返す旧形式 (av) の応答
    QList<QVariantMap> mapRows;      // GetMapArray が返す旧形式 (aa{sv}) の応答
    QStringList stringList;          // GetStringList が返す不正形式 (as) の応答

public slots:
    /** @brief 設定済みの応答を返す */
    PackageListReply Get() const { return reply; }

    /** @brief 範囲外索引を含むテスト専用応答を返す */
    testwire::BadReply GetBad() const { return badReply; }

    /** @brief 旧形式 (av) の応答を返す */
    QVariantList GetLegacy() const { return legacyRows; }

    /** @brief 旧形式 (aa{sv}) の応答を返す */
    QList<QVariantMap> GetMapArray() const { return mapRows; }

    /** @brief 一覧として解釈できない形式 (as) の応答を返す */
    QStringList GetStringList() const { return stringList; }
};

/**
 * @brief 遅延応答を手動で送る検証用オブジェクト
 */
class DelayedServer : public QObject, protected QDBusContext
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.presire.qzypper.tests.Delayed")

public:
    PackageListReply reply;   // 送信する応答

public slots:
    /** @brief 設定済みの応答を遅延返信として送る */
    void Get()
    {
        // 自動応答を抑止し、実際のバックエンドと同じ手動返信の署名を検証する
        setDelayedReply(true);
        connection().send(message().createReply(QVariantList{QVariant::fromValue(reply)}));
    }
};

/**
 * @class PackageRowTest
 * @brief PackageRowとPackageListReplyのマーシャリングを検証する
 */
class PackageRowTest : public QObject
{
    Q_OBJECT

private slots:
    /** @brief メタタイプ登録とpeer-to-peer接続を準備する */
    void initTestCase();
    /** @brief peer-to-peer接続を後始末する */
    void cleanupTestCase();
    /** @brief 型のD-Bus署名が期待値であることを検証する */
    void reportsWireSignature();
    /** @brief QVariantMapの往復で全属性が保たれることを検証する */
    void convertsVariantMapRoundTrip();
    /** @brief updatableキー欠落時にfalseとなることを検証する */
    void defaultsUpdatableWhenMissing();
    /** @brief PackageInfoの一覧が順序を保って行へ変換されることを検証する */
    void convertsPackageInfoRows();
    /** @brief 空一覧が往復できることを検証する */
    void roundTripsEmptyList();
    /** @brief peer-to-peerのD-Bus往復で署名と行の属性が保たれることを検証する */
    void roundTripsRows();
    /** @brief リポジトリ表がaliasの初出順で重複排除されることを検証する */
    void deduplicatesRepoTableByAlias();
    /** @brief 範囲外のリポジトリ索引が空文字へ倒れることを検証する */
    void ignoresOutOfRangeRepoIndex();
    /** @brief 遅延応答の署名と内容が正しいことを検証する */
    void repliesWithExpectedSignature();
    /** @brief イントロスペクションの戻り値型がワイヤ形式であることを検証する */
    void introspectsRowType();
    /** @brief 新形式・旧形式・不正形式の応答をdecodePackageListで判定する */
    void decodesWireFormats();
    /** @brief 展開済みのリストをdecodePackageListで判定する */
    void decodesExpandedLists();

private:
    /** @brief 非同期呼び出しの完了を待ち応答を返す
     * @param message 送信するD-Busメッセージ
     * @return 完了した呼び出しの応答
     */
    QDBusMessage waitForReply(const QDBusMessage &message);

    std::unique_ptr<QDBusServer> m_dbusServer;          // peer-to-peerサーバ
    std::unique_ptr<QDBusConnection> m_serverConnection; // サーバ側接続
    std::unique_ptr<QDBusConnection> m_clientConnection; // クライアント側接続
    RowServer m_rowServer;                              // 一覧応答の提供元
    DelayedServer m_delayedServer;                      // 遅延応答の提供元
};

void PackageRowTest::initTestCase()
{
    // 実行 : メタタイプを登録し、peer-to-peer接続を確立する
    // 署名(a(ss)a(sssssitib))は検証用応答と同一なので、最後に本番型を登録して
    // 署名から復元される型がPackageListReplyになるようにする
    qDBusRegisterMetaType<testwire::Repo>();
    qDBusRegisterMetaType<testwire::Row>();
    qDBusRegisterMetaType<testwire::BadReply>();
    qDBusRegisterMetaType<QList<QVariantMap>>();
    registerPackageListReplyType();

    m_dbusServer = std::make_unique<QDBusServer>(QStringLiteral("unix:tmpdir=/tmp"));
    QVERIFY2(m_dbusServer->isConnected(), qPrintable(m_dbusServer->lastError().message()));

    std::optional<QDBusConnection> peerConnection;
    QEventLoop waitLoop;
    QTimer waitTimer;
    waitTimer.setSingleShot(true);
    // timeoutで待機を終了し、接続失敗時に無期限に待たない
    connect(&waitTimer, &QTimer::timeout, &waitLoop, &QEventLoop::quit);
    // newConnectionでサーバ側接続を受け取り、待機を終了する
    connect(m_dbusServer.get(), &QDBusServer::newConnection, &waitLoop,
            [&peerConnection, &waitLoop](const QDBusConnection &connection) {
                peerConnection = connection;
                waitLoop.quit();
            });
    waitTimer.start(5000);

    m_clientConnection = std::make_unique<QDBusConnection>(
        QDBusConnection::connectToPeer(m_dbusServer->address(),
                                       QStringLiteral("tst_PackageRowClient")));
    QVERIFY2(m_clientConnection->isConnected(), qPrintable(m_clientConnection->lastError().message()));

    // 確認 : サーバ側接続を受け取り、検証用オブジェクトを公開する
    waitLoop.exec();
    QVERIFY2(peerConnection.has_value(), "peer connection timed out");
    m_serverConnection = std::make_unique<QDBusConnection>(*peerConnection);
    QVERIFY(m_serverConnection->isConnected());
    QVERIFY(m_serverConnection->registerObject(QStringLiteral("/t"), &m_rowServer,
                                               QDBusConnection::ExportAllSlots));
    QVERIFY(m_serverConnection->registerObject(QStringLiteral("/delay"), &m_delayedServer,
                                               QDBusConnection::ExportAllSlots));
}

void PackageRowTest::cleanupTestCase()
{
    QDBusConnection::disconnectFromPeer(QStringLiteral("tst_PackageRowClient"));
    m_serverConnection.reset();
    m_clientConnection.reset();
    m_dbusServer.reset();
}

QDBusMessage PackageRowTest::waitForReply(const QDBusMessage &message)
{
    const QDBusPendingCall call = m_clientConnection->asyncCall(message);
    QDBusPendingCallWatcher watcher(call);
    QSignalSpy spy(&watcher, &QDBusPendingCallWatcher::finished);
    if (spy.isEmpty())
        spy.wait(5000);
    return watcher.reply();
}

void PackageRowTest::reportsWireSignature()
{
    // 実行 : 登録済み型の署名を取得する
    const char *signature = QDBusMetaType::typeToSignature(QMetaType::fromType<PackageListReply>());

    // 確認 : 期待するワイヤ形式と一致する
    QVERIFY(signature != nullptr);
    QCOMPARE(QString::fromLatin1(signature), wireSignature);
}

void PackageRowTest::convertsVariantMapRoundTrip()
{
    // 実行 : 行をマップへ変換し、復元する
    const PackageRow original = makeRow(QStringLiteral("bash"));
    const QVariantMap map = original.toVariantMap();
    const PackageRow restored = PackageRow::fromVariantMap(map);

    // 確認 : キーはPackageInfoと一致し、行の全属性が復元される
    const QVariantMap infoMap = PackageInfo{}.toVariantMap();
    QCOMPARE(map.keys(), infoMap.keys());
    QCOMPARE(restored.name, original.name);
    QCOMPARE(restored.summary, original.summary);
    QCOMPARE(restored.version, original.version);
    QCOMPARE(restored.installedVersion, original.installedVersion);
    QCOMPARE(restored.arch, original.arch);
    QCOMPARE(restored.repoAlias, original.repoAlias);
    QCOMPARE(restored.repoName, original.repoName);
    QCOMPARE(restored.size, original.size);
    QCOMPARE(restored.status, original.status);
    QCOMPARE(restored.updatable, original.updatable);
}

void PackageRowTest::defaultsUpdatableWhenMissing()
{
    // 実行 : updatableキーを除いたマップから復元する
    QVariantMap map = makeRow(QStringLiteral("bash")).toVariantMap();
    map.remove(QStringLiteral("updatable"));
    const PackageRow restored = PackageRow::fromVariantMap(map);

    // 確認 : 欠落時はfalse、他のキー欠落時も既定値になる
    QVERIFY(!restored.updatable);
    const PackageRow empty = PackageRow::fromVariantMap(QVariantMap{});
    QVERIFY(!empty.updatable);
    QCOMPARE(empty.status, static_cast<int>(PackageStatus::NoInst));
    QCOMPARE(empty.size, quint64(0));
}

void PackageRowTest::convertsPackageInfoRows()
{
    // 実行 : 2件のPackageInfoを行へ変換する
    PackageInfo first;
    first.name = QStringLiteral("bash");
    first.summary = QStringLiteral("The GNU Bourne Again Shell");
    first.version = QStringLiteral("5.2");
    first.installedVersion = QStringLiteral("5.1");
    first.arch = QStringLiteral("x86_64");
    first.repoAlias = QStringLiteral("oss");
    first.repoName = QStringLiteral("openSUSE");
    first.size = 123456;
    first.status = static_cast<int>(PackageStatus::KeepInstalled);
    first.updatable = true;

    PackageInfo second;
    second.name = QStringLiteral("vim");
    second.status = static_cast<int>(PackageStatus::NoInst);

    const PackageRows rows = toPackageRows({first, second});

    // 確認 : 順序と全属性が保たれる
    QCOMPARE(rows.size(), 2);
    QCOMPARE(rows.at(0).name, first.name);
    QCOMPARE(rows.at(0).summary, first.summary);
    QCOMPARE(rows.at(0).version, first.version);
    QCOMPARE(rows.at(0).installedVersion, first.installedVersion);
    QCOMPARE(rows.at(0).arch, first.arch);
    QCOMPARE(rows.at(0).repoAlias, first.repoAlias);
    QCOMPARE(rows.at(0).repoName, first.repoName);
    QCOMPARE(rows.at(0).size, first.size);
    QCOMPARE(rows.at(0).status, first.status);
    QCOMPARE(rows.at(0).updatable, first.updatable);
    QCOMPARE(rows.at(1).name, second.name);
    QCOMPARE(rows.at(1).status, second.status);
    QVERIFY(!rows.at(1).updatable);
    QVERIFY(toPackageRows({}).isEmpty());
}

void PackageRowTest::roundTripsEmptyList()
{
    // 実行 : 空一覧の応答を往復させる
    m_rowServer.reply = PackageListReply{};
    const QDBusMessage response = waitForReply(
        QDBusMessage::createMethodCall(QString(), QStringLiteral("/t"),
                                       QStringLiteral("org.presire.qzypper.tests.PackageRow"),
                                       QStringLiteral("Get")));

    // 確認 : 空のまま受信できる
    QCOMPARE(response.type(), QDBusMessage::ReplyMessage);
    const PackageListReply decoded = qdbus_cast<PackageListReply>(response.arguments().value(0));
    QVERIFY(decoded.rows.isEmpty());
}

void PackageRowTest::roundTripsRows()
{
    // 実行 : リポジトリ有り・無しの2行を往復させる
    PackageListReply sent;
    sent.rows = {makeRow(QStringLiteral("bash")), makeRow(QStringLiteral("vim"), QString(), QString())};
    m_rowServer.reply = sent;
    const QDBusMessage response = waitForReply(
        QDBusMessage::createMethodCall(QString(), QStringLiteral("/t"),
                                       QStringLiteral("org.presire.qzypper.tests.PackageRow"),
                                       QStringLiteral("Get")));

    // 確認 : 署名と行の属性が保持される
    QCOMPARE(response.type(), QDBusMessage::ReplyMessage);
    const QVariant argument = response.arguments().value(0);
    const QDBusArgument raw = argument.value<QDBusArgument>();
    QCOMPARE(raw.currentSignature(), wireSignature);

    // 確認 : リポジトリ表は実在する1件だけである
    int repoCount = 0;
    raw.beginStructure();
    raw.beginArray();
    while (!raw.atEnd()) {
        raw.beginStructure();
        QString alias, name;
        raw >> alias >> name;
        raw.endStructure();
        ++repoCount;
    }
    raw.endArray();
    raw.endStructure();
    QCOMPARE(repoCount, 1);

    const PackageListReply decoded = qdbus_cast<PackageListReply>(argument);
    QCOMPARE(decoded.rows.size(), 2);
    QCOMPARE(decoded.rows.at(0).name, sent.rows.at(0).name);
    QCOMPARE(decoded.rows.at(0).summary, sent.rows.at(0).summary);
    QCOMPARE(decoded.rows.at(0).version, sent.rows.at(0).version);
    QCOMPARE(decoded.rows.at(0).installedVersion, sent.rows.at(0).installedVersion);
    QCOMPARE(decoded.rows.at(0).arch, sent.rows.at(0).arch);
    QCOMPARE(decoded.rows.at(0).repoAlias, sent.rows.at(0).repoAlias);
    QCOMPARE(decoded.rows.at(0).repoName, sent.rows.at(0).repoName);
    QCOMPARE(decoded.rows.at(0).size, sent.rows.at(0).size);
    QCOMPARE(decoded.rows.at(0).status, sent.rows.at(0).status);
    QCOMPARE(decoded.rows.at(0).updatable, sent.rows.at(0).updatable);
    QCOMPARE(decoded.rows.at(1).name, sent.rows.at(1).name);
    QVERIFY(decoded.rows.at(1).repoAlias.isEmpty());
    QVERIFY(decoded.rows.at(1).repoName.isEmpty());
}

void PackageRowTest::deduplicatesRepoTableByAlias()
{
    // 実行 : oss, oss, update, 空 の順で行を持つ応答を往復させる
    PackageListReply sent;
    sent.rows = {makeRow(QStringLiteral("bash")),
                 makeRow(QStringLiteral("grep")),
                 makeRow(QStringLiteral("vim"), QStringLiteral("update"), QStringLiteral("Update")),
                 makeRow(QStringLiteral("local"), QString(), QString())};
    m_rowServer.reply = sent;
    const QDBusMessage response = waitForReply(
        QDBusMessage::createMethodCall(QString(), QStringLiteral("/t"),
                                       QStringLiteral("org.presire.qzypper.tests.PackageRow"),
                                       QStringLiteral("Get")));
    QCOMPARE(response.type(), QDBusMessage::ReplyMessage);

    // 実行 : ワイヤ形式のリポジトリ表と行の索引を直接確認する
    const QDBusArgument argument = response.arguments().value(0).value<QDBusArgument>();
    QVector<QPair<QString, QString>> repos;
    QVector<int> indices;
    argument.beginStructure();
    argument.beginArray();
    while (!argument.atEnd()) {
        argument.beginStructure();
        QString alias, name;
        argument >> alias >> name;
        argument.endStructure();
        repos.append(qMakePair(alias, name));
    }
    argument.endArray();
    argument.beginArray();
    while (!argument.atEnd()) {
        argument.beginStructure();
        QString name, summary, version, installedVersion, arch;
        int repoIndex = 0;
        quint64 size = 0;
        int status = 0;
        bool updatable = false;
        argument >> name >> summary >> version >> installedVersion >> arch
                 >> repoIndex >> size >> status >> updatable;
        argument.endStructure();
        indices.append(repoIndex);
    }
    argument.endArray();
    argument.endStructure();

    // 確認 : 表は初出順で2件、索引は 0,0,1,-1
    QCOMPARE(repos.size(), 2);
    QCOMPARE(repos.at(0).first, QStringLiteral("oss"));
    QCOMPARE(repos.at(0).second, QStringLiteral("openSUSE"));
    QCOMPARE(repos.at(1).first, QStringLiteral("update"));
    QCOMPARE(repos.at(1).second, QStringLiteral("Update"));
    QCOMPARE(indices.size(), 4);
    QCOMPARE(indices.at(0), 0);
    QCOMPARE(indices.at(1), 0);
    QCOMPARE(indices.at(2), 1);
    QCOMPARE(indices.at(3), -1);
}

void PackageRowTest::ignoresOutOfRangeRepoIndex()
{
    // 実行 : 範囲外 (5) と負値 (-2) の索引を持つ応答を復元する
    testwire::BadReply bad;
    bad.repos = {testwire::Repo{QStringLiteral("oss"), QStringLiteral("openSUSE")}};
    testwire::Row first;
    first.name = QStringLiteral("bad-high");
    first.repoIndex = 5;
    testwire::Row second;
    second.name = QStringLiteral("bad-negative");
    second.repoIndex = -2;
    bad.rows = {first, second};
    m_rowServer.badReply = bad;

    const QDBusMessage response = waitForReply(
        QDBusMessage::createMethodCall(QString(), QStringLiteral("/t"),
                                       QStringLiteral("org.presire.qzypper.tests.PackageRow"),
                                       QStringLiteral("GetBad")));

    // 確認 : 行は残り、リポジトリだけが空文字になる
    QCOMPARE(response.type(), QDBusMessage::ReplyMessage);
    const PackageListReply decoded = qdbus_cast<PackageListReply>(response.arguments().value(0));
    QCOMPARE(decoded.rows.size(), 2);
    QCOMPARE(decoded.rows.at(0).name, QStringLiteral("bad-high"));
    QCOMPARE(decoded.rows.at(1).name, QStringLiteral("bad-negative"));
    for (const PackageRow &row : decoded.rows) {
        QVERIFY(row.repoAlias.isEmpty());
        QVERIFY(row.repoName.isEmpty());
    }
}

void PackageRowTest::repliesWithExpectedSignature()
{
    // 実行 : 遅延応答で1行を返す
    PackageListReply sent;
    sent.rows = {makeRow(QStringLiteral("bash"))};
    m_delayedServer.reply = sent;
    const QDBusMessage response = waitForReply(
        QDBusMessage::createMethodCall(QString(), QStringLiteral("/delay"),
                                       QStringLiteral("org.presire.qzypper.tests.Delayed"),
                                       QStringLiteral("Get")));

    // 確認 : 受信した応答の署名と内容が一致する
    QCOMPARE(response.type(), QDBusMessage::ReplyMessage);
    const QVariant argument = response.arguments().value(0);
    QCOMPARE(argument.value<QDBusArgument>().currentSignature(), wireSignature);
    const PackageListReply decoded = qdbus_cast<PackageListReply>(argument);
    QCOMPARE(decoded.rows.size(), 1);
    QCOMPARE(decoded.rows.at(0).name, QStringLiteral("bash"));
    QCOMPARE(decoded.rows.at(0).repoAlias, QStringLiteral("oss"));
}

void PackageRowTest::introspectsRowType()
{
    // 実行 : /t のイントロスペクションを取得する
    const QDBusMessage response = waitForReply(
        QDBusMessage::createMethodCall(QString(), QStringLiteral("/t"),
                                       QStringLiteral("org.freedesktop.DBus.Introspectable"),
                                       QStringLiteral("Introspect")));
    QCOMPARE(response.type(), QDBusMessage::ReplyMessage);
    const QString xml = response.arguments().value(0).toString();

    // 確認 : Get の out 引数がワイヤ形式で公開される
    QVERIFY2(xml.contains(QStringLiteral("<method name=\"Get\"")), qPrintable(xml));
    QVERIFY2(xml.contains(QStringLiteral("type=\"(a(ss)a(sssssitib))\"")), qPrintable(xml));
}

QTEST_MAIN(PackageRowTest)

void PackageRowTest::decodesWireFormats()
{
    // 実行 : 新形式の応答をdecodePackageListへ渡す
    PackageListReply sent;
    sent.rows = {makeRow(QStringLiteral("bash")), makeRow(QStringLiteral("vim"), QString(), QString())};
    m_rowServer.reply = sent;
    QDBusMessage response = waitForReply(
        QDBusMessage::createMethodCall(QString(), QStringLiteral("/t"),
                                       QStringLiteral("org.presire.qzypper.tests.PackageRow"),
                                       QStringLiteral("Get")));

    // 確認 : 新形式は行と属性を保って受理される
    QCOMPARE(response.type(), QDBusMessage::ReplyMessage);
    PackageRows rows;
    QVERIFY(decodePackageList(response.arguments().value(0), &rows));
    QCOMPARE(rows.size(), 2);
    QCOMPARE(rows.at(0).name, QStringLiteral("bash"));
    QCOMPARE(rows.at(0).updatable, true);
    QCOMPARE(rows.at(1).repoAlias, QString());

    // 実行 : 旧形式 av の応答を渡す
    m_rowServer.legacyRows = QVariantList{makeRow(QStringLiteral("av-a")).toVariantMap(),
                                          makeRow(QStringLiteral("av-b")).toVariantMap()};
    response = waitForReply(
        QDBusMessage::createMethodCall(QString(), QStringLiteral("/t"),
                                       QStringLiteral("org.presire.qzypper.tests.PackageRow"),
                                       QStringLiteral("GetLegacy")));

    // 確認 : av は旧形式として受理される
    QCOMPARE(response.type(), QDBusMessage::ReplyMessage);
    rows.clear();
    QVERIFY(decodePackageList(response.arguments().value(0), &rows));
    QCOMPARE(rows.size(), 2);
    QCOMPARE(rows.at(1).name, QStringLiteral("av-b"));

    // 実行 : 旧形式 aa{sv} の応答を渡す
    m_rowServer.mapRows = {makeRow(QStringLiteral("map-a")).toVariantMap(),
                           makeRow(QStringLiteral("map-b"), QString(), QString()).toVariantMap()};
    response = waitForReply(
        QDBusMessage::createMethodCall(QString(), QStringLiteral("/t"),
                                       QStringLiteral("org.presire.qzypper.tests.PackageRow"),
                                       QStringLiteral("GetMapArray")));

    // 確認 : aa{sv} は旧形式として受理される
    QCOMPARE(response.type(), QDBusMessage::ReplyMessage);
    rows.clear();
    QVERIFY(decodePackageList(response.arguments().value(0), &rows));
    QCOMPARE(rows.size(), 2);
    QCOMPARE(rows.at(1).name, QStringLiteral("map-b"));

    // 実行 : 一覧として解釈できない as の応答を渡す
    m_rowServer.stringList = {QStringLiteral("x")};
    response = waitForReply(
        QDBusMessage::createMethodCall(QString(), QStringLiteral("/t"),
                                       QStringLiteral("org.presire.qzypper.tests.PackageRow"),
                                       QStringLiteral("GetStringList")));

    // 確認 : 不正形式は拒否される
    QCOMPARE(response.type(), QDBusMessage::ReplyMessage);
    QVERIFY(!decodePackageList(response.arguments().value(0), &rows));
}

void PackageRowTest::decodesExpandedLists()
{
    PackageRows rows;

    // 実行 : 展開済みのQVariantList (全要素がQVariantMap) を渡す
    const QVariantList expanded{makeRow(QStringLiteral("a")).toVariantMap(),
                                makeRow(QStringLiteral("b")).toVariantMap()};

    // 確認 : 旧形式として受理される
    QVERIFY(decodePackageList(expanded, &rows));
    QCOMPARE(rows.size(), 2);
    QCOMPARE(rows.at(0).name, QStringLiteral("a"));

    // 実行 : QStringList を渡す
    // 確認 : 一覧としては拒否される
    QVERIFY(!decodePackageList(QStringList{QStringLiteral("a")}, &rows));

    // 実行 : QVariantList の要素に文字列を混ぜて渡す
    // 確認 : 一覧としては拒否される
    QVERIFY(!decodePackageList(QVariantList{makeRow(QStringLiteral("a")).toVariantMap(),
                                            QStringLiteral("b")},
                               &rows));
}

#include "tst_PackageRow.moc"
