#ifndef QZYPPER_BACKEND_KEYTRUSTPROMPT_H
#define QZYPPER_BACKEND_KEYTRUSTPROMPT_H

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <QVariantList>
#include <QVariantMap>
#include <QString>
#include "PromptRegistry.h"

namespace qZypper {

/**
 * @brief GPG 鍵信頼プロンプトの種別。
 *
 * libzypp の KeyRingReport は用途別に複数の問合せを発行する。
 * それぞれを独立した D-Bus プロンプト種別へ写像する。
 */
enum class KeyTrustPromptKind {
    GpgKey,          //!< 未知の署名鍵 (askUserToAcceptKey)
    GpgUnsigned,     //!< 未署名ファイル (askUserToAcceptUnsignedFile / askUserToAcceptUnknownKey)
    GpgVerifyFailed  //!< 署名検証失敗 (askUserToAcceptVerificationFailed)
};

/**
 * @brief ユーザー回答を正規化した信頼判断。
 */
enum class KeyTrustDecision {
    TrustTemporarily,  //!< 今回の操作のみ信頼
    TrustPermanently,  //!< 鍵を恒久的にインポートして信頼
    Reject             //!< 拒否 (fail-closed)
};

/**
 * @brief askUserToAcceptKey が渡す鍵情報。
 *
 * D-Bus へは QVariantMap として渡すが、ワーカー側の橋渡しは
 * この POD 経由で行い libzypp 型を漏らさない。
 */
struct KeyTrustAnswerInfo {
    std::string fingerprint;  //!< 正規化済みフィンガープリント
    std::string id;           //!< 鍵 ID (短縮)
    std::string name;         //!< 鍵の所有者名
    std::int64_t created = 0; //!< 作成時刻 (epoch 秒)
    std::int64_t expires = 0; //!< 失効時刻 (epoch 秒、0 は無期限)
    std::string repoAlias;    //!< 署名元リポジトリのエイリアス
    std::string repoName;     //!< 署名元リポジトリの表示名
};

/** @brief 鍵信頼プロンプトへの回答を返すコールバック (ワーカースレッドで実行)。 */
using KeyTrustPromptFn =
    std::function<std::optional<KeyTrustDecision>(const KeyTrustAnswerInfo &info)>;

/** @brief 未署名/検証失敗プロンプトへの回答を返すコールバック (ワーカースレッドで実行)。 */
using UnsignedFilePromptFn =
    std::function<std::optional<bool>(const std::string &file, KeyTrustPromptKind kind)>;

/**
 * @brief GPG 関連プロンプトの契約 (型名・データキー・ボタン) を一元管理する。
 *
 * 純関数のみで構成し、Qt のイベントループや D-Bus に依存しない。
 * これにより root 権限なしのユニットテストが可能になる。
 */
namespace KeyTrustPrompt {

/**
 * @brief プロンプト種別の D-Bus 型文字列を返す。
 * @param kind プロンプト種別
 * @return "gpg-key" / "gpg-unsigned" / "gpg-verify-failed"
 */
QString typeString(KeyTrustPromptKind kind);

/**
 * @brief 種別ごとのボタン定義を返す。
 *
 * 返す配列は data["buttons"] と同時に PromptRegistry の
 * 許可アクション集合としても使用する。
 * @param kind プロンプト種別
 * @return [{action,label}, ...]
 */
QVariantList buttons(KeyTrustPromptKind kind);

/**
 * @brief 鍵信頼プロンプトの要求を構築する。
 * @param info 鍵情報
 * @return PromptRequest (type="gpg-key", safeDefault=reject)
 */
PromptRequest buildKeyRequest(const KeyTrustAnswerInfo &info);

/**
 * @brief 未署名/検証失敗プロンプトの要求を構築する。
 * @param file 対象ファイル名
 * @param kind プロンプト種別 (GpgUnsigned または GpgVerifyFailed)
 * @return PromptRequest (safeDefault=reject, タイムアウト時も拒否)
 */
PromptRequest buildUnsignedRequest(const std::string &file, KeyTrustPromptKind kind);

/**
 * @brief 指定種別で有効なアクションかを判定する。
 * @param kind プロンプト種別
 * @param action 判定するアクション
 * @return 有効時 true
 */
bool isKnownAction(KeyTrustPromptKind kind, const QString &action);

/**
 * @brief 回答マップを信頼判断へ写像する。
 * @param answer 回答 ({action:...})
 * @return 有効な判断、または不正回答の場合 nullopt
 */
std::optional<KeyTrustDecision> decisionFromAnswer(const QVariantMap &answer);

/**
 * @brief 回答マップを未署名許否へ写像する。
 * @param kind プロンプト種別
 * @param answer 回答 ({action:...})
 * @return 許可=true/拒否=false、それ以外は nullopt
 */
std::optional<bool> unsignedFromAnswer(KeyTrustPromptKind kind, const QVariantMap &answer);

} // namespace KeyTrustPrompt

} // namespace qZypper

#endif // QZYPPER_BACKEND_KEYTRUSTPROMPT_H
