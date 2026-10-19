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
 * @brief GPG鍵信頼プロンプトの種別
 *
 * 利用者へ問い合わせるのは署名鍵の信頼 (askUserToAcceptKey) だけである
 * 未署名ファイル・未知の鍵・署名検証失敗は問い合わせずに常に拒否する
 */
enum class KeyTrustPromptKind {
    GpgKey  // 未知の署名鍵 (askUserToAcceptKey)
};

/**
 * @brief ユーザ回答を正規化した信頼判断
 */
enum class KeyTrustDecision {
    TrustTemporarily,  // 今回の操作のみ信頼
    TrustPermanently,  // 鍵を恒久的にインポートして信頼
    Reject             // 拒否 (fail-closed)
};

/**
 * @brief askUserToAcceptKeyが渡す鍵情報
 *
 * D-BusへはQVariantMapとして渡すが、ワーカー側の橋渡しは
 * このPOD経由で行いlibzypp型を漏らさない
 */
struct KeyTrustAnswerInfo {
    std::string fingerprint;   // 正規化済みフィンガープリント
    std::string id;            // 鍵ID (短縮)
    std::string name;          // 鍵の所有者名
    std::int64_t created = 0;  // 作成時刻 (エポックタイム)
    std::int64_t expires = 0;  // 失効時刻 (エポックタイム、0は無期限)
    std::string repoAlias;     // 署名元リポジトリのエイリアス
    std::string repoName;      // 署名元リポジトリの表示名
};

/** @brief 鍵信頼プロンプトへの回答を返すコールバック (ワーカースレッドで実行) */
using KeyTrustPromptFn =
    std::function<std::optional<KeyTrustDecision>(const KeyTrustAnswerInfo &info)>;

/**
 * @brief GPG関連プロンプトの契約 (型名・データキー・ボタン) を一元管理する
 *
 * 純関数のみで構成し、QtのイベントループやD-Busに依存しない
 * これにより、root権限なしのユニットテストが可能になる
 */
namespace KeyTrustPrompt {

/**
 * @brief プロンプト種別のD-Bus型文字列を返す
 * @param kind プロンプト種別
 * @return "gpg-key"
 */
QString typeString(KeyTrustPromptKind kind);

/**
 * @brief 種別ごとのボタン定義を返す
 *
 * 返す配列はdata["buttons"]と同時にPromptRegistryの許可アクション集合としても使用する
 *
 * @param kind プロンプト種別
 * @return [{action,label}, ...]
 */
QVariantList buttons(KeyTrustPromptKind kind);

/**
 * @brief 鍵信頼プロンプトの要求を構築する
 * @param info 鍵情報
 * @return PromptRequest (type = "gpg-key", safeDefault = reject)
 */
PromptRequest buildKeyRequest(const KeyTrustAnswerInfo &info);

/**
 * @brief 指定種別で有効なアクションかを判定する
 * @param kind プロンプト種別
 * @param action 判定するアクション
 * @return 有効時true
 */
bool isKnownAction(KeyTrustPromptKind kind, const QString &action);

/**
 * @brief 回答マップを信頼判断へ写像する
 * @param answer 回答 ({action:...})
 * @return 有効な判断、または不正回答の場合はnullopt
 */
std::optional<KeyTrustDecision> decisionFromAnswer(const QVariantMap &answer);

} // namespace KeyTrustPrompt

} // namespace qZypper

#endif // QZYPPER_BACKEND_KEYTRUSTPROMPT_H
