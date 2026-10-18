#include "PolkitAuthorization.h"
#include "PromptRegistry.h"
#include <QtTest>
#include <future>

using namespace qZypper;
using namespace std::chrono_literals;

class PromptRegistryTest : public QObject {
    Q_OBJECT
private slots:
    void acceptsMatchingAnswer()
    {
        // Given
        PromptRegistry registry; registry.open();
        PromptRequest request; request.actions << "retry"; request.timeout = 100ms;
        // When
        const auto result = registry.ask(request, [&registry](quint32 id, const QString &, const QVariantMap &) {
            QVERIFY(registry.answer(id, {{"action", "retry"}}));
        });
        // Then
        QCOMPARE(result.value("action").toString(), "retry");
    }
    void rejectsInvalidAnswers_data()
    {
        QTest::addColumn<QVariantMap>("answer");
        QTest::newRow("unknown-action") << QVariantMap{{"action", "proceed"}};
        QTest::newRow("extra-key") << QVariantMap{{"action", "abort"}, {"extra", true}};
        QTest::newRow("wrong-type") << QVariantMap{{"action", true}};
        QTest::newRow("missing-action") << QVariantMap{{"confirm", true}};
        QTest::newRow("empty") << QVariantMap{};
    }
    void rejectsInvalidAnswers()
    {
        // Given
        QFETCH(QVariantMap, answer);
        PromptRegistry registry; registry.open(); PromptRequest request; request.timeout = 0ms;
        bool accepted = true;
        // When
        const auto result = registry.ask(request, [&registry, &answer, &accepted](quint32 id, const QString &, const QVariantMap &) {
            accepted = registry.answer(id, answer);
        });
        // Then
        QVERIFY(!accepted); QCOMPARE(result, request.safeDefault);
    }
    void rejectsWrongId()
    {
        // Given
        PromptRegistry registry; registry.open(); PromptRequest request; request.timeout = 0ms;
        bool accepted = true;
        // When
        const auto result = registry.ask(request, [&registry, &accepted](quint32 id, const QString &, const QVariantMap &) {
            accepted = registry.answer(id + 1, {{"action", "abort"}});
        });
        // Then
        QVERIFY(!accepted); QCOMPARE(result, request.safeDefault);
    }
    void returnsDefaultWhenTimedOut()
    {
        // Given
        PromptRegistry registry; registry.open(); PromptRequest request; request.timeout = 0ms;
        // When
        const auto result = registry.ask(request, [](quint32, const QString &, const QVariantMap &) {});
        // Then
        QCOMPARE(result, request.safeDefault);
    }
    void wakesWhenOwnerDisappearsOrOperationCancelled()
    {
        // Given
        PromptRegistry registry; registry.open(); PromptRequest request; request.timeout = 500ms;
        std::promise<void> announced; auto notification = announced.get_future();
        auto result = std::async(std::launch::async, [&] {
            return registry.ask(request, [&](quint32, const QString &, const QVariantMap &) { announced.set_value(); });
        });
        QVERIFY(notification.wait_for(1s) == std::future_status::ready);
        // When
        registry.interrupt();
        // Then
        QVERIFY(result.wait_for(1s) == std::future_status::ready);
        QCOMPARE(result.get(), request.safeDefault);
    }
    void rejectsStaleAnswerAfterNextPrompt()
    {
        // Given
        PromptRegistry registry; registry.open(); PromptRequest request; request.timeout = 0ms;
        quint32 oldId = 0;
        registry.ask(request, [&](quint32 id, const QString &, const QVariantMap &) { oldId = id; });
        bool accepted = true;
        // When
        registry.ask(request, [&](quint32, const QString &, const QVariantMap &) { accepted = registry.answer(oldId, {{"action", "abort"}}); });
        // Then
        QVERIFY(!accepted);
    }
    void defaultsWhenInterruptedBeforePrompt()
    {
        // Given
        PromptRegistry registry; registry.open(); registry.interrupt(); PromptRequest request;
        bool notified = false;
        // When
        const auto result = registry.ask(request, [&](quint32, const QString &, const QVariantMap &) { notified = true; });
        // Then
        QVERIFY(!notified); QCOMPARE(result, request.safeDefault);
    }
    void heldPromptOutlivesTimeoutUntilReleased()
    {
        // Given: 認可待ちで保留した問合せ (期限 50ms)
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
        // When: 期限を過ぎても保留中は既定値で終わらない
        QVERIFY(result.wait_for(200ms) == std::future_status::timeout);
        QVERIFY(!registry.answer(id, {{"action", "abort"}}));
        QVERIFY(registry.release(id, {{"action", "trust"}}));
        // Then: 認可後の回答が反映される
        QVERIFY(result.wait_for(1s) == std::future_status::ready);
        QCOMPARE(result.get().value("action").toString(), "trust");
    }
    void releaseFailsAfterInterrupt()
    {
        // Given: 保留中の問合せ
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
        // When: 取消しで中断された後に認可が完了する
        registry.interrupt();
        QVERIFY(result.wait_for(1s) == std::future_status::ready);
        QCOMPARE(result.get(), request.safeDefault);
        // Then: 遅れた認可結果は反映されず、失敗として扱われる
        QVERIFY(!registry.release(id, {{"action", "trust"}}));
    }
    void holdRejectsInvalidOrDuplicateAnswers()
    {
        // Given
        PromptRegistry registry; registry.open();
        PromptRequest request; request.actions << "trust"; request.timeout = 0ms;
        bool invalidHeld = true, wrongIdHeld = true, secondHeld = true, released = false;
        // When
        registry.ask(request, [&](quint32 id, const QString &, const QVariantMap &) {
            invalidHeld = registry.hold(id, {{"action", "accept-once"}});
            wrongIdHeld = registry.hold(id + 1, {{"action", "trust"}});
            QVERIFY(registry.hold(id, {{"action", "trust"}}));
            secondHeld = registry.hold(id, {{"action", "trust"}});
            released = registry.release(id, {{"action", "abort"}});
        });
        // Then
        QVERIFY(!invalidHeld); QVERIFY(!wrongIdHeld); QVERIFY(!secondHeld); QVERIFY(released);
    }
    void clearsAuthorizationIdOnlyWhenItMatches()
    {
        // Given: 新しい認可要求が始まっている
        QString current = QStringLiteral("new-request");
        // When: 古い要求の完了通知が届く
        clearAuthorizationId(current, QStringLiteral("old-request"));
        // Then: 新しい要求の識別子は残る
        QCOMPARE(current, QStringLiteral("new-request"));
        clearAuthorizationId(current, QString());
        QCOMPARE(current, QStringLiteral("new-request"));
        clearAuthorizationId(current, QStringLiteral("new-request"));
        QVERIFY(current.isEmpty());
    }
};

QTEST_GUILESS_MAIN(PromptRegistryTest)
#include "tst_PromptRegistry.moc"
