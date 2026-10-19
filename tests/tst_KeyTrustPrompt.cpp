#include <QtTest>
#include "KeyTrustPrompt.h"
#include "ZyppKeyReceiver.h"

using namespace qZypper;

namespace {

/**
 * @brief ボタン定義のリストからaction値だけを取り出す
 * @param buttons KeyTrustPrompt::buttonsが返すボタン定義
 * @return 各ボタンのaction文字列を順に並べたリスト
 */
QStringList actionsOf(const QVariantList &buttons)
{
    QStringList out;
    for (const QVariant &entry : buttons)
        out.append(entry.toMap().value(QStringLiteral("action")).toString());
    return out;
}

} // namespace

/**
 * @class KeyTrustPromptTest
 * @brief 鍵信頼プロンプトの種別文字列・ボタン・回答マッピングを検証する
 *
 * 種別から文字列への変換、GPG鍵向けボタン構成、フェイルセーフな既定回答、既知アクションの判定、回答から決定へのマッピングと未知回答の拒否、
 * 署名問題を常に拒否する受信側の振る舞いを対象にする
 */
class KeyTrustPromptTest : public QObject {
    Q_OBJECT
private slots:
    /** @brief 種別を対応する文字列へ変換することを検証する */
    void mapsTypeStrings();
    /** @brief 種別ごとのボタン構成を組み立てることを検証する */
    void buildsKeyButtons();
    /** @brief 鍵要求が既定で拒否を選ぶフェイルセーフ構成になることを検証する */
    void keyRequestIsFailClosed();
    /** @brief 種別ごとに既知アクションを判定することを検証する */
    void knowsActions();
    /** @brief 回答から鍵信頼の決定へマッピングすることを検証する */
    void mapsKeyAnswers();
    /** @brief 未知の回答を拒否することを検証する */
    void rejectsUnknownAnswers();
    /** @brief 署名問題を常に拒否することを検証する */
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
    QCOMPARE(request.safeDefault.value(QStringLiteral("action")).toString(), QStringLiteral("reject"));
    QCOMPARE(request.actions, actionsOf(KeyTrustPrompt::buttons(KeyTrustPromptKind::GpgKey)));
    const QVariantMap data = request.data;
    QCOMPARE(data.value(QStringLiteral("fingerprint")).toString(), QStringLiteral("DEADBEEF"));
    QCOMPARE(data.value(QStringLiteral("repoAlias")).toString(), QStringLiteral("repo"));
    QVERIFY(data.contains(QStringLiteral("buttons")));
    QVERIFY(data.contains(QStringLiteral("message")));
    QVERIFY(data.contains(QStringLiteral("details")));
}

void KeyTrustPromptTest::knowsActions()
{
    QVERIFY(KeyTrustPrompt::isKnownAction(KeyTrustPromptKind::GpgKey, QStringLiteral("trust-permanent")));
    // 「今回だけ受け入れる」は廃止済みで、どの種別でも受け付けない
    QVERIFY(!KeyTrustPrompt::isKnownAction(KeyTrustPromptKind::GpgKey, QStringLiteral("accept-once")));
}

void KeyTrustPromptTest::mapsKeyAnswers()
{
    const auto temporary = KeyTrustPrompt::decisionFromAnswer({{QStringLiteral("action"), QStringLiteral("trust-temporary")}});
    QVERIFY(temporary.has_value());
    QCOMPARE(*temporary, KeyTrustDecision::TrustTemporarily);
    const auto permanent = KeyTrustPrompt::decisionFromAnswer({{QStringLiteral("action"), QStringLiteral("trust-permanent")}});
    QVERIFY(permanent.has_value());
    QCOMPARE(*permanent, KeyTrustDecision::TrustPermanently);
    const auto reject = KeyTrustPrompt::decisionFromAnswer({{QStringLiteral("action"), QStringLiteral("reject")}});
    QVERIFY(reject.has_value());
    QCOMPARE(*reject, KeyTrustDecision::Reject);
}

void KeyTrustPromptTest::rejectsUnknownAnswers()
{
    QVERIFY(!KeyTrustPrompt::decisionFromAnswer({{QStringLiteral("action"), QStringLiteral("accept-once")}}).has_value());
    QVERIFY(!KeyTrustPrompt::decisionFromAnswer({}).has_value());
}

void KeyTrustPromptTest::alwaysRejectsSignatureProblems()
{
    // 準備 : 鍵信頼プロンプトが常に「恒久的に信頼」と答える受信側
    KeyRingReceiver receiver;
    int prompts = 0;
    receiver.setKeyTrustPromptCallback([&prompts](const KeyTrustAnswerInfo &) {
        ++prompts;
        return std::optional<KeyTrustDecision>(KeyTrustDecision::TrustPermanently);
    });
    const zypp::KeyContext context;

    // 実行・確認 : 未署名・未知の鍵・検証失敗は問い合わせずに拒否する
    QVERIFY(!receiver.askUserToAcceptUnsignedFile("repomd.xml", context));
    QVERIFY(!receiver.askUserToAcceptUnknownKey("repomd.xml", "0123456789ABCDEF", context));
    QVERIFY(!receiver.askUserToAcceptVerificationFailed("repomd.xml", zypp::PublicKey(), context));
    QCOMPARE(prompts, 0);
}

QTEST_GUILESS_MAIN(KeyTrustPromptTest)
#include "tst_KeyTrustPrompt.moc"
