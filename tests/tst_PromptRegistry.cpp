#include <QtTest>
#include <future>
#include "PolkitAuthorization.h"
#include "PromptRegistry.h"

using namespace qZypper;
using namespace std::chrono_literals;

/**
 * @class PromptRegistryTest
 * @brief 認可プロンプトの問合せ・回答・保留・中断の振る舞いを検証する
 *
 * 正常回答の受理、不正回答や誤ったidの拒否、タイムアウト時の既定値、所有者消滅や取消しによる起床、認可待ちの保留と解放、
 * 2重保留の拒否、認可idの一致時だけの消去を対象にする
 */
class PromptRegistryTest : public QObject {
    Q_OBJECT
private slots:
    /** @brief 有効な回答を受理し結果に反映することを検証する */
    void acceptsMatchingAnswer()
    {
        // 準備 : retryを許す要求と100[ms]の期限を用意する
        PromptRegistry registry; registry.open();
        PromptRequest request; request.actions << "retry"; request.timeout = 100ms;
        // 実行 : 問合せ中にretryで回答する
        const auto result = registry.ask(request, [&registry](quint32 id, const QString &, const QVariantMap &) {
            QVERIFY(registry.answer(id, {{"action", "retry"}}));
        });
        // 確認 : 回答のactionが結果に反映されることを確認する
        QCOMPARE(result.value("action").toString(), "retry");
    }
    /** @brief rejectsInvalidAnswersの入力データを提供する */
    void rejectsInvalidAnswers_data()
    {
        QTest::addColumn<QVariantMap>("answer");
        QTest::newRow("unknown-action") << QVariantMap{{"action", "proceed"}};
        QTest::newRow("extra-key") << QVariantMap{{"action", "abort"}, {"extra", true}};
        QTest::newRow("wrong-type") << QVariantMap{{"action", true}};
        QTest::newRow("missing-action") << QVariantMap{{"confirm", true}};
        QTest::newRow("empty") << QVariantMap{};
    }
    /** @brief 不正な回答を拒否し既定値へフォールバックすることを検証する */
    void rejectsInvalidAnswers()
    {
        // 準備 : 不正な回答マップと期限0msの要求を用意する
        QFETCH(QVariantMap, answer);
        PromptRegistry registry; registry.open(); PromptRequest request; request.timeout = 0ms;
        bool accepted = true;
        // 実行 : 問合せ中にその回答を返す
        const auto result = registry.ask(request, [&registry, &answer, &accepted](quint32 id, const QString &, const QVariantMap &) {
            accepted = registry.answer(id, answer);
        });
        // 確認 : 受理されず既定値が返ることを確認する
        QVERIFY(!accepted); QCOMPARE(result, request.safeDefault);
    }
    /** @brief 誤ったidの回答を拒否し既定値へフォールバックすることを検証する */
    void rejectsWrongId()
    {
        // 準備 : 期限0[ms]の要求を用意する
        PromptRegistry registry; registry.open(); PromptRequest request; request.timeout = 0ms;
        bool accepted = true;
        // 実行 : 問合せ中にidを1つずらして回答する
        const auto result = registry.ask(request, [&registry, &accepted](quint32 id, const QString &, const QVariantMap &) {
            accepted = registry.answer(id + 1, {{"action", "abort"}});
        });
        // 確認 : 受理されず既定値が返ることを確認する
        QVERIFY(!accepted); QCOMPARE(result, request.safeDefault);
    }
    /** @brief タイムアウト時に既定値を返すことを検証する */
    void returnsDefaultWhenTimedOut()
    {
        // 準備 : 期限0[ms]の要求を用意する
        PromptRegistry registry; registry.open(); PromptRequest request; request.timeout = 0ms;
        // 実行 : コールバックが何もしないまま問い合わせる
        const auto result = registry.ask(request, [](quint32, const QString &, const QVariantMap &) {});
        // 確認 : 既定値が返ることを確認する
        QCOMPARE(result, request.safeDefault);
    }
    /** @brief 所有者消滅や操作取消しで問合せが起床し既定値になることを検証する */
    void wakesWhenOwnerDisappearsOrOperationCancelled()
    {
        // 準備 : 500[ms]の期限で問合せを別スレッドで開始する
        PromptRegistry registry; registry.open(); PromptRequest request; request.timeout = 500ms;
        std::promise<void> announced; auto notification = announced.get_future();
        auto result = std::async(std::launch::async, [&] {
            return registry.ask(request, [&](quint32, const QString &, const QVariantMap &) { announced.set_value(); });
        });
        QVERIFY(notification.wait_for(1s) == std::future_status::ready);
        // 実行 : 操作を取消す
        registry.interrupt();
        // 確認 : ただちに起床し既定値が返ることを確認する
        QVERIFY(result.wait_for(1s) == std::future_status::ready);
        QCOMPARE(result.get(), request.safeDefault);
    }
    /** @brief 次のプロンプト後に届いた古い回答を拒否することを検証する */
    void rejectsStaleAnswerAfterNextPrompt()
    {
        // 準備 : 期限0[ms]の要求で最初の問合せのidを控える
        PromptRegistry registry; registry.open(); PromptRequest request; request.timeout = 0ms;
        quint32 oldId = 0;
        registry.ask(request, [&](quint32 id, const QString &, const QVariantMap &) { oldId = id; });
        bool accepted = true;
        // 実行 : 次の問合せ中に古いidで回答する
        registry.ask(request, [&](quint32, const QString &, const QVariantMap &) { accepted = registry.answer(oldId, {{"action", "abort"}}); });
        // 確認 : 受理されないことを確認する
        QVERIFY(!accepted);
    }
    /** @brief プロンプト前に中断された場合は通知せず既定値になることを検証する */
    void defaultsWhenInterruptedBeforePrompt()
    {
        // 準備 : 問合せ前に取消しておく
        PromptRegistry registry; registry.open(); registry.interrupt(); PromptRequest request;
        bool notified = false;
        // 実行 : 中断済みの状態で問い合わせる
        const auto result = registry.ask(request, [&](quint32, const QString &, const QVariantMap &) { notified = true; });
        // 確認 : 通知されず既定値が返ることを確認する
        QVERIFY(!notified); QCOMPARE(result, request.safeDefault);
    }
    /** @brief 保留した問合せが期限を過ぎても解放まで既定値で終わらないことを検証する */
    void heldPromptOutlivesTimeoutUntilReleased()
    {
        // 準備 : 認可待ちで保留した問合せ (期限50[ms])
        PromptRegistry registry; registry.open();
        PromptRequest request; request.actions << "trust"; request.timeout = 50ms;
        std::promise<quint32> announced; auto notification = announced.get_future();
        auto result = std::async(std::launch::async, [&] {
            return registry.ask(request, [&](quint32 id, const QString &, const QVariantMap &) {
                QVERIFY(registry.hold(id, {{"action", "trust"}}));
                announced.set_value(id);
            });
        });
        QVERIFY(notification.wait_for(1s) == std::future_status::ready);
        const quint32 id = notification.get();
        // 実行 : 期限を過ぎても保留中は既定値で終わらない
        QVERIFY(result.wait_for(200ms) == std::future_status::timeout);
        QVERIFY(!registry.answer(id, {{"action", "abort"}}));
        QVERIFY(registry.release(id, {{"action", "trust"}}));
        // 確認 : 認可後の回答が反映される
        QVERIFY(result.wait_for(1s) == std::future_status::ready);
        QCOMPARE(result.get().value("action").toString(), "trust");
    }
    /** @brief 中断後の解放が失敗として扱われることを検証する */
    void releaseFailsAfterInterrupt()
    {
        // 準備 : 保留中の問合せ
        PromptRegistry registry; registry.open();
        PromptRequest request; request.actions << "trust"; request.timeout = 50ms;
        std::promise<quint32> announced; auto notification = announced.get_future();
        auto result = std::async(std::launch::async, [&] {
            return registry.ask(request, [&](quint32 id, const QString &, const QVariantMap &) {
                QVERIFY(registry.hold(id, {{"action", "trust"}}));
                announced.set_value(id);
            });
        });
        QVERIFY(notification.wait_for(1s) == std::future_status::ready);
        const quint32 id = notification.get();
        // 実行 : 取消しで中断された後に認可が完了する
        registry.interrupt();
        QVERIFY(result.wait_for(1s) == std::future_status::ready);
        QCOMPARE(result.get(), request.safeDefault);
        // 確認 : 遅れた認可結果は反映されず、失敗として扱われる
        QVERIFY(!registry.release(id, {{"action", "trust"}}));
    }
    /** @brief 不正な回答や重複した保留を拒否することを検証する */
    void holdRejectsInvalidOrDuplicateAnswers()
    {
        // 準備 : trustを許す要求と期限0msを用意する
        PromptRegistry registry; registry.open();
        PromptRequest request; request.actions << "trust"; request.timeout = 0ms;
        bool invalidHeld = true, wrongIdHeld = true, secondHeld = true, released = false;
        // 実行 : 保留中に不正な回答・誤ったid・重複を順に試す
        registry.ask(request, [&](quint32 id, const QString &, const QVariantMap &) {
            invalidHeld = registry.hold(id, {{"action", "accept-once"}});
            wrongIdHeld = registry.hold(id + 1, {{"action", "trust"}});
            QVERIFY(registry.hold(id, {{"action", "trust"}}));
            secondHeld = registry.hold(id, {{"action", "trust"}});
            released = registry.release(id, {{"action", "abort"}});
        });
        // 確認 : 不要な保留が却下され正しい解放だけが成功することを確認する
        QVERIFY(!invalidHeld); QVERIFY(!wrongIdHeld); QVERIFY(!secondHeld); QVERIFY(released);
    }
    /** @brief 認可idが一致するときだけ消去することを検証する */
    void clearsAuthorizationIdOnlyWhenItMatches()
    {
        // 準備 : 新しい認可要求が始まっている
        QString current = QStringLiteral("new-request");
        // 実行 : 古い要求の完了通知が届く
        clearAuthorizationId(current, QStringLiteral("old-request"));
        // 確認 : 新しい要求の識別子は残る
        QCOMPARE(current, QStringLiteral("new-request"));
        clearAuthorizationId(current, QString());
        QCOMPARE(current, QStringLiteral("new-request"));
        clearAuthorizationId(current, QStringLiteral("new-request"));
        QVERIFY(current.isEmpty());
    }
};

QTEST_GUILESS_MAIN(PromptRegistryTest)
#include "tst_PromptRegistry.moc"
