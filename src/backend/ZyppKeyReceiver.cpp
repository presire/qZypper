#include <cctype>
#include "ZyppKeyReceiver.h"

namespace qZypper {

// KeyRingReceiver (fail-closed)

/**
 * @brief フィンガープリントを正規化する
 *
 * ASCII空白を除去して大文字化する
 * 結果が40桁または64桁の16進数でなければ""を返す (不正な指紋は承認集合に載せないため)
 *
 * @param raw 生フィンガープリント文字列
 * @return 正規化済みフィンガープリント、または""
 */
std::string KeyRingReceiver::normalizeFingerprint(const std::string &raw)
{
    std::string out;
    out.reserve(raw.size());
    for (unsigned char c : raw) {
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v')
            continue;
        out += static_cast<char>(std::toupper(c));
    }
    if (out.size() != 40 && out.size() != 64)
        return "";
    for (unsigned char c : out) {
        if (!std::isxdigit(c))
            return "";
    }
    return out;
}

/**
 * @brief フィンガープリントを承認済み集合に登録する (mutex保護)
 * @param normalizedFingerprint 正規化済みフィンガープリント
 */
void KeyRingReceiver::approveFingerprint(const std::string &normalizedFingerprint)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_approved.insert(normalizedFingerprint);
}

/** @brief セッション間で署名鍵の承認を引き継がない */
void KeyRingReceiver::clearApprovals()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_approved.clear();
}

/**
 * @brief 未信頼鍵通知コールバックを設定する (mutex保護)
 * @param cb 通知コールバック
 */
void KeyRingReceiver::setUntrustedKeyCallback(UntrustedKeyCallbackFn cb)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_untrustedCb = std::move(cb);
}

/**
 * @brief 未知鍵のin-flight信頼プロンプトコールバックを設定する (mutex保護)
 * @param cb 回答コールバック
 */
void KeyRingReceiver::setKeyTrustPromptCallback(KeyTrustPromptFn cb)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_keyTrustPrompt = std::move(cb);
}

/**
 * @brief 事前承認または対話回答に応じてGPG鍵の信頼方法を返す (fail-closed)
 *
 * 事前承認済みのフィンガープリントは1回の使用で消費し、KEY_TRUST_AND_IMPORTを返す
 * 事前承認がなければ、設定済みの信頼確認コールバックで回答を待機する
 *
 * 一時承認はKEY_TRUST_TEMPORARILY、恒久承認はKEY_TRUST_AND_IMPORTを返す
 *
 * 拒否・未回答時はKEY_DONT_TRUSTを返し、コールバック未設定時はUntrustedKeyInfoを通知する
 *
 * @param key 対象の公開鍵
 * @param keycontext 鍵のコンテキスト (リポジトリ情報)
 * @return 事前承認・恒久承認はKEY_TRUST_AND_IMPORT、一時承認はKEY_TRUST_TEMPORARILY、それ以外はKEY_DONT_TRUST
 */
KeyRingReceiver::KeyTrust KeyRingReceiver::askUserToAcceptKey(const zypp::PublicKey &key, const zypp::KeyContext &keycontext)
{
    const std::string rawFp = key.fingerprint();
    const std::string fp = normalizeFingerprint(rawFp);

    UntrustedKeyCallbackFn cb;
    KeyTrustPromptFn prompt;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (!fp.empty()) {
            auto it = m_approved.find(fp);
            if (it != m_approved.end()) {
                m_approved.erase(it);
                return KEY_TRUST_AND_IMPORT;
            }
        }
        cb = m_untrustedCb;
        prompt = m_keyTrustPrompt;
    }

    KeyTrustAnswerInfo answerInfo;
    answerInfo.fingerprint = fp.empty() ? rawFp : fp;
    answerInfo.id = key.id();
    answerInfo.name = key.name();
    answerInfo.created = static_cast<std::int64_t>(key.created());
    answerInfo.expires = static_cast<std::int64_t>(key.expires());
    answerInfo.repoAlias = keycontext.repoInfo().alias();
    answerInfo.repoName = keycontext.repoInfo().name();

    // in-flightプロンプトが設定されていれば、その場で回答を待つ
    // 未回答・タイムアウト・不正回答はすべて拒否 (fail-closed)
    if (prompt) {
        switch (prompt(answerInfo).value_or(KeyTrustDecision::Reject)) {
        case KeyTrustDecision::TrustTemporarily:
            return KEY_TRUST_TEMPORARILY;
        case KeyTrustDecision::TrustPermanently:
            return KEY_TRUST_AND_IMPORT;
        case KeyTrustDecision::Reject:
        default:
            return KEY_DONT_TRUST;
        }
    }

    UntrustedKeyInfo info;
    info.fingerprint = answerInfo.fingerprint;
    info.id = answerInfo.id;
    info.name = answerInfo.name;
    info.created = answerInfo.created;
    info.expires = answerInfo.expires;
    info.repoAlias = answerInfo.repoAlias;
    info.repoName = answerInfo.repoName;

    qWarning("Refusing to trust GPG key id=%s fingerprint=%s repo=%s (no explicit approval)",
             info.id.c_str(), info.fingerprint.c_str(), info.repoAlias.c_str());

    // ロック外で通知する (デッドロック回避)
    if (cb)
        cb(info);

    return KEY_DONT_TRUST;
}

