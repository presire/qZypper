/**
 * @file SearchModeDetect.cpp
 * @brief 検索欄の入力文字列から検索モードを自動判定する純関数の実装
 *
 * 入力の前後空白を除いたうえで、=による完全一致、正規表現の特殊文字、ワイルドカードを
 * この順に判定し、該当しない場合は部分一致とする
 * GUIの検索欄から呼び出され、バックエンドやlibzyppには依存しない
 *
 * 入力 : ユーザーが検索欄へ入力した文字列
 * 出力 : 判定したモード名と問い合わせ文字列 (DetectedSearch)
 * 依存 : Qt Core (QString) のみ
 */
#include "SearchModeDetect.h"

namespace qZypper {

namespace {

/**
 * @brief 正規表現として扱うべき特殊文字を含むか判定する
 *
 * 文字クラスや選択、グループ、量指定子の特殊文字と、任意文字の繰り返しを表す
 * .*や.+、エスケープのためのバックスラッシュを検出する
 * +単体は検出しないため、libstdc++やg++のような名前を正規表現と誤判定しない
 *
 * @param text 判定する文字列
 * @return 正規表現の特殊文字を含む場合true
 */
bool containsRegexSpecial(const QString &text)
{
    const QString specials = QStringLiteral("^$()|[]{}");
    for (const QChar ch : text) {
        if (specials.contains(ch)) {
            return true;
        }
    }

    if (text.contains(QStringLiteral(".*"))) {
        return true;
    }

    if (text.contains(QStringLiteral(".+"))) {
        return true;
    }

    if (text.contains(QLatin1Char('\\'))) {
        return true;
    }

    return false;
}

} // namespace

/**
 * @brief 検索欄の入力文字列から検索モードと問い合わせ文字列を判定する
 *
 * 先頭の=は完全一致、正規表現の特殊文字や.*や.+はregex、*や?はglobとして扱い
 * それ以外は部分一致とする
 * 正規表現の特殊文字に+は含めないため、libstdc++やg++のような名前は部分一致のまま扱う
 *
 * 副作用はなく、同じ入力に対して常に同じ結果を返す
 *
 * @param text ユーザーが検索欄へ入力した文字列
 * @return 判定したモード名と問い合わせ文字列 (空入力ではmodeがnone、queryが空)
 */
DetectedSearch detectSearchMode(const QString &text)
{
    const QString t = text.trimmed();
    // 空入力は検索を行わないため、noneを返して早期に終える
    if (t.isEmpty()) {
        return {QStringLiteral("none"), QString()};
    }

    if (t.startsWith(QLatin1Char('='))) {
        const QString rest = t.mid(1).trimmed();
        // 空の問い合わせでも完全一致の意思表示として扱い、noneにはしない
        return {QStringLiteral("exact"), rest};
    }

    // 正規表現の特殊文字はglobより先に判定し、*を量指定子として扱う入力をregexへ振り分ける
    if (containsRegexSpecial(t)) {
        return {QStringLiteral("regex"), t};
    }

    if (t.contains(QLatin1Char('*')) || t.contains(QLatin1Char('?'))) {
        return {QStringLiteral("glob"), t};
    }

    return {QStringLiteral("substring"), t};
}

} // namespace qZypper
