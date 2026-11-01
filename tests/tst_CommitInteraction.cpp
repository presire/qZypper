#include <QtTest>
#include <chrono>
#include "CommitInteraction.h"
#include "ZyppCallbackReceiver.h"

using namespace qZypper;

namespace {
/**
 * @brief 進捗スロットルのテスト用サンプルを生成する
 * @param package パッケージ名
 * @param stage 進捗ステージ
 * @param percentage パッケージ単位の進捗 (%)
 * @param totalSteps 総ステップ数
 * @param completedSteps 完了ステップ数
 * @param overallPercentage 全体進捗 (%)
 * @return 生成した進捗サンプル
 */
ProgressSample progressSample(const QString &package, const QString &stage, int percentage,
                              int totalSteps = 10, int completedSteps = 0,
                              int overallPercentage = 0)
{
    return ProgressSample{package, stage, percentage, totalSteps, completedSteps, overallPercentage};
}
/**
 * @brief rpm出力行を持つUserDataを生成する
 * @param type UserDataの種別
 * @param line 出力行 (呼び出し側で寿命を保証する)
 * @return 生成したUserData
 * @note libzyppのclassic_rpmtransと同じくstd::reference_wrapper<const std::string>で格納する
 */
zypp::callback::UserData rpmoutUserData(const zypp::callback::UserData::ContentType &type,
                                        const std::string &line)
{
    zypp::callback::UserData data(type);
    data.set("line", std::cref(line));
    data.set("lineno", 1);
    return data;
}

/**
 * @brief 受信クラスの構築に必要な共有状態をまとめる
 */
struct ReceiverFixture {
    int completedSteps = 0;                 // 完了ステップ数
    std::atomic<bool> cancelFlag{false};    // キャンセルフラグ
    std::string problemDetail;              // エラー詳細
    std::vector<std::string> ignored;       // [Ignore]したパッケージ
};
} // namespace

