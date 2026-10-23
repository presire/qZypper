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
 * @brief self-pipe trickによるSIGINT/SIGTERMハンドリング
 *
 * UNIXシグナルハンドラからはasync-signal-safeな操作しか
 * 実行できないため、パイプに1バイト書き込むだけで復帰する
 * 読み出し側をQSocketNotifierで監視し、Qtイベントループ内でQCoreApplication::exit()を呼ぶ
 */
int g_signalPipe[2] = {-1, -1};

/**
 * @brief 受信シグナルをQtイベントループへ通知する
 * @param signo 受信したシグナル番号
 * @note async-signal-safeなwrite()だけを実行する
 */
void unixSignalHandler(int /*signo*/)
{
    const char c = 1;
    ssize_t n = ::write(g_signalPipe[1], &c, sizeof(c));
    (void)n;  // async-signal-safe文脈のためエラー処理不可
}
} // anonymous namespace

/**
 * @brief GUIアプリケーションのエントリポイント
 *
 * QMLエンジンを初期化し、Fusionスタイルを設定してメインウィンドウを起動する
 * @param argc コマンドライン引数の数
 * @param argv コマンドライン引数の配列
 * @return アプリケーションの終了コード
 */
int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);

    // SIGINT ([Ctrl] + [C]) / SIGTERMハンドラを登録
    // self-pipeへバイトを書き込み、QSocketNotifierがQtイベントループ内でこれを受信して読み取り、exit()を呼ぶ
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
            // quit()はウィンドウのcloseを経由し、未適用の変更の確認ダイアログで停止するため、exit()で確認を迂回して終了する
            // (バックエンドはセッション所有者の切断を検出して選択状態を破棄する)
            QCoreApplication::exit(0);
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

    // Fusionスタイルを既定とする (SUSEテーマとの親和性のため)
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
    // styleHints()のcolorScheme()が正しく検出できない環境向けのフォールバック
    bool systemDarkMode = app.styleHints()->colorScheme() == Qt::ColorScheme::Dark;
    if (!systemDarkMode) {
        // gsettingsからGNOME/GTKのテーマ設定を確認
        QProcess proc;
        proc.start("gsettings", {"get", "org.gnome.desktop.interface", "color-scheme"});
        if (proc.waitForFinished(500)) {
            QString result = proc.readAllStandardOutput().trimmed();
            if (result.contains("dark"))
                systemDarkMode = true;
        }
    }
    engine.rootContext()->setContextProperty("systemDarkMode", systemDarkMode);

    // PackageControllerをシングルトンとして登録
    auto *controller = new qZypper::PackageController(&app);
    qmlRegisterSingletonInstance("org.presire.qzypper.gui", 1, 0, "PackageController", controller);

    engine.loadFromModule("org.presire.qzypper.gui", "Main");

    return app.exec();
}
