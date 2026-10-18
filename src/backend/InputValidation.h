#ifndef QZYPPER_BACKEND_INPUTVALIDATION_H
#define QZYPPER_BACKEND_INPUTVALIDATION_H
#include <QVariantList>
#include <cstddef>
namespace qZypper {
/** @brief .repo 本文 (ImportRepoFile / ParseRepoFile) の最大サイズ */
constexpr std::size_t kMaxRepoFileContentBytes = 64 * 1024;
/** @brief D-Bus入力の大きさと制御文字を境界で検査する。型固有の検査は既存の解析器が行う。 */
bool validateArguments(const QVariantList &arguments, QString &error);
/** @brief .repo 本文を検査する。改行・タブは許可し、サイズ上限と他の制御文字を拒否する。 */
bool validateRepoFileContent(const QVariantList &arguments, QString &error);
}
#endif
