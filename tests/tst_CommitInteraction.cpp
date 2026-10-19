#include <QtTest>
#include <chrono>
#include "CommitInteraction.h"

using namespace qZypper;

/**
 * @class CommitInteractionTest
 * @brief CommitInteraction.hの補助関数とログ集約の挙動を検証する
 *
 * コミット処理で使うテキスト切り詰め・行サニタイズ・プロンプトデータ生成・スクリプト出力集約を対象にし、libzyppは起動しない
 */
class CommitInteractionTest : public QObject {
    Q_OBJECT
private slots:
    /** @brief 上限内の短いテキストは切り詰めずそのまま返すことを検証する */
    void capTextKeepsShortText()
    {
        bool truncated = true;
        QCOMPARE(capText(QStringLiteral("hello"), 1024, &truncated), QStringLiteral("hello"));
        QVERIFY(!truncated);
    }

    /** @brief ASCIIテキストを上限バイト数で切り詰めることを検証する */
    void capTextTruncatesAscii()
    {
        const QString text(5000, QLatin1Char('a'));
        bool truncated = false;
        const QString result = capText(text, 100, &truncated);
        QVERIFY(truncated);
        QCOMPARE(result.toUtf8().size(), 100);
    }

    /** @brief UTF-8のマルチバイト文字を途中で分割しないことを検証する */
    void capTextDoesNotSplitMultibyte()
    {
        const QString text = QStringLiteral("ああ");
        bool truncated = false;
        const QString result = capText(text, 4, &truncated);
        QVERIFY(truncated);
        QCOMPARE(result, QStringLiteral("あ"));
        QVERIFY(!result.contains(QChar(0xFFFD)));
    }

    /** @brief 制御文字を除去しつつタブは残すことを検証する */
    void sanitizeStripsControlButKeepsTab()
    {
        const QString line = QStringLiteral("a\nb\tc\x01");
        QCOMPARE(sanitizeScriptLine(line), QStringLiteral("ab\tc"));
    }

    /** @brief 1行あたりのバイト数上限を超えないよう切り詰めることを検証する */
    void sanitizeCapsLineLength()
    {
        const QString line(CommitLimits::kMaxScriptLineBytes + 500, QLatin1Char('x'));
        QVERIFY(sanitizeScriptLine(line).toUtf8().size() <= CommitLimits::kMaxScriptLineBytes);
    }

    /** @brief ライセンス同意プロンプトのボタン構成と本文サイズ上限を検証する */
    void licensePromptDataHasButtonsAndCap()
    {
        const QString big(CommitLimits::kMaxLicenseTextBytes + 4096, QLatin1Char('L'));
        const QVariantMap data = buildLicensePromptData(QStringLiteral("pkg"), QStringLiteral("1.0"), big);

        QCOMPARE(data.value(QStringLiteral("package")).toString(), QStringLiteral("pkg"));
        QCOMPARE(data.value(QStringLiteral("version")).toString(), QStringLiteral("1.0"));
        QVERIFY(!data.value(QStringLiteral("message")).toString().isEmpty());
        QVERIFY(data.value(QStringLiteral("details")).toString().toUtf8().size()
                <= CommitLimits::kMaxLicenseTextBytes);

        const QVariantList buttons = data.value(QStringLiteral("buttons")).toList();
        QCOMPARE(buttons.size(), 2);
        QCOMPARE(buttons.at(0).toMap().value(QStringLiteral("action")).toString(), QStringLiteral("accept"));
        QCOMPARE(buttons.at(1).toMap().value(QStringLiteral("action")).toString(), QStringLiteral("reject"));
    }

    /** @brief ライセンス同意の回答をacceptとrejectにマッピングすることを検証する */
    void licenseAnswerMapping()
    {
        QVERIFY(licenseAnswerAccepted(QVariantMap{{QStringLiteral("action"), QStringLiteral("accept")}}));
        QVERIFY(!licenseAnswerAccepted(QVariantMap{{QStringLiteral("action"), QStringLiteral("reject")}}));
        QVERIFY(!licenseAnswerAccepted(QVariantMap{}));
    }

