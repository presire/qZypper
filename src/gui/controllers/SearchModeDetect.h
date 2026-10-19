/**
 * @file SearchModeDetect.h
 * @brief 検索欄の入力文字列から検索モードを自動判定する純関数の宣言
 *
 * 入力された文字列を解析し、none / exact / regex / glob / substring のいずれかのモード名と
 * 実際に検索へ渡す問い合わせ文字列を返す
 * バックエンドやlibzyppには依存せず、GUI側の入力補助として単独で利用できる
 *
 * 入力 : ユーザーが検索欄へ入力した文字列
 * 出力 : 判定したモード名と問い合わせ文字列 (DetectedSearch)
 * 依存 : Qt Core (QString) のみ
 */
#ifndef QZYPPER_GUI_CONTROLLERS_SEARCHMODEDETECT_H
#define QZYPPER_GUI_CONTROLLERS_SEARCHMODEDETECT_H

#include <QString>

namespace qZypper {

/**
 * @brief 検索モードの判定結果
 */
struct DetectedSearch {
    QString mode;   // 検索モード名 (none / exact / regex / glob / substring)
    QString query;  // 検索へ渡す問い合わせ文字列
};

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
DetectedSearch detectSearchMode(const QString &text);

} // namespace qZypper

#endif // QZYPPER_GUI_CONTROLLERS_SEARCHMODEDETECT_H
