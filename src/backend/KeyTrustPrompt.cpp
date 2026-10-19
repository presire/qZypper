#include "KeyTrustPrompt.h"

namespace qZypper {
    namespace KeyTrustPrompt {
        /**
        * @brief プロンプト種別のD-Bus型文字列を返す
        * @param kind プロンプト種別
        * @return 型文字列
        */
        QString typeString(KeyTrustPromptKind kind)
        {
            switch (kind) {
            case KeyTrustPromptKind::GpgKey: return QStringLiteral("gpg-key");
            }
            return QStringLiteral("gpg-key");
        }

       /**
        * @brief 種別ごとのボタン定義を返す
        * @param kind プロンプト種別
        * @return ボタン配列
        */
        QVariantList buttons(KeyTrustPromptKind kind)
        {
            Q_UNUSED(kind)
            // 未署名・未知の鍵・検証失敗は問い合わせずに常に拒否するため、鍵信頼のボタンだけを持つ
            return {
                QVariantMap{{QStringLiteral("action"), QStringLiteral("trust-temporary")},
                            {QStringLiteral("label"), QStringLiteral("Trust Temporarily")}},
                QVariantMap{{QStringLiteral("action"), QStringLiteral("trust-permanent")},
                            {QStringLiteral("label"), QStringLiteral("Trust Always")}},
                QVariantMap{{QStringLiteral("action"), QStringLiteral("reject")},
                            {QStringLiteral("label"), QStringLiteral("Reject")}},
            };
        }

        /**
        * @brief 種別に対応する許可アクション一覧を返す
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
        * @brief 種別に対応する安全な既定回答を返す (常に拒否)
        * @return QVariantMap
        */
        static QVariantMap rejectDefault()
        {
            return QVariantMap{{QStringLiteral("action"), QStringLiteral("reject")}};
        }

        /**
        * @brief 鍵信頼プロンプトの要求を構築する
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
        * @brief 指定種別で有効なアクションかを判定する
        * @param kind プロンプト種別
        * @param action 判定するアクション
        * @return 有効時はtrue
        */
        bool isKnownAction(KeyTrustPromptKind kind, const QString &action)
        {
            return actionList(kind).contains(action);
        }

        /**
        * @brief 回答マップを信頼判断へ写像する
        * @param answer 回答
        * @return 判断、または不正時nullopt
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
    } // namespace KeyTrustPrompt
} // namespace qZypper
