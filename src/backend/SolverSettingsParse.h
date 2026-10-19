#ifndef QZYPPER_BACKEND_SOLVERSETTINGSPARSE_H
#define QZYPPER_BACKEND_SOLVERSETTINGSPARSE_H

#include <QString>
#include <QVariantMap>
#include "ZyppManager.h"

namespace qZypper {

/**
 * @brief ソルバー設定の上書きマップを厳格に検証して解析する
 *
 * 未知のキー、真偽値以外の値、空のマップはすべて拒否する
 * 入力の制御文字・過大長はInputValidation::validateArgumentsで弾く
 *
 * @param settings D-Bus由来の設定マップ
 * @param out 解析結果の格納先 (失敗時は変更しない)
 * @param error 失敗理由 (英語メッセージ)
 * @return 検証成功ならtrue
 */
bool parseSolverSettings(const QVariantMap &settings, ZyppManager::SolverOverrides &out,
                         QString &error);

} // namespace qZypper

#endif // QZYPPER_BACKEND_SOLVERSETTINGSPARSE_H
