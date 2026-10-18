#include "KeyTrustPrompt.h"
#include "ZyppCallbackReceiver.h"

#include <QtTest>

using namespace qZypper;

namespace {

QStringList actionsOf(const QVariantList &buttons)
{
    QStringList out;
    for (const QVariant &entry : buttons)
        out.append(entry.toMap().value(QStringLiteral("action")).toString());
    return out;
}

} // namespace

class KeyTrustPromptTest : public QObject {
    Q_OBJECT
private slots:
    void mapsTypeStrings();
    void buildsKeyButtons();
    void keyRequestIsFailClosed();
    void knowsActions();
    void mapsKeyAnswers();
    void rejectsUnknownAnswers();
    void alwaysRejectsSignatureProblems();
};

void KeyTrustPromptTest::mapsTypeStrings()
{
    QCOMPARE(KeyTrustPrompt::typeString(KeyTrustPromptKind::GpgKey), QStringLiteral("gpg-key"));
}

void KeyTrustPromptTest::buildsKeyButtons()
{
    const QStringList actions = actionsOf(KeyTrustPrompt::buttons(KeyTrustPromptKind::GpgKey));
    QCOMPARE(actions,
             QStringList({QStringLiteral("trust-temporary"), QStringLiteral("trust-permanent"),
                          QStringLiteral("reject")}));
}

void KeyTrustPromptTest::keyRequestIsFailClosed()
{
    KeyTrustAnswerInfo info;
    info.fingerprint = "DEADBEEF";
    info.id = "0xDEADBEEF";
    info.name = "Test Key";
    info.created = 1000;
    info.expires = 2000;
    info.repoAlias = "repo";
    info.repoName = "Repo";
    const PromptRequest request = KeyTrustPrompt::buildKeyRequest(info);
    QCOMPARE(request.type, QStringLiteral("gpg-key"));
    QCOMPARE(request.safeDefault.value(QStringLiteral("action")).toString(),
             QStringLiteral("reject"));
    QCOMPARE(request.actions,
             actionsOf(KeyTrustPrompt::buttons(KeyTrustPromptKind::GpgKey)));
    const QVariantMap data = request.data;
    QCOMPARE(data.value(QStringLiteral("fingerprint")).toString(), QStringLiteral("DEADBEEF"));
    QCOMPARE(data.value(QStringLiteral("repoAlias")).toString(), QStringLiteral("repo"));
    QVERIFY(data.contains(QStringLiteral("buttons")));
    QVERIFY(data.contains(QStringLiteral("message")));
    QVERIFY(data.contains(QStringLiteral("details")));
}

void KeyTrustPromptTest::knowsActions()
{
    QVERIFY(KeyTrustPrompt::isKnownAction(KeyTrustPromptKind::GpgKey,
                                          QStringLiteral("trust-permanent")));
    // 「今回だけ受け入れる」は廃止済みで、どの種別でも受け付けない。
    QVERIFY(!KeyTrustPrompt::isKnownAction(KeyTrustPromptKind::GpgKey,
                                           QStringLiteral("accept-once")));
}

void KeyTrustPromptTest::mapsKeyAnswers()
{
    const auto temporary =
        KeyTrustPrompt::decisionFromAnswer({{QStringLiteral("action"), QStringLiteral("trust-temporary")}});
    QVERIFY(temporary.has_value());
    QCOMPARE(*temporary, KeyTrustDecision::TrustTemporarily);
    const auto permanent =
        KeyTrustPrompt::decisionFromAnswer({{QStringLiteral("action"), QStringLiteral("trust-permanent")}});
    QVERIFY(permanent.has_value());
    QCOMPARE(*permanent, KeyTrustDecision::TrustPermanently);
    const auto reject =
        KeyTrustPrompt::decisionFromAnswer({{QStringLiteral("action"), QStringLiteral("reject")}});
    QVERIFY(reject.has_value());
    QCOMPARE(*reject, KeyTrustDecision::Reject);
}

void KeyTrustPromptTest::rejectsUnknownAnswers()
{
    QVERIFY(!KeyTrustPrompt::decisionFromAnswer({{QStringLiteral("action"), QStringLiteral("accept-once")}})
                 .has_value());
    QVERIFY(!KeyTrustPrompt::decisionFromAnswer({}).has_value());
}

void KeyTrustPromptTest::alwaysRejectsSignatureProblems()
{
    // Given: 鍵信頼プロンプトが常に「恒久的に信頼」と答える受信側
    KeyRingReceiver receiver;
    int prompts = 0;
    receiver.setKeyTrustPromptCallback([&prompts](const KeyTrustAnswerInfo &) {
        ++prompts;
        return std::optional<KeyTrustDecision>(KeyTrustDecision::TrustPermanently);
    });
    const zypp::KeyContext context;

    // When / Then: 未署名・未知の鍵・検証失敗は問い合わせずに拒否する
    QVERIFY(!receiver.askUserToAcceptUnsignedFile("repomd.xml", context));
    QVERIFY(!receiver.askUserToAcceptUnknownKey("repomd.xml", "0123456789ABCDEF", context));
    QVERIFY(!receiver.askUserToAcceptVerificationFailed("repomd.xml", zypp::PublicKey(), context));
    QCOMPARE(prompts, 0);
}

QTEST_GUILESS_MAIN(KeyTrustPromptTest)
#include "tst_KeyTrustPrompt.moc"
