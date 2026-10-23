/**
 * @file tst_RepoPresets.cpp
 * @brief コミュニティリポジトリのプリセットとURL展開プレビューの単体テスト
 *
 * ディストリビューション判定、プリセット内容、URL入力検証、展開プレビューを検証する
 * root権限とネットワークは不要
 *
 * 入力: ベース製品の名前、要約、バージョン、生のURL
 * 出力: テスト結果
 * 依存: RepoPresets (qzypper-zypp)、Qt Test
 */
#include <QtTest>
#include "RepoPresets.h"

using namespace qZypper;

/**
 * @class RepoPresetsTest
 * @brief コミュニティリポジトリのプリセット選択、URL入力検証、展開プレビューを検証する
 *
 * ディストリビューション判定、プリセット内容、URL入力検証、変数を含まないURLの展開プレビューを対象にする
 * root権限とネットワークは不要
 */
class RepoPresetsTest : public QObject {
    Q_OBJECT

private slots:
    /** @brief detectsPresetDistroの入力データを提供する */
    void detectsPresetDistro_data()
    {
        QTest::addColumn<QString>("name");
        QTest::addColumn<QString>("summary");
        QTest::addColumn<QString>("version");
        QTest::addColumn<int>("expected");
        const int none = static_cast<int>(PresetDistro::None);
        QTest::newRow("tumbleweed") << "openSUSE" << "openSUSE Tumbleweed" << "20260101" << static_cast<int>(PresetDistro::Tumbleweed);
        QTest::newRow("slowroll") << "openSUSE" << "openSUSE Tumbleweed-Slowroll" << "20260101" << none;
        QTest::newRow("leap-16") << "Leap" << "openSUSE Leap 16.0" << "16.0" << static_cast<int>(PresetDistro::Leap16);
        QTest::newRow("leap-15") << "Leap" << "openSUSE Leap 15.6" << "15.6" << none;
        QTest::newRow("sle-15") << "SLES" << "SUSE Linux Enterprise Server 15 SP6" << "15.6" << none;
        QTest::newRow("sles-16") << "SLES" << "SUSE Linux Enterprise Server 16.0" << "16.0" << none;
        QTest::newRow("unknown") << "Other" << "Other OS" << "1" << none;
    }

    /** @brief ベース製品の名前・要約・バージョンから対象ディストリビューションを判定できることを検証する */
    void detectsPresetDistro()
    {
        // 準備: 製品情報を取り出す
        QFETCH(QString, name);
        QFETCH(QString, summary);
        QFETCH(QString, version);
        QFETCH(int, expected);
        // 実行/確認: TumbleweedとLeap 16だけが対象になる
        QCOMPARE(static_cast<int>(detectPresetDistro(name, summary, version)), expected);
    }

    /** @brief プリセットが対象ディストリビューションごとの内容と共通属性を持つことを検証する */
    void providesPresetsPerDistro()
    {
        // 準備/実行: 各ディストリビューションのプリセットを得る
        const auto tumbleweed = repoPresetsFor(PresetDistro::Tumbleweed);
        const auto leap = repoPresetsFor(PresetDistro::Leap16);
        // 確認: 件数と、対象外が空であること
        QCOMPARE(tumbleweed.size(), std::size_t(6));
        QCOMPARE(leap.size(), std::size_t(4));
        QVERIFY(repoPresetsFor(PresetDistro::None).empty());
        QStringList aliases;
        for (const RepoPreset &preset : leap) {
            QVERIFY(preset.url.startsWith(QLatin1String("https://")));
            QVERIFY(preset.url.contains(QLatin1String("$releasever")));
            aliases << preset.alias;
        }
        QVERIFY(!aliases.contains(QStringLiteral("libdvdcss-repo")));
        QVERIFY(!aliases.contains(QStringLiteral("openh264-repo")));
        aliases.removeDuplicates();
        QCOMPARE(aliases.size(), 4);
        for (const RepoPreset &preset : tumbleweed) {
            QVERIFY2(preset.url.startsWith(QLatin1String("https://")), qPrintable(preset.url));
            QVERIFY(!preset.url.contains(QLatin1Char('$')));
            QVERIFY(preset.priority >= 1 && preset.priority <= 99);
            const QVariantMap map = preset.toVariantMap();
            QVERIFY(map.value(QStringLiteral("thirdParty")).toBool());
            QVERIFY(map.value(QStringLiteral("enabled")).toBool());
            QVERIFY(map.value(QStringLiteral("autoRefresh")).toBool());
            for (const char *key : {"id", "name", "alias", "url", "description"}) {
                QVERIFY2(!map.value(QLatin1String(key)).toString().isEmpty(), key);
            }
        }
    }

    /** @brief URL入力の長さと制御文字が検証されることを確認する */
    void validatesRepoUrlInput()
    {
        // 準備/実行/確認: 通常のURLは通り、制御文字と過長は拒否される
        QVERIFY(validRepoUrlInput(QStringLiteral("https://example.com/$releasever/")));
        QVERIFY(validRepoUrlInput(QString()));
        QVERIFY(!validRepoUrlInput(QStringLiteral("https://example.com/\n")));
        QVERIFY(!validRepoUrlInput(QStringLiteral("https://example.com/\x7f")));
        QVERIFY(!validRepoUrlInput(QStringLiteral("https://example.com/") + QString(kMaxRepoUrlBytes, QLatin1Char('a'))));
    }

    /** @brief 変数を含まない・不正なURLは展開プレビューが空になることを検証する */
    void returnsEmptyPreviewWhenNothingToExpand()
    {
        // 準備/実行/確認: $を含まないURL、制御文字入り、空文字列は空を返す (libzyppの対象製品は不要)
        QVERIFY(expandRepoUrlPreview(QStringLiteral("https://example.com/repo/")).isEmpty());
        QVERIFY(expandRepoUrlPreview(QStringLiteral("https://user:secret@example.com/repo/")).isEmpty());
        QVERIFY(expandRepoUrlPreview(QStringLiteral("https://example.com/$releasever\n")).isEmpty());
        QVERIFY(expandRepoUrlPreview(QString()).isEmpty());
    }

    /** @brief 展開結果がパスワードを含まず、変数が置き換わることを検証する */
    void expandsVariablesAndMasksPassword()
    {
        // 準備: $archを含みパスワードを持つURLを用意する
        const QString raw = QStringLiteral("https://user:secret@example.com/$arch/");
        // 実行: 展開する
        const QString expanded = expandRepoUrlPreview(raw);
        // 確認: $archが置き換わり、パスワードが伏せられる
        QVERIFY2(!expanded.isEmpty(), "expected expansion of $arch");
        QVERIFY(!expanded.contains(QLatin1String("$arch")));
        QVERIFY(!expanded.contains(QLatin1String("secret")));
        QVERIFY(expanded.contains(QLatin1String("user")));
    }
};

QTEST_GUILESS_MAIN(RepoPresetsTest)
#include "tst_RepoPresets.moc"