    /** @brief メディア交換プロンプトのリトライ・スキップ・中止ボタンを検証する */
    void mediaPromptDataHasButtons()
    {
        const QVariantMap data = buildMediaChangePromptData(
            QStringLiteral("cd:///"), QStringLiteral("LABEL"), 3,
            QStringLiteral("msg"), QStringLiteral("details"));

        QCOMPARE(data.value(QStringLiteral("url")).toString(), QStringLiteral("cd:///"));
        QCOMPARE(data.value(QStringLiteral("label")).toString(), QStringLiteral("LABEL"));
        QCOMPARE(data.value(QStringLiteral("mediaNr")).toUInt(), 3u);

        const QVariantList buttons = data.value(QStringLiteral("buttons")).toList();
        QCOMPARE(buttons.size(), 3);
        QCOMPARE(buttons.at(0).toMap().value(QStringLiteral("action")).toString(), QStringLiteral("retry"));
        QCOMPARE(buttons.at(1).toMap().value(QStringLiteral("action")).toString(), QStringLiteral("skip"));
        QCOMPARE(buttons.at(2).toMap().value(QStringLiteral("action")).toString(), QStringLiteral("abort"));
    }

    /** @brief メディア交換の回答を正規化し不正値はabortにすることを検証する */
    void mediaAnswerMapping()
    {
        QCOMPARE(mediaAnswerAction(QVariantMap{{QStringLiteral("action"), QStringLiteral("retry")}}),
                 QStringLiteral("retry"));
        QCOMPARE(mediaAnswerAction(QVariantMap{{QStringLiteral("action"), QStringLiteral("skip")}}),
                 QStringLiteral("skip"));
        QCOMPARE(mediaAnswerAction(QVariantMap{{QStringLiteral("action"), QStringLiteral("abort")}}),
                 QStringLiteral("abort"));
        QCOMPARE(mediaAnswerAction(QVariantMap{}), QStringLiteral("abort"));
        QCOMPARE(mediaAnswerAction(QVariantMap{{QStringLiteral("action"), QStringLiteral("bogus")}}),
                 QStringLiteral("abort"));
    }

    /** @brief ファイル競合プロンプトの継続・中止ボタンと件数を検証する */
    void fileConflictPromptDataHasButtons()
    {
        const QVariantMap data = buildFileConflictPromptData(
            {QStringLiteral("conflict one"), QStringLiteral("conflict two")}, 7);

        QCOMPARE(data.value(QStringLiteral("count")).toInt(), 7);
        QVERIFY(data.value(QStringLiteral("details")).toString().contains(QStringLiteral("conflict one")));

        const QVariantList buttons = data.value(QStringLiteral("buttons")).toList();
        QCOMPARE(buttons.size(), 2);
        QCOMPARE(buttons.at(0).toMap().value(QStringLiteral("action")).toString(), QStringLiteral("continue"));
        QCOMPARE(buttons.at(1).toMap().value(QStringLiteral("action")).toString(), QStringLiteral("abort"));
    }

    /** @brief ファイル競合の回答で継続可否を判定することを検証する */
    void fileConflictAnswerMapping()
    {
        QVERIFY(fileConflictAnswerContinue(QVariantMap{{QStringLiteral("action"), QStringLiteral("continue")}}));
        QVERIFY(!fileConflictAnswerContinue(QVariantMap{{QStringLiteral("action"), QStringLiteral("abort")}}));
        QVERIFY(!fileConflictAnswerContinue(QVariantMap{}));
    }

    /** @brief 先頭行は待たずに即時通知することを検証する */
    void aggregatorFlushesFirstLineImmediately()
    {
        QStringList emitted;
        ScriptOutputAggregator aggregator([&](const QString &, const QString &, const QString &line, bool) {
            emitted.append(line);
        });
        const auto t0 = ScriptOutputAggregator::Clock::time_point{};
        aggregator.addLine(QStringLiteral("p"), QStringLiteral("post"), QStringLiteral("l1"), false, t0);
        QCOMPARE(emitted, QStringList{QStringLiteral("l1")});
        QCOMPARE(aggregator.pendingCount(), 0);
    }