/**
 * @brief 未署名ファイルを受け入れるか確認する - 常に拒否する
 * @param file 対象ファイル名
 * @param keycontext 鍵のコンテキスト
 * @return 常にfalse (libzyppの安全なデフォルトと同様)
 */
bool KeyRingReceiver::askUserToAcceptUnsignedFile(
    const std::string &file,
    const zypp::KeyContext &/*keycontext*/)
{
    qWarning("Refusing unsigned file: %s", file.c_str());
    return false;
}

/**
 * @brief 未知の鍵IDを受け入れるか確認する - 常に拒否する
 * @param file 対象ファイル名
 * @param id 未知の鍵ID
 * @param keycontext 鍵のコンテキスト
 * @return 常にfalse (libzyppの安全なデフォルトと同様)
 */
bool KeyRingReceiver::askUserToAcceptUnknownKey(
    const std::string &file,
    const std::string &id,
    const zypp::KeyContext &/*keycontext*/)
{
    qWarning("Refusing unknown key id=%s for file: %s", id.c_str(), file.c_str());
    return false;
}

/**
 * @brief 署名検証失敗を受け入れるか確認する - 常に拒否する
 * @param file 対象ファイル名
 * @param key 検証に使われた公開鍵
 * @param keycontext 鍵のコンテキスト
 * @return 常にfalse (libzyppの安全なデフォルトと同様)
 */
bool KeyRingReceiver::askUserToAcceptVerificationFailed(
    const std::string &file,
    const zypp::PublicKey &key,
    const zypp::KeyContext &/*keycontext*/)
{
    qWarning("Refusing file with failed verification: %s key id=%s",
             file.c_str(), key.id().c_str());
    return false;
}

// ─── DigestReceiver (fail-closed) ─────────────────────────

/**
 * @brief ダイジェストなしファイルを受け入れるか - 常に拒否する
 * @param file 対象ファイルパス
 * @return 常にfalse
 */
bool DigestReceiver::askUserToAcceptNoDigest(const zypp::Pathname &file)
{
    qWarning("Refusing file without digest: %s", file.c_str());
    return false;
}

/**
 * @brief 未知のダイジェスト種別を受け入れるか - 常に拒否する
 * @param file 対象ファイルパス
 * @param name ダイジェスト名
 * @return 常にfalse
 */
bool DigestReceiver::askUserToAccepUnknownDigest(
    const zypp::Pathname &file,
    const std::string &name)
{
    qWarning("Refusing file with unknown digest %s: %s", name.c_str(), file.c_str());
    return false;
}

/**
 * @brief 不一致ダイジェストを受け入れるか - 常に拒否する
 * @param file 対象ファイルパス
 * @param requested 要求されたダイジェスト
 * @param found 実際のダイジェスト
 * @return 常にfalse
 */
bool DigestReceiver::askUserToAcceptWrongDigest(
    const zypp::Pathname &file,
    const std::string &/*requested*/,
    const std::string &/*found*/)
{
    qWarning("Refusing file with wrong digest: %s", file.c_str());
    return false;
}

} // namespace qZypper
