#include "KeyTrustPrompt.h"

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
    void buildsUnsignedButtons();
    void keyRequestIsFailClosed();
    void unsignedRequestIsFailClosed();
    void knowsActions();
    void mapsKeyAnswers();
    void mapsUnsignedAnswers();
    void rejectsUnknownAnswers();
};

void KeyTrustPromptTest::mapsTypeStrings()
{
    QCOMPARE(KeyTrustPrompt::typeString(KeyTrustPromptKind::GpgKey), QStringLiteral("gpg-key"));
    QCOMPARE(KeyTrustPrompt::typeString(KeyTrustPromptKind::GpgUnsigned),
             QStringLiteral("gpg-unsigned"));
    QCOMPARE(KeyTrustPrompt::typeString(KeyTrustPromptKind::GpgVerifyFailed),
             QStringLiteral("gpg-verify-failed"));
}

void KeyTrustPromptTest::buildsKeyButtons()
{
    const QStringList actions = actionsOf(KeyTrustPrompt::buttons(KeyTrustPromptKind::GpgKey));
    QCOMPARE(actions,
             QStringList({QStringLiteral("trust-temporary"), QStringLiteral("trust-permanent"),
                          QStringLiteral("reject")}));
}

void KeyTrustPromptTest::buildsUnsignedButtons()
{
    const QStringList actions = actionsOf(KeyTrustPrompt::buttons(KeyTrustPromptKind::GpgUnsigned));
    QCOMPARE(actions, QStringList({QStringLiteral("reject"), QStringLiteral("accept-once")}));
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

void KeyTrustPromptTest::unsignedRequestIsFailClosed()
{
    const PromptRequest request =
        KeyTrustPrompt::buildUnsignedRequest("file.rpm", KeyTrustPromptKind::GpgVerifyFailed);
    QCOMPARE(request.type, QStringLiteral("gpg-verify-failed"));
    QCOMPARE(request.safeDefault.value(QStringLiteral("action")).toString(),
             QStringLiteral("reject"));
    QCOMPARE(request.actions,
             actionsOf(KeyTrustPrompt::buttons(KeyTrustPromptKind::GpgVerifyFailed)));
    QCOMPARE(request.data.value(QStringLiteral("file")).toString(), QStringLiteral("file.rpm"));
}

void KeyTrustPromptTest::knowsActions()
{
    QVERIFY(KeyTrustPrompt::isKnownAction(KeyTrustPromptKind::GpgKey,
                                          QStringLiteral("trust-permanent")));
    QVERIFY(KeyTrustPrompt::isKnownAction(KeyTrustPromptKind::GpgUnsigned,
                                          QStringLiteral("accept-once")));
    QVERIFY(!KeyTrustPrompt::isKnownAction(KeyTrustPromptKind::GpgUnsigned,
                                           QStringLiteral("trust-permanent")));
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

void KeyTrustPromptTest::mapsUnsignedAnswers()
{
    const auto accept = KeyTrustPrompt::unsignedFromAnswer(
        KeyTrustPromptKind::GpgUnsigned, {{QStringLiteral("action"), QStringLiteral("accept-once")}});
    QVERIFY(accept.has_value());
    QCOMPARE(*accept, true);
    const auto reject = KeyTrustPrompt::unsignedFromAnswer(
        KeyTrustPromptKind::GpgVerifyFailed, {{QStringLiteral("action"), QStringLiteral("reject")}});
    QVERIFY(reject.has_value());
    QCOMPARE(*reject, false);
}

void KeyTrustPromptTest::rejectsUnknownAnswers()
{
    QVERIFY(!KeyTrustPrompt::decisionFromAnswer({{QStringLiteral("action"), QStringLiteral("accept-once")}})
                 .has_value());
    QVERIFY(!KeyTrustPrompt::decisionFromAnswer({}).has_value());
    QVERIFY(!KeyTrustPrompt::unsignedFromAnswer(KeyTrustPromptKind::GpgUnsigned,
                                                {{QStringLiteral("action"), QStringLiteral("trust-permanent")}})
                 .has_value());
}

QTEST_GUILESS_MAIN(KeyTrustPromptTest)
#include "tst_KeyTrustPrompt.moc"
