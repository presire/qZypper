#include <QVariantMap>
#include "InputValidation.h"

namespace qZypper {
namespace {
/**
 * @brief 文字列がサイズ上限内で制御文字を含まないか検証する
 * @param value 検証する文字列
 * @return 有効な文字列ならtrue
 */
bool validString(const QString &value)
{
    if (value.toUtf8().size() > 8192) return false;
    for (const auto character : value) {
        if (character.unicode() < 0x20 || character.unicode() == 0x7f) return false;
    }
    return true;
}
/**
 * @brief QVariant値が許可済み文字列制約を満たすか検証する
 * @param value 検証する値
 * @return 値が安全に転送できる場合はtrue
 */
bool validValue(const QVariant &value)
{
    if (value.typeId() == QMetaType::QString) return validString(value.toString());
    return true;
}
}
/**
 * @brief .repo本文のみに適用する改行許可の入力制約を検証する
 * @param arguments D-Busで受け取った引数列
 * @param error 失敗時の理由の出力先
 * @return 1個の安全な本文を受け取った場合はtrue
 */
bool validateRepoFileContent(const QVariantList &arguments, QString &error)
{
    if (arguments.size() != 1 || arguments.first().typeId() != QMetaType::QString) {
        error = QStringLiteral("Expected a single string argument");
        return false;
    }

    const QString content = arguments.first().toString();
    if (static_cast<std::size_t>(content.toUtf8().size()) > kMaxRepoFileContentBytes) {
        error = QStringLiteral("The .repo file exceeds the size limit");
        return false;
    }

    for (const auto character : content) {
        const char16_t code = character.unicode();
        if ((code < 0x20 && code != u'\n' && code != u'\r' && code != u'\t') || code == 0x7f) {
            error = QStringLiteral("The .repo file contains control characters");
            return false;
        }
    }
    return true;
}

/**
 * @brief D-Bus引数列の個数、文字列、属性マップを検証する
 * @param arguments 検証する引数列
 * @param error 失敗時の理由の出力先
 * @return 全引数が共通制約を満たす場合はtrue
 */
bool validateArguments(const QVariantList &arguments, QString &error)
{
    if (arguments.size() > 16) {
        error = QStringLiteral("Too many arguments");
        return false;
    }
    for (const auto &argument : arguments) {
        if (!validValue(argument)) {
            error = QStringLiteral("Argument contains control characters or exceeds the size limit");
            return false;
        }
        if (argument.typeId() != QMetaType::QVariantMap) continue;
        const auto properties = argument.toMap();
        if (properties.size() > 32) {
            error = QStringLiteral("Too many properties");
            return false;
        }
        for (auto property = properties.cbegin(); property != properties.cend(); ++property) {
            if (!validString(property.key()) || !validValue(property.value())) {
                error = QStringLiteral("Property contains control characters or exceeds the size limit");
                return false;
            }
        }
    }
    return true;
}
}
