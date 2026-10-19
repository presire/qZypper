#ifndef QZYPPER_BACKEND_ZYPPLOGGING_H
#define QZYPPER_BACKEND_ZYPPLOGGING_H

#include <QLoggingCategory>

namespace qZypper {

/**
 * @brief libzypp連携処理のログカテゴリ
 * @note 定義はZyppManager.cppにあり、環境変数QT_LOGGING_RULES = "qzypper.zypp.debug=true"で有効化する
 */
Q_DECLARE_LOGGING_CATEGORY(lcZypp)

} // namespace qZypper

#endif // QZYPPER_BACKEND_ZYPPLOGGING_H