    /** @brief 行数閾値に達するまでバッファし、到達時にまとめて通知することを検証する */
    void aggregatorBatchesUntilThreshold()
    {
        QStringList emitted;
        ScriptOutputAggregator aggregator([&](const QString &, const QString &, const QString &line, bool) {
            emitted.append(line);
        });
        const auto t0 = ScriptOutputAggregator::Clock::time_point{};
        aggregator.addLine(QStringLiteral("p"), QStringLiteral("post"), QStringLiteral("first"), false, t0);
        for (int i = 0; i < CommitLimits::kFlushLineThreshold - 1; ++i)
            aggregator.addLine(QStringLiteral("p"), QStringLiteral("post"),
                               QStringLiteral("l%1").arg(i), false, t0);
        QCOMPARE(aggregator.pendingCount(), CommitLimits::kFlushLineThreshold - 1);

        aggregator.addLine(QStringLiteral("p"), QStringLiteral("post"), QStringLiteral("trigger"), false, t0);
        QCOMPARE(aggregator.pendingCount(), 0);
        QCOMPARE(emitted.size(), CommitLimits::kFlushLineThreshold + 1);
    }

    /** @brief 時間間隔を過ぎた行の追加で、バッファを通知することを検証する */
    void aggregatorFlushesOnTimeInterval()
    {
        QStringList emitted;
        ScriptOutputAggregator aggregator([&](const QString &, const QString &, const QString &line, bool) {
            emitted.append(line);
        });
        const auto t0 = ScriptOutputAggregator::Clock::time_point{};
        aggregator.addLine(QStringLiteral("p"), QStringLiteral("post"), QStringLiteral("first"), false, t0);
        aggregator.addLine(QStringLiteral("p"), QStringLiteral("post"), QStringLiteral("second"), false,
                           t0 + std::chrono::milliseconds(CommitLimits::kFlushIntervalMs + 1));
        QCOMPARE(aggregator.pendingCount(), 0);
        QCOMPARE(emitted.size(), 2);
    }

    /** @brief 保持行数の上限を超えたら、古い行を切り捨てることを検証する */
    void aggregatorCapsCollectedLines()
    {
        ScriptOutputAggregator aggregator;
        const auto t0 = ScriptOutputAggregator::Clock::time_point{};
        for (int i = 0; i < CommitLimits::kMaxScriptLines + 120; ++i)
            aggregator.addLine(QStringLiteral("p"), QStringLiteral("post"),
                               QStringLiteral("line%1").arg(i), false, t0);

        QCOMPARE(aggregator.collected().size(), CommitLimits::kMaxScriptLines);
        QVERIFY(aggregator.truncated());
        QCOMPARE(aggregator.collected().last(),
                 QStringLiteral("line%1").arg(CommitLimits::kMaxScriptLines + 119));
    }

    /** @brief rpmメッセージを通常行と分けて保持することを検証する */
    void aggregatorSeparatesRpmMessages()
    {
        ScriptOutputAggregator aggregator;
        aggregator.addRpmMessage(QStringLiteral("rpm msg"), false);
        QCOMPARE(aggregator.rpmMessages().size(), 1);
        QCOMPARE(aggregator.rpmMessages().first(), QStringLiteral("rpm msg"));
        QVERIFY(aggregator.collected().isEmpty());
    }

    /** @brief 空行はバッファにも通知にも含めないことを検証する */
    void aggregatorSkipsEmptyLines()
    {
        ScriptOutputAggregator aggregator;
        aggregator.addLine(QStringLiteral("p"), QStringLiteral("post"), QStringLiteral("\n"), false);
        QVERIFY(aggregator.collected().isEmpty());
        QCOMPARE(aggregator.pendingCount(), 0);
    }
};

QTEST_APPLESS_MAIN(CommitInteractionTest)
#include "tst_CommitInteraction.moc"
