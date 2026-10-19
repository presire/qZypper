#ifndef QZYPPER_BACKEND_ZYPPKEYRECEIVER_H
#define QZYPPER_BACKEND_ZYPPKEYRECEIVER_H

#include <cstdint>
#include <functional>
#include <string>
#include <mutex>
#include <set>
#include <zypp/Callback.h>
#include <zypp/KeyRing.h>
#include <zypp/Digest.h>
#include "KeyTrustPrompt.h"

namespace qZypper {

/**
 * @brief 信頼されていないGPG鍵の情報
 *
 * fail-closed動作のため、askUserToAcceptKeyで信頼されなかった鍵の詳細を、UI層 (D-Bus経由) へ通知するための構造体
 */
struct UntrustedKeyInfo {
    std::string fingerprint;   // 正規化済みフィンガープリント (大文字16進数、空白なし)
    std::string id;            // 鍵ID
    std::string name;          // 鍵のユーザID / 名前
    std::int64_t created = 0;  // 作成日時 (エポックタイム)
    std::int64_t expires = 0;  // 有効期限 (エポックタイム、0は無期限)
    std::string repoAlias;     // KeyContextのリポジトリエイリアス (空の場合あり)
    std::string repoName;      // KeyContextのリポジトリ名 (空の場合あり)
};

/**
 * @brief 未信頼鍵通知コールバック型
 * @param info 信頼されなかった鍵の情報
 */
using UntrustedKeyCallbackFn = std::function<void(const UntrustedKeyInfo&)>;

/**
 * @class KeyRingReceiver
 * @brief GPG鍵信頼確認コールバック (fail-closed)
 *
 * 事前承認済みのフィンガープリントは1回の使用で消費し、KEY_TRUST_AND_IMPORTを返す
 * 事前承認がなければ、設定済みの信頼確認コールバックで回答を待機する
 *
 * 一時承認はKEY_TRUST_TEMPORARILY、恒久承認はKEY_TRUST_AND_IMPORTを返す
 * 拒否・未回答時はKEY_DONT_TRUSTを返し、コールバック未設定時は未信頼鍵を通知する
 *
 * 未署名ファイル・検証失敗は全て拒否する
 */
class KeyRingReceiver
    : public zypp::callback::ReceiveReport<zypp::KeyRingReport>
{
public:
    /**
     * @brief フィンガープリントを正規化する
     * @param raw 生フィンガープリント文字列
     * @return ASCII空白を除去し大文字化した結果
     *         40桁または64桁の16進数でなければ""を返す
     */
    static std::string normalizeFingerprint(const std::string &raw);

    /**
     * @brief フィンガープリントを承認済み集合に登録する (mutex保護)
     * @param normalizedFingerprint 正規化済みフィンガープリント
     */
    void approveFingerprint(const std::string &normalizedFingerprint);

    /**
     * @brief 承認済みフィンガープリントを全て破棄する (mutex保護)
     * @note セッション終了時に呼び出し、次のセッションへ承認を持ち越さない
     */
    void clearApprovals();

    /**
     * @brief 未信頼鍵通知コールバックを設定する (mutex保護)
     * @param cb 通知コールバック
     */
    void setUntrustedKeyCallback(UntrustedKeyCallbackFn cb);

    /**
     * @brief 未知鍵のin-flight信頼プロンプトコールバックを設定する (mutex保護)
     * @param cb 回答コールバック (ワーカースレッドで実行)
     *           未設定時は従来動作
     */
    void setKeyTrustPromptCallback(KeyTrustPromptFn cb);

    /**
     * @brief 事前承認または対話回答に応じてGPG鍵の信頼方法を返す
     * @param key 対象の公開鍵
     * @param keycontext 鍵のコンテキスト (リポジトリ情報)
     * @return 事前承認・恒久承認はKEY_TRUST_AND_IMPORT、一時承認はKEY_TRUST_TEMPORARILY、それ以外はKEY_DONT_TRUST
     */
    KeyTrust askUserToAcceptKey(const zypp::PublicKey &key,
                                const zypp::KeyContext &keycontext) override;

    /**
     * @brief 未署名ファイルを受け入れるか — 常に拒否する (fail-closed)
     * @param file 対象ファイル名
     * @param keycontext 鍵のコンテキスト
     * @return 常にfalse
     */
    bool askUserToAcceptUnsignedFile(const std::string &file,
                                     const zypp::KeyContext &keycontext) override;

    /**
     * @brief 未知の鍵を受け入れるか — 常に拒否する (fail-closed)
     * @param file 対象ファイル名
     * @param id 未知の鍵ID
     * @param keycontext 鍵のコンテキスト
     * @return 常にfalse
     */
    bool askUserToAcceptUnknownKey(const std::string &file,
                                   const std::string &id,
                                   const zypp::KeyContext &keycontext) override;

    /**
     * @brief 検証失敗を受け入れるか — 常に拒否する (fail-closed)
     * @param file 対象ファイル名
     * @param key 検証に使われた公開鍵
     * @param keycontext 鍵のコンテキスト
     * @return 常にfalse
     */
    bool askUserToAcceptVerificationFailed(const std::string &file,
                                           const zypp::PublicKey &key,
                                           const zypp::KeyContext &keycontext) override;

private:
    std::mutex m_mutex;                    // 承認済みリストとコールバックの排他制御
    std::set<std::string> m_approved;      // ユーザが承認した正規化済みフィンガープリント
    UntrustedKeyCallbackFn m_untrustedCb;  // 信頼されなかった鍵の通知コールバック
    KeyTrustPromptFn m_keyTrustPrompt;     // 鍵信頼確認プロンプトのコールバック
};

/**
 * @class DigestReceiver
 * @brief ダイジェスト検証コールバック (fail-closed)
 *
 * ダイジェストなし・未知・不一致は全て拒否する
 */
class DigestReceiver
    : public zypp::callback::ReceiveReport<zypp::DigestReport>
{
public:
    /**
     * @brief ダイジェストなしファイルを受け入れるか - 常に拒否する
     * @param file 対象ファイルパス
     * @return 常にfalse
     */
    bool askUserToAcceptNoDigest(const zypp::Pathname &file) override;
    /**
     * @brief 未知のダイジェスト種別を受け入れるか - 常に拒否する
     * @param file 対象ファイルパス
     * @param name ダイジェスト名
     * @return 常にfalse
     */
    bool askUserToAccepUnknownDigest(const zypp::Pathname &file,
                                     const std::string &name) override;
    /**
     * @brief 不一致ダイジェストを受け入れるか - 常に拒否する
     * @param file 対象ファイルパス
     * @param requested 要求されたダイジェスト
     * @param found 実際のダイジェスト
     * @return 常にfalse
     */
    bool askUserToAcceptWrongDigest(const zypp::Pathname &file,
                                    const std::string &requested,
                                    const std::string &found) override;
};

} // namespace qZypper

#endif // QZYPPER_BACKEND_ZYPPKEYRECEIVER_H
