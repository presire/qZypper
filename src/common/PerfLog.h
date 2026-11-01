#ifndef QZYPPER_PERFLOG_H
#define QZYPPER_PERFLOG_H

#include <QLoggingCategory>

namespace qZypper {

/**
 * @brief 性能計測用のログカテゴリ
 *
 * 既定ではdebug出力を行わず、環境変数QT_LOGGING_RULES = "qzypper.perf.debug=true"で有効化する
 * infoレベルは既定で有効とし、debugレベルは明示的な設定時のみ有効にする
 *
 * @return 共有されるqzypper.perfカテゴリ
 */
inline const QLoggingCategory &lcPerf()
{
    static const QLoggingCategory category("qzypper.perf", QtInfoMsg);
    return category;
}

} // namespace qZypper

#endif // QZYPPER_PERFLOG_H
