#include <QTemporaryDir>
#include <QtTest>

#include "SelectionFixture.h"
#include "ZyppManager.h"

using namespace qZypper;

/**
 * @brief シャットダウン時にソルバー設定の上書きが破棄されることを検証する
 */
class SolverSettingsResetTest : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void shutdownResetsOverrides();
    void cleanupTestCase();

private:
    QTemporaryDir m_root;
    SelectionFixture m_fixture;
};

/** @brief 一時ルートでzyppを初期化する */
void SolverSettingsResetTest::initTestCase()
{
    qputenv("ZYPP_READONLY_HACK", "1");
    QVERIFY(m_root.isValid());

    if (!m_fixture.build(m_root.path())) {
        if (!m_fixture.skipReason.isEmpty())
            QSKIP(qPrintable(m_fixture.skipReason));
        QVERIFY2(false, qPrintable(m_fixture.error));
    }

    auto &manager = ZyppManager::instance();
    QVERIFY2(manager.initialize(m_root.path().toStdString(), true),
             manager.lastError().c_str());
}

/**
 * @brief 上書き設定がshutdownと再初期化で既定値へ戻ることを検証する
 */
void SolverSettingsResetTest::shutdownResetsOverrides()
{
    // 準備 : すべての項目をzypp.conf既定値の逆へ設定する
    auto &manager = ZyppManager::instance();
    const QVariantMap defaults = manager.getSolverSettings().value(QStringLiteral("defaults")).toMap();
    QCOMPARE(defaults.size(), 3);

    ZyppManager::SolverOverrides patch;
    patch.installRecommends = !defaults.value(QStringLiteral("installRecommends")).toBool();
    patch.cleandepsOnRemove = !defaults.value(QStringLiteral("cleandepsOnRemove")).toBool();
    patch.allowVendorChange = !defaults.value(QStringLiteral("allowVendorChange")).toBool();
    QVERIFY(manager.setSolverSettings(patch));
    QCOMPARE(manager.getSolverSettings().value(QStringLiteral("installRecommends")).toBool(),
             patch.installRecommends.value());

    // 実行 : シャットダウンして再初期化する
    manager.shutdown();
    QVERIFY2(manager.initialize(m_root.path().toStdString(), true),
             manager.lastError().c_str());

    // 確認 : 各有効値が既定値へ戻っている
    const QVariantMap effective = manager.getSolverSettings();
    const QVariantMap restoredDefaults =
        effective.value(QStringLiteral("defaults")).toMap();
    QCOMPARE(effective.value(QStringLiteral("installRecommends")).toBool(),
             restoredDefaults.value(QStringLiteral("installRecommends")).toBool());
    QCOMPARE(effective.value(QStringLiteral("cleandepsOnRemove")).toBool(),
             restoredDefaults.value(QStringLiteral("cleandepsOnRemove")).toBool());
    QCOMPARE(effective.value(QStringLiteral("allowVendorChange")).toBool(),
             restoredDefaults.value(QStringLiteral("allowVendorChange")).toBool());
}

/** @brief zyppの状態を解放する */
void SolverSettingsResetTest::cleanupTestCase()
{
    ZyppManager::instance().shutdown();
}

QTEST_GUILESS_MAIN(SolverSettingsResetTest)
#include "tst_SolverSettingsReset.moc"
