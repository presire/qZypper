/**
 * @file tst_ProductInfo.cpp
 * @brief 製品情報のQVariantMap変換の単体テスト
 *
 * D-Bus転送用マップのキーと既定値を検証する
 *
 * 入力: なし
 * 出力: テスト結果 (標準出力)
 * 依存: ProductInfo.h、Qt Test
 */

#include <QtTest>
#include "ProductInfo.h"

using namespace qZypper;

/**
 * @class ProductInfoTest
 * @brief 製品情報のD-Bus転送用マップ変換を検証する
 */
class ProductInfoTest : public QObject {
    Q_OBJECT
private slots:
    /** @brief 主要な属性がマップへ正しく反映されることを検証する */
    void mapsAllFields()
    {
        ProductInfo info;
        info.name             = QStringLiteral("openSUSE-Leap");
        info.shortName        = QStringLiteral("Leap 16.0");
        info.summary          = QStringLiteral("openSUSE Leap 16.0");
        info.version          = QStringLiteral("16.0-1");
        info.installedVersion = QStringLiteral("16.0-1");
        info.vendor           = QStringLiteral("openSUSE");
        info.arch             = QStringLiteral("x86_64");
        info.repoAlias        = QStringLiteral("repo-oss");
        info.repoName         = QStringLiteral("Main Repository");
        info.status           = 1;
        info.installed        = true;
        info.available        = true;
        info.isBase           = true;

        const QVariantMap map = info.toVariantMap();

        QCOMPARE(map.size(), 13);
        QCOMPARE(map.value("name").toString(), info.name);
        QCOMPARE(map.value("shortName").toString(), info.shortName);
        QCOMPARE(map.value("summary").toString(), info.summary);
        QCOMPARE(map.value("version").toString(), info.version);
        QCOMPARE(map.value("installedVersion").toString(), info.installedVersion);
        QCOMPARE(map.value("vendor").toString(), info.vendor);
        QCOMPARE(map.value("arch").toString(), info.arch);
        QCOMPARE(map.value("repoAlias").toString(), info.repoAlias);
        QCOMPARE(map.value("repoName").toString(), info.repoName);
        QCOMPARE(map.value("status").toInt(), 1);
        QCOMPARE(map.value("installed").toBool(), true);
        QCOMPARE(map.value("available").toBool(), true);
        QCOMPARE(map.value("isBase").toBool(), true);
    }

    /** @brief 既定値が未インストール・非ベースとして扱われることを検証する */
    void defaultsToNotInstalled()
    {
        const ProductInfo info;
        const QVariantMap map = info.toVariantMap();

        QCOMPARE(map.value("installed").toBool(), false);
        QCOMPARE(map.value("available").toBool(), false);
        QCOMPARE(map.value("isBase").toBool(), false);
        QCOMPARE(map.value("installedVersion").toString(), QString());
        QCOMPARE(map.value("status").toInt(), 0);
    }
};

QTEST_GUILESS_MAIN(ProductInfoTest)
#include "tst_ProductInfo.moc"