/**
 * @class CommitInteractionTest
 * @brief CommitInteraction.hの補助関数とログ集約の挙動を検証する
 *
 * コミット処理で使うテキスト切り詰め、行サニタイズ、
 * プロンプトデータ生成、スクリプト出力集約を対象にする
 *
 * libzyppは起動しない
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

    /** @brief ライセンス同意の回答をacceptとrejectへ対応付けることを検証する */
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

    /** @brief メディア交換の回答を正規化し、不正値はabortにすることを検証する */
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

    /** @brief 時間間隔を過ぎた行の追加で、バッファした行を通知することを検証する */
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

    /** @brief バッチ送出先では先頭行が要素数1のバッチで届くことを検証する */
    void batchAggregatorDeliversFirstLineAsBatch()
    {
        std::vector<ScriptOutputEntry> lastBatch;
        int callCount = 0;
        ScriptOutputAggregator aggregator([&](const std::vector<ScriptOutputEntry> &entries) {
            ++callCount;
            lastBatch = entries;
        });
        const auto t0 = ScriptOutputAggregator::Clock::time_point{};

        aggregator.addLine(QStringLiteral("pkg"), QStringLiteral("post"), QStringLiteral("first"), false, t0);

        QCOMPARE(callCount, 1);
        QCOMPARE(static_cast<int>(lastBatch.size()), 1);
        QCOMPARE(lastBatch.at(0).package, QStringLiteral("pkg"));
        QCOMPARE(lastBatch.at(0).scriptType, QStringLiteral("post"));
        QCOMPARE(lastBatch.at(0).line, QStringLiteral("first"));
        QVERIFY(!lastBatch.at(0).isError);
        QCOMPARE(aggregator.pendingCount(), 0);
    }

    /** @brief 行数閾値までは溜め、到達時に未送信分を1バッチで届けることを検証する */
    void batchAggregatorBatchesUntilThreshold()
    {
        std::vector<ScriptOutputEntry> lastBatch;
        int callCount = 0;
        ScriptOutputAggregator aggregator([&](const std::vector<ScriptOutputEntry> &entries) {
            ++callCount;
            lastBatch = entries;
        });
        const auto t0 = ScriptOutputAggregator::Clock::time_point{};

        // 1行目は即時フラッシュされる
        aggregator.addLine(QStringLiteral("pkg"), QStringLiteral("post"), QStringLiteral("first"), false, t0);
        QCOMPARE(callCount, 1);
        QCOMPARE(static_cast<int>(lastBatch.size()), 1);

        // 2行目から50行目までは保留されたままになる
        for (int i = 0; i < CommitLimits::kFlushLineThreshold - 1; ++i)
            aggregator.addLine(QStringLiteral("pkg"), QStringLiteral("post"),
                               QStringLiteral("l%1").arg(i), false, t0);
        QCOMPARE(callCount, 1);
        QCOMPARE(aggregator.pendingCount(), CommitLimits::kFlushLineThreshold - 1);

        // 51行目で保留が閾値に達し、1バッチだけ届く
        aggregator.addLine(QStringLiteral("pkg"), QStringLiteral("post"),
                           QStringLiteral("trigger"), false, t0);
        QCOMPARE(callCount, 2);
        QCOMPARE(static_cast<int>(lastBatch.size()), CommitLimits::kFlushLineThreshold);
        QCOMPARE(lastBatch.front().line, QStringLiteral("l0"));
        QCOMPARE(lastBatch.back().line, QStringLiteral("trigger"));
        QCOMPARE(aggregator.pendingCount(), 0);
    }

    /** @brief flush()が残りの行を1バッチで届け、行の無害化も従来どおり行うことを検証する */
    void batchAggregatorFinalFlushDeliversRemainder()
    {
        std::vector<ScriptOutputEntry> lastBatch;
        int callCount = 0;
        ScriptOutputAggregator aggregator([&](const std::vector<ScriptOutputEntry> &entries) {
            ++callCount;
            lastBatch = entries;
        });
        const auto t0 = ScriptOutputAggregator::Clock::time_point{};

        aggregator.addLine(QStringLiteral("pkg"), QStringLiteral("pre"), QStringLiteral("first"),
                           false, t0);
        aggregator.addLine(QStringLiteral("pkg"), QStringLiteral("pre"),
                           QStringLiteral("\x1b[31mred"), true, t0);
        aggregator.addLine(QStringLiteral("pkg"), QStringLiteral("pre"),
                           QStringLiteral("a\nb\tc\x01"), false, t0);
        aggregator.addLine(QStringLiteral("pkg"), QStringLiteral("pre"),
                           QString(CommitLimits::kMaxScriptLineBytes + 500, QLatin1Char('x')),
                           false, t0);
        QCOMPARE(callCount, 1);
        QCOMPARE(aggregator.pendingCount(), 3);

        aggregator.flush(t0);

        QCOMPARE(callCount, 2);
        QCOMPARE(static_cast<int>(lastBatch.size()), 3);
        // 制御文字は1行単位の経路と同じく除去される
        QCOMPARE(lastBatch.at(0).line, QStringLiteral("[31mred"));
        QVERIFY(lastBatch.at(0).isError);
        QCOMPARE(lastBatch.at(1).line, QStringLiteral("ab\tc"));
        QVERIFY(lastBatch.at(2).line.toUtf8().size() <= CommitLimits::kMaxScriptLineBytes);
        QCOMPARE(aggregator.pendingCount(), 0);
    }

    /** @brief 保留が無い状態のflush()が空バッチを送出しないことを検証する */
    void batchAggregatorSkipsEmptyFlush()
    {
        int callCount = 0;
        int lastSize = -1;
        ScriptOutputAggregator aggregator([&](const std::vector<ScriptOutputEntry> &entries) {
            ++callCount;
            lastSize = static_cast<int>(entries.size());
        });
        const auto t0 = ScriptOutputAggregator::Clock::time_point{};

        aggregator.flush(t0);
        QCOMPARE(callCount, 0);

        // 直前のflush()でthrottle時刻が更新されるため、間隔を超えた時刻で追加する
        const auto t1 = t0 + std::chrono::milliseconds(CommitLimits::kFlushIntervalMs + 1);
        aggregator.addLine(QStringLiteral("pkg"), QStringLiteral("post"), QStringLiteral("first"),
                           false, t1);
        QCOMPARE(callCount, 1);
        QCOMPARE(lastSize, 1);

        aggregator.flush(t1);
        QCOMPARE(callCount, 1);
    }

    /** @brief 操作エラープロンプトの本文・ボタン・操作種別を検証する */
    void problemPromptDataHasButtonsAndPackage()
    {
        const QStringList actions{QStringLiteral("abort"), QStringLiteral("retry"),
                                  QStringLiteral("ignore")};
        const QVariantMap data = buildProblemPromptData(
            QStringLiteral("install"), QStringLiteral("bash"),
            QStringLiteral("io"), QStringLiteral("read error"), actions);

        QVERIFY(data.value(QStringLiteral("message")).toString().contains(QStringLiteral("bash")));
        QCOMPARE(data.value(QStringLiteral("operation")).toString(), QStringLiteral("install"));
        QCOMPARE(data.value(QStringLiteral("package")).toString(), QStringLiteral("bash"));
        QCOMPARE(data.value(QStringLiteral("errorKind")).toString(), QStringLiteral("io"));
        QCOMPARE(data.value(QStringLiteral("details")).toString(), QStringLiteral("read error"));
        QCOMPARE(data.value(QStringLiteral("actions")).toStringList(), actions);

        const QVariantList buttons = data.value(QStringLiteral("buttons")).toList();
        QCOMPARE(buttons.size(), 3);
        QCOMPARE(buttons.at(0).toMap().value(QStringLiteral("action")).toString(),
                 QStringLiteral("abort"));
        QCOMPARE(buttons.at(2).toMap().value(QStringLiteral("action")).toString(),
                 QStringLiteral("ignore"));
    }

    /** @brief 許可されたアクションだけを採用し、それ以外は[abort]へ倒すことを検証する */
    void problemAnswerMappingRejectsDisallowedAction()
    {
        const QStringList all{QStringLiteral("abort"), QStringLiteral("retry"),
                              QStringLiteral("ignore")};
        QCOMPARE(problemAnswerAction({{QStringLiteral("action"), QStringLiteral("retry")}}, all),
                 QStringLiteral("retry"));
        QCOMPARE(problemAnswerAction({{QStringLiteral("action"), QStringLiteral("ignore")}}, all),
                 QStringLiteral("ignore"));
        QCOMPARE(problemAnswerAction({{QStringLiteral("action"), QStringLiteral("abort")}}, all),
                 QStringLiteral("abort"));

        // 検証系ダウンロードエラーでは[Ignore]を提示しないため、回答されても[abort]とする
        const QStringList noIgnore{QStringLiteral("abort"), QStringLiteral("retry")};
        QCOMPARE(problemAnswerAction({{QStringLiteral("action"), QStringLiteral("ignore")}}, noIgnore),
                 QStringLiteral("abort"));
        QCOMPARE(problemAnswerAction({}, noIgnore), QStringLiteral("abort"));
        QCOMPARE(problemAnswerAction({{QStringLiteral("action"), QStringLiteral("bogus")}}, all),
                 QStringLiteral("abort"));
    }

    /** @brief 最初のサンプルは必ず送信することを検証する */
    void throttleEmitsFirstSample()
    {
        ProgressThrottle throttle;
        const auto t0 = std::chrono::steady_clock::time_point{};
        QVERIFY(throttle.shouldEmit(
            progressSample(QStringLiteral("pkg"), QStringLiteral("install"), 10), t0));
    }

    /** @brief 全項目が同じサンプルは経過時間にかかわらず送信しないことを検証する */
    void throttleDropsExactDuplicate()
    {
        ProgressThrottle throttle;
        const auto t0 = std::chrono::steady_clock::time_point{};
        const ProgressSample sample =
            progressSample(QStringLiteral("pkg"), QStringLiteral("install"), 10);
        QVERIFY(throttle.shouldEmit(sample, t0));
        QVERIFY(!throttle.shouldEmit(sample, t0 + std::chrono::milliseconds(10)));
        QVERIFY(!throttle.shouldEmit(sample, t0 + std::chrono::milliseconds(500)));
    }

    /** @brief パッケージまたはステージが変われば間隔内でも送信することを検証する */
    void throttleEmitsOnPackageOrStageChange()
    {
        ProgressThrottle throttle;
        const auto t0 = std::chrono::steady_clock::time_point{};
        QVERIFY(throttle.shouldEmit(
            progressSample(QStringLiteral("pkg-a"), QStringLiteral("install"), 10), t0));
        QVERIFY(!throttle.shouldEmit(
            progressSample(QStringLiteral("pkg-a"), QStringLiteral("install"), 11),
            t0 + std::chrono::milliseconds(10)));
        QVERIFY(throttle.shouldEmit(
            progressSample(QStringLiteral("pkg-b"), QStringLiteral("install"), 11),
            t0 + std::chrono::milliseconds(20)));
        QVERIFY(throttle.shouldEmit(
            progressSample(QStringLiteral("pkg-b"), QStringLiteral("download"), 11),
            t0 + std::chrono::milliseconds(30)));
    }

    /** @brief 0%と100%は間隔内でも送信し、同値の再送はしないことを検証する */
    void throttleEmitsZeroAndHundred()
    {
        ProgressThrottle throttle;
        const auto t0 = std::chrono::steady_clock::time_point{};
        QVERIFY(throttle.shouldEmit(
            progressSample(QStringLiteral("pkg"), QStringLiteral("install"), 50), t0));
        QVERIFY(throttle.shouldEmit(
            progressSample(QStringLiteral("pkg"), QStringLiteral("install"), 0),
            t0 + std::chrono::milliseconds(10)));
        QVERIFY(!throttle.shouldEmit(
            progressSample(QStringLiteral("pkg"), QStringLiteral("install"), 0),
            t0 + std::chrono::milliseconds(20)));
        QVERIFY(throttle.shouldEmit(
            progressSample(QStringLiteral("pkg"), QStringLiteral("install"), 100),
            t0 + std::chrono::milliseconds(30)));
    }

    /** @brief 同じパッケージの小さな進捗変化は間隔内では送信しないことを検証する */
    void throttleSuppressesWithinInterval()
    {
        ProgressThrottle throttle;
        const auto t0 = std::chrono::steady_clock::time_point{};
        QVERIFY(throttle.shouldEmit(
            progressSample(QStringLiteral("pkg"), QStringLiteral("install"), 10), t0));
        QVERIFY(!throttle.shouldEmit(
            progressSample(QStringLiteral("pkg"), QStringLiteral("install"), 11),
            t0 + std::chrono::milliseconds(10)));
        QVERIFY(!throttle.shouldEmit(
            progressSample(QStringLiteral("pkg"), QStringLiteral("install"), 12),
            t0 + std::chrono::milliseconds(20)));
    }

    /** @brief 前回送信から間隔が経過すれば同じパッケージでも送信することを検証する */
    void throttleEmitsAfterInterval()
    {
        ProgressThrottle throttle;
        const auto t0 = std::chrono::steady_clock::time_point{};
        QVERIFY(throttle.shouldEmit(
            progressSample(QStringLiteral("pkg"), QStringLiteral("install"), 10), t0));
        QVERIFY(throttle.shouldEmit(
            progressSample(QStringLiteral("pkg"), QStringLiteral("install"), 11),
            t0 + std::chrono::milliseconds(CommitLimits::kProgressIntervalMs + 20)));
    }

    /** @brief 間引かれた最新サンプルが1回だけ回収されることを検証する */
    void throttleTakePendingReturnsSuppressedSampleOnce()
    {
        ProgressThrottle throttle;
        const auto t0 = std::chrono::steady_clock::time_point{};
        QVERIFY(throttle.shouldEmit(
            progressSample(QStringLiteral("pkg"), QStringLiteral("install"), 10), t0));
        QVERIFY(!throttle.shouldEmit(
            progressSample(QStringLiteral("pkg"), QStringLiteral("install"), 88),
            t0 + std::chrono::milliseconds(10)));

        const auto pending = throttle.takePending(t0 + std::chrono::milliseconds(20));
        QVERIFY(pending.has_value());
        QCOMPARE(pending->percentage, 88);
        QCOMPARE(pending->package, QStringLiteral("pkg"));
        QVERIFY(!throttle.takePending(t0 + std::chrono::milliseconds(30)).has_value());
    }

    /** @brief 最後のサンプルが送信済みなら回収対象が無いことを検証する */
    void throttleTakePendingEmptyWhenLastSampleSent()
    {
        ProgressThrottle throttle;
        const auto t0 = std::chrono::steady_clock::time_point{};
        QVERIFY(!throttle.takePending(t0).has_value());
        QVERIFY(throttle.shouldEmit(
            progressSample(QStringLiteral("pkg"), QStringLiteral("install"), 10), t0));
        QVERIFY(!throttle.takePending(t0 + std::chrono::milliseconds(10)).has_value());

        // 間引き後に間隔経過で送信された場合も、保留は残らない
        QVERIFY(!throttle.shouldEmit(
            progressSample(QStringLiteral("pkg"), QStringLiteral("install"), 50),
            t0 + std::chrono::milliseconds(20)));
        QVERIFY(throttle.shouldEmit(
            progressSample(QStringLiteral("pkg"), QStringLiteral("install"), 60),
            t0 + std::chrono::milliseconds(CommitLimits::kProgressIntervalMs + 20)));
        QVERIFY(!throttle.takePending(
            t0 + std::chrono::milliseconds(CommitLimits::kProgressIntervalMs + 30)).has_value());
    }

    /** @brief 回収後は送信済みとして扱い、同値の再送や二重回収をしないことを検証する */
    void throttleTakePendingRecordsSampleAsSent()
    {
        ProgressThrottle throttle;
        const auto t0 = std::chrono::steady_clock::time_point{};
        const ProgressSample suppressed =
            progressSample(QStringLiteral("pkg"), QStringLiteral("install"), 88);
        QVERIFY(throttle.shouldEmit(
            progressSample(QStringLiteral("pkg"), QStringLiteral("install"), 10), t0));
        QVERIFY(!throttle.shouldEmit(suppressed, t0 + std::chrono::milliseconds(10)));
        QVERIFY(throttle.takePending(t0 + std::chrono::milliseconds(20)).has_value());

        // 回収済みの値と同じサンプルは重複として送らない
        QVERIFY(!throttle.shouldEmit(suppressed, t0 + std::chrono::milliseconds(500)));
        QVERIFY(!throttle.takePending(t0 + std::chrono::milliseconds(510)).has_value());
    }

    /** @brief 最新値が送信済みと同値に戻った場合、古い保留を回収しないことを検証する */
    void throttleTakePendingDroppedWhenNewestMatchesSent()
    {
        ProgressThrottle throttle;
        const auto t0 = std::chrono::steady_clock::time_point{};
        QVERIFY(throttle.shouldEmit(
            progressSample(QStringLiteral("pkg"), QStringLiteral("install"), 10), t0));
        QVERIFY(!throttle.shouldEmit(
            progressSample(QStringLiteral("pkg"), QStringLiteral("install"), 11),
            t0 + std::chrono::milliseconds(10)));
        QVERIFY(!throttle.shouldEmit(
            progressSample(QStringLiteral("pkg"), QStringLiteral("install"), 10),
            t0 + std::chrono::milliseconds(20)));
        QVERIFY(!throttle.takePending(t0 + std::chrono::milliseconds(30)).has_value());
    }
    /** @brief classic_rpmtransのインストール時rpm出力行が集約器と逐次通知へ届くことを検証する */
    void installReceiverForwardsClassicRpmout()
    {
        QStringList emitted;
        ScriptOutputAggregator aggregator(
            [&emitted](const QString &, const QString &, const QString &line, bool) { emitted.append(line); });
        ReceiverFixture fx;
        InstallReceiver receiver(ProgressCallbackFn(), StateEventCallbackFn(), fx.completedSteps, 1,
                                 fx.cancelFlag, PromptAskFn(), fx.problemDetail, fx.ignored, &aggregator);

        const std::string line = "Updating /etc/sysconfig/lifecycle-report ...";
        receiver.report(rpmoutUserData(zypp::target::rpm::InstallResolvableReport::contentRpmout, line));
        aggregator.flush();

        QCOMPARE(aggregator.collected(), QStringList{QString::fromStdString(line)});
        QCOMPARE(emitted, QStringList{QString::fromStdString(line)});
        QVERIFY(aggregator.rpmMessages().isEmpty());
    }

    /** @brief classic_rpmtransの削除時rpm出力行が集約器と逐次通知へ届くことを検証する */
    void removeReceiverForwardsClassicRpmout()
    {
        QStringList emitted;
        ScriptOutputAggregator aggregator(
            [&emitted](const QString &, const QString &, const QString &line, bool) { emitted.append(line); });
        ReceiverFixture fx;
        RemoveReceiver receiver(ProgressCallbackFn(), StateEventCallbackFn(), fx.completedSteps, 1,
                                fx.cancelFlag, PromptAskFn(), fx.problemDetail, fx.ignored, &aggregator);

        const std::string line = "Removing lifecycle data";
        receiver.report(rpmoutUserData(zypp::target::rpm::RemoveResolvableReport::contentRpmout, line));
        aggregator.flush();

        QCOMPARE(aggregator.collected(), QStringList{QString::fromStdString(line)});
        QCOMPARE(emitted, QStringList{QString::fromStdString(line)});
    }

    /** @brief rpmout以外の種別や集約器未指定の場合は何も転送しないことを検証する */
    void classicReceiversIgnoreOtherUserData()
    {
        int emitted = 0;
        ScriptOutputAggregator aggregator(
            [&emitted](const QString &, const QString &, const QString &, bool) { ++emitted; });
        ReceiverFixture fx;
        InstallReceiver install(ProgressCallbackFn(), StateEventCallbackFn(), fx.completedSteps, 1,
                                fx.cancelFlag, PromptAskFn(), fx.problemDetail, fx.ignored, &aggregator);
        RemoveReceiver remove(ProgressCallbackFn(), StateEventCallbackFn(), fx.completedSteps, 1,
                              fx.cancelFlag, PromptAskFn(), fx.problemDetail, fx.ignored, &aggregator);
        InstallReceiver noAggregator(ProgressCallbackFn(), StateEventCallbackFn(), fx.completedSteps, 1,
                                     fx.cancelFlag, PromptAskFn(), fx.problemDetail, fx.ignored);

        const std::string line = "should not appear";
        // 種別が異なる(削除用をインストール側へ、インストール用を削除側へ)ものは無視する
        install.report(rpmoutUserData(zypp::target::rpm::RemoveResolvableReport::contentRpmout, line));
        remove.report(rpmoutUserData(zypp::target::rpm::InstallResolvableReport::contentRpmout, line));
        install.report(rpmoutUserData(zypp::callback::UserData::ContentType("other", "type"), line));
        noAggregator.report(rpmoutUserData(zypp::target::rpm::InstallResolvableReport::contentRpmout, line));
        aggregator.flush();

        QVERIFY(aggregator.collected().isEmpty());
        QCOMPARE(emitted, 0);
    }
};

QTEST_APPLESS_MAIN(CommitInteractionTest)
#include "tst_CommitInteraction.moc"
