#include "KeyTrustPrompt.h"

namespace qZypper {
namespace KeyTrustPrompt {

/**
 * @brief プロンプト種別の D-Bus 型文字列を返す。
 * @param kind プロンプト種別
 * @return 型文字列
 */
QString typeString(KeyTrustPromptKind kind)
{
    switch (kind) {
    case KeyTrustPromptKind::GpgKey:         return QStringLiteral("gpg-key");
    case KeyTrustPromptKind::GpgUnsigned:    return QStringLiteral("gpg-unsigned");
    case KeyTrustPromptKind::GpgVerifyFailed: return QStringLiteral("gpg-verify-failed");
    }
    return QStringLiteral("gpg-unsigned");
}

/**
 * @brief 種別ごとのボタン定義を返す。
 * @param kind プロンプト種別
 * @return ボタン配列
 */
QVariantList buttons(KeyTrustPromptKind kind)
{
    if (kind == KeyTrustPromptKind::GpgKey) {
        return {
            QVariantMap{{QStringLiteral("action"), QStringLiteral("trust-temporary")},
                        {QStringLiteral("label"), QStringLiteral("Trust Temporarily")}},
            QVariantMap{{QStringLiteral("action"), QStringLiteral("trust-permanent")},
                        {QStringLiteral("label"), QStringLiteral("Trust Always")}},
            QVariantMap{{QStringLiteral("action"), QStringLiteral("reject")},
                        {QStringLiteral("label"), QStringLiteral("Reject")}},
        };
    }
    // 未署名/検証失敗は fail-closed: 既定・タイムアウトとも拒否。
    return {
        QVariantMap{{QStringLiteral("action"), QStringLiteral("reject")},
                    {QStringLiteral("label"), QStringLiteral("Abort")}},
        QVariantMap{{QStringLiteral("action"), QStringLiteral("accept-once")},
                    {QStringLiteral("label"), QStringLiteral("Ignore and Continue")}},
    };
}

/**
 * @brief 種別に対応する許可アクション一覧を返す。
 * @param kind プロンプト種別
 * @return QStringList
 */
static QStringList actionList(KeyTrustPromptKind kind)
{
    QStringList actions;
    const QVariantList defs = buttons(kind);
    for (const auto &b : defs)
        actions << b.toMap().value(QStringLiteral("action")).toString();
    return actions;
}

/**
 * @brief 種別に対応する安全な既定回答を返す (常に拒否)。
 * @return QVariantMap
 */
static QVariantMap rejectDefault()
{
    return QVariantMap{{QStringLiteral("action"), QStringLiteral("reject")}};
}

/**
 * @brief 鍵信頼プロンプトの要求を構築する。
 * @param info 鍵情報
 * @return PromptRequest
 */
PromptRequest buildKeyRequest(const KeyTrustAnswerInfo &info)
{
    PromptRequest request;
    request.type = typeString(KeyTrustPromptKind::GpgKey);
    request.actions = actionList(KeyTrustPromptKind::GpgKey);
    request.safeDefault = rejectDefault();
    request.timeout = std::chrono::milliseconds(60000);

    const QString repo = info.repoName.empty()
        ? QString::fromStdString(info.repoAlias) : QString::fromStdString(info.repoName);

    request.data = QVariantMap{
        {QStringLiteral("message"),
         QStringLiteral("The repository \"%1\" is signed with an unknown GPG key.").arg(repo)},
        {QStringLiteral("details"),
         QStringLiteral("Key ID: %1\nFingerprint: %2\nOwner: %3\nCreated: %4\nExpires: %5")
             .arg(QString::fromStdString(info.id), QString::fromStdString(info.fingerprint),
                  QString::fromStdString(info.name), QString::number(info.created),
                  info.expires == 0 ? QStringLiteral("never") : QString::number(info.expires))},
        {QStringLiteral("fingerprint"), QString::fromStdString(info.fingerprint)},
        {QStringLiteral("id"), QString::fromStdString(info.id)},
        {QStringLiteral("name"), QString::fromStdString(info.name)},
        {QStringLiteral("created"), QVariant::fromValue<qlonglong>(info.created)},
        {QStringLiteral("expires"), QVariant::fromValue<qlonglong>(info.expires)},
        {QStringLiteral("repoAlias"), QString::fromStdString(info.repoAlias)},
        {QStringLiteral("repoName"), QString::fromStdString(info.repoName)},
        {QStringLiteral("buttons"), buttons(KeyTrustPromptKind::GpgKey)},
    };
    return request;
}

/**
 * @brief 未署名/検証失敗プロンプトの要求を構築する。
 * @param file 対象ファイル名
 * @param kind プロンプト種別
 * @return PromptRequest
 */
PromptRequest buildUnsignedRequest(const std::string &file, KeyTrustPromptKind kind)
{
    if (kind == KeyTrustPromptKind::GpgKey)
        kind = KeyTrustPromptKind::GpgUnsigned;

    PromptRequest request;
    request.type = typeString(kind);
    request.actions = actionList(kind);
    request.safeDefault = rejectDefault();
    request.timeout = std::chrono::milliseconds(60000);

    const QString fileName = QString::fromStdString(file);
    const QString message = kind == KeyTrustPromptKind::GpgVerifyFailed
        ? QStringLiteral("Signature verification failed for \"%1\".").arg(fileName)
        : QStringLiteral("\"%1\" is not signed.").arg(fileName);

    request.data = QVariantMap{
        {QStringLiteral("message"), message},
        {QStringLiteral("details"),
         QStringLiteral("The signature could not be verified with a trusted key. "
                        "Continuing is insecure and is not recommended.")},
        {QStringLiteral("file"), fileName},
        {QStringLiteral("buttons"), buttons(kind)},
    };
    return request;
}

/**
 * @brief 指定種別で有効なアクションかを判定する。
 * @param kind プロンプト種別
 * @param action 判定するアクション
 * @return 有効時 true
 */
bool isKnownAction(KeyTrustPromptKind kind, const QString &action)
{
    return actionList(kind).contains(action);
}

/**
 * @brief 回答マップを信頼判断へ写像する。
 * @param answer 回答
 * @return 判断、または不正時 nullopt
 */
std::optional<KeyTrustDecision> decisionFromAnswer(const QVariantMap &answer)
{
    const QString action = answer.value(QStringLiteral("action")).toString();
    if (action == QStringLiteral("trust-temporary"))
        return KeyTrustDecision::TrustTemporarily;
    if (action == QStringLiteral("trust-permanent"))
        return KeyTrustDecision::TrustPermanently;
    if (action == QStringLiteral("reject"))
        return KeyTrustDecision::Reject;
    return std::nullopt;
}

/**
 * @brief 回答マップを未署名許否へ写像する。
 * @param kind プロンプト種別
 * @param answer 回答
 * @return 許可/拒否、または不正時 nullopt
 */
std::optional<bool> unsignedFromAnswer(KeyTrustPromptKind kind, const QVariantMap &answer)
{
    if (kind == KeyTrustPromptKind::GpgKey)
        kind = KeyTrustPromptKind::GpgUnsigned;
    const QString action = answer.value(QStringLiteral("action")).toString();
    if (action == QStringLiteral("accept-once"))
        return true;
    if (action == QStringLiteral("reject"))
        return false;
    return std::nullopt;
}

} // namespace KeyTrustPrompt
} // namespace qZypper
