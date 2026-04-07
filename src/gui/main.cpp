#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QIcon>
#include <QTranslator>
#include <QLocale>
#include <QStyleHints>
#include <QProcess>
#include <QSocketNotifier>
#include <QtQml>
#include <csignal>
#include <unistd.h>
#include "PackageController.h"

namespace {
/**
 * @brief self-pipe trick による SIGINT/SIGTERM ハンドリング。
 *
 * UNIX シグナルハンドラからは async-signal-safe な操作しか
 * 実行できないため、パイプに 1 バイト書き込むだけで復帰する。
 * 読み出し側を QSocketNotifier で監視し、Qt イベントループ内で
 * QCoreApplication::quit() を呼ぶ。これにより aboutToQuit が
 * 発火し、D-Bus バックエンドも正しく終了する。
 */
int g_signalPipe[2] = {-1, -1};

void unixSignalHandler(int /*signo*/)
{
    const char c = 1;
    ssize_t n = ::write(g_signalPipe[1], &c, sizeof(c));
    (void)n;  // async-signal-safe 文脈のためエラー処理不可
}
} // anonymous namespace

/**
 * @brief GUIアプリケーションのエントリポイント。
 *
 * QMLエンジンを初期化し、Fusionスタイルを設定してメインウィンドウを起動する。
 * @param argc コマンドライン引数の数
 * @param argv コマンドライン引数の配列
 * @return アプリケーションの終了コード
 */
int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);

    // SIGINT (Ctrl+C) / SIGTERM ハンドラを登録
    // self-pipe にバイトを書き込み、QSocketNotifier が Qt イベントループで
    // これを読み取って quit() を呼ぶ → aboutToQuit → quitBackend の順で
    // D-Bus バックエンドを伴って安全に終了する
    if (::pipe(g_signalPipe) == 0) {
        struct sigaction sa;
        sa.sa_handler = unixSignalHandler;
        sigemptyset(&sa.sa_mask);
        sa.sa_flags = SA_RESTART;
        ::sigaction(SIGINT,  &sa, nullptr);
        ::sigaction(SIGTERM, &sa, nullptr);

        auto *notifier = new QSocketNotifier(g_signalPipe[0],
                                             QSocketNotifier::Read, &app);
        QObject::connect(notifier, &QSocketNotifier::activated,
                         &app, [notifier](QSocketDescriptor fd, QSocketNotifier::Type) {
            char c;
            ssize_t n = ::read(int(qintptr(fd)), &c, sizeof(c));
            (void)n;
            notifier->setEnabled(false);
            QCoreApplication::quit();
        });
    }

    app.setApplicationName("qZypper");
    app.setApplicationVersion(APP_VERSION);
    app.setOrganizationName("Presire");
    app.setDesktopFileName("org.presire.qzypper");
    {
        QIcon appIcon;
        appIcon.addFile(":/icons/icons/qZypper@64.png",   QSize(64, 64));
        appIcon.addFile(":/icons/icons/qZypper@128.png",  QSize(128, 128));
        appIcon.addFile(":/icons/icons/qZypper@256.png",  QSize(256, 256));
        appIcon.addFile(":/icons/icons/qZypper@512.png",  QSize(512, 512));
        appIcon.addFile(":/icons/icons/qZypper@1024.png", QSize(1024, 1024));
        app.setWindowIcon(appIcon);
    }

    // ロケールに応じた翻訳ファイルの読み込み
    QTranslator translator;
    if (translator.load(QLocale(), "qZypper", "_", ":/translations")) {
        app.installTranslator(&translator);
    }

    // Fusion スタイルをデフォルトに (openSUSEテーマとの親和性)
    QQuickStyle::setStyle("Fusion");

    QQmlApplicationEngine engine;

    QObject::connect(
        &engine, &QQmlApplicationEngine::objectCreationFailed,
        &app, []() { QCoreApplication::exit(1); },
        Qt::QueuedConnection
    );

    engine.rootContext()->setContextProperty("appVersion", QStringLiteral(APP_VERSION));
    engine.rootContext()->setContextProperty("qtVersion", QString(qVersion()));

    // システムのカラースキームを検出してQMLに渡す
    // Qt.styleHints.colorScheme が正しく検出できない環境向けのフォールバック
    bool systemDarkMode = app.styleHints()->colorScheme() == Qt::ColorScheme::Dark;
    if (!systemDarkMode) {
        // gsettings からGNOME/GTKのテーマ設定を確認
        QProcess proc;
        proc.start("gsettings", {"get", "org.gnome.desktop.interface", "color-scheme"});
        if (proc.waitForFinished(500)) {
            QString result = proc.readAllStandardOutput().trimmed();
            if (result.contains("dark"))
                systemDarkMode = true;
        }
    }
    engine.rootContext()->setContextProperty("systemDarkMode", systemDarkMode);

    // PackageController をシングルトンとして登録
    auto *controller = new qZypper::PackageController(&app);
    qmlRegisterSingletonInstance("org.presire.qzypper.gui", 1, 0, "PackageController", controller);

    // GUI終了時に D-Bus バックエンドプロセスも終了させる
    QObject::connect(&app, &QCoreApplication::aboutToQuit,
                     controller, &qZypper::PackageController::quitBackend);

    engine.loadFromModule("org.presire.qzypper.gui", "Main");

    return app.exec();
}
