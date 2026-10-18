#include "InputValidation.h"
#include <QVariantMap>

namespace qZypper {
namespace {
bool validString(const QString &value)
{
    if (value.toUtf8().size() > 8192) return false;
    for (const auto character : value) {
        if (character.unicode() < 0x20 || character.unicode() == 0x7f) return false;
    }
    return true;
}
bool validValue(const QVariant &value)
{
    if (value.typeId() == QMetaType::QString) return validString(value.toString());
    return true;
}
}
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
