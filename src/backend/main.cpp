#include <QCoreApplication>
#include <iostream>
#include <QDBusConnection>
#include <QDBusError>
#include "PackageManagerAdaptor.h"

/**
 * @brief D-Busバックエンドサービスのエントリポイント。
 *
 * PackageManagerAdaptorを生成し、D-Busシステムバスにサービスを登録する。
 * @param argc コマンドライン引数の数
 * @param argv コマンドライン引数の配列
 * @return アプリケーションの終了コード
 */
int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    app.setApplicationName("qzypper-backend");
    app.setApplicationVersion(APP_VERSION);

    // D-Busオブジェクト (アダプタの親)
    QObject serviceObject;
    new qZypper::PackageManagerAdaptor(&serviceObject);

    // システムバスに接続
    auto bus = QDBusConnection::systemBus();
    if (!bus.isConnected()) {
        std::cerr << "Error: Cannot connect to system D-Bus." << std::endl;
        return 1;
    }

    // サービス名を登録
    if (!bus.registerService("org.presire.qzypper")) {
        std::cerr << "Error: Cannot register D-Bus service: "
                  << bus.lastError().message().toStdString() << std::endl;
        return 1;
    }

    // オブジェクトパスを登録
    if (!bus.registerObject("/org/presire/qzypper", &serviceObject)) {
        std::cerr << "Error: Cannot register D-Bus object: "
                  << bus.lastError().message().toStdString() << std::endl;
        return 1;
    }

    std::cout << "qzypper-backend started on system bus." << std::endl;

    return app.exec();
}
