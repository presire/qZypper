#include "SearchOptions.h"

#include "InputValidation.h"

#include <QRegularExpression>

namespace qZypper {

namespace {

constexpr int kMaxSearchFlags = 127; ///< SearchFlag に定義された全ビットの和
constexpr int kMaxQueryBytes = 256;  ///< 検索文字列の最大バイト数

/**
 * @brief 整数として扱える型かどうかを判定する。
 * @param value 判定対象
 * @return D-Bus 由来の整数型なら true
 */
bool isIntegerType(const QVariant &value)
{
    switch (value.typeId()) {
        case QMetaType::Int:
        case QMetaType::UInt:
        case QMetaType::LongLong:
        case QMetaType::ULongLong:
            return true;
        default:
            return false;
    }
}

/**
 * @brief 非負の整数として取り出す。
 * @param value 判定対象
 * @param out 取り出し結果
 * @return 非負整数なら true
 */
bool toNonNegative(const QVariant &value, unsigned int &out)
{
    if (!isIntegerType(value))
        return false;
    bool ok = false;
    const qlonglong v = value.toLongLong(&ok);
    if (!ok || v < 0)
        return false;
    out = static_cast<unsigned int>(v);
    return true;
}

} // namespace

bool parseSearchOptions(const QVariantMap &options, SearchOptions &out, QString &error)
{
    // 制御文字・過大な文字列長などは既存の共通検証で弾く。
    QString argError;
    if (!validateArguments(QVariantList{options}, argError)) {
        error = argError;
        return false;
    }

    static const QStringList kAllowedKeys = {
        QStringLiteral("query"), QStringLiteral("flags"), QStringLiteral("match"),
        QStringLiteral("offset"), QStringLiteral("limit"),
        QStringLiteral("installedOnly"), QStringLiteral("repoAlias")
    };

    for (auto it = options.constBegin(); it != options.constEnd(); ++it) {
        if (!kAllowedKeys.contains(it.key())) {
            error = QStringLiteral("Unknown search option: %1").arg(it.key());
            return false;
        }
    }

    SearchOptions parsed;

    if (!options.contains(QStringLiteral("query"))) {
        error = QStringLiteral("Missing required option: query");
        return false;
    }
    const QVariant queryValue = options.value(QStringLiteral("query"));
    if (queryValue.typeId() != QMetaType::QString) {
        error = QStringLiteral("Option 'query' must be a string");
        return false;
    }
    parsed.query = queryValue.toString();
    if (parsed.query.toUtf8().size() > kMaxQueryBytes) {
        error = QStringLiteral("Search query is too long");
        return false;
    }

    if (options.contains(QStringLiteral("flags"))) {
        const QVariant flagsValue = options.value(QStringLiteral("flags"));
        if (!isIntegerType(flagsValue)) {
            error = QStringLiteral("Option 'flags' must be an integer");
            return false;
        }
        const qlonglong flags = flagsValue.toLongLong();
        if (flags < 1 || flags > kMaxSearchFlags) {
            error = QStringLiteral("Option 'flags' is out of range");
            return false;
        }
        parsed.flags = static_cast<int>(flags);
    } else {
        parsed.flags = 1; // SearchFlag::Name を既定とする
    }

    if (options.contains(QStringLiteral("match"))) {
        const QVariant matchValue = options.value(QStringLiteral("match"));
        if (matchValue.typeId() != QMetaType::QString) {
            error = QStringLiteral("Option 'match' must be a string");
            return false;
        }
        const QString mode = matchValue.toString();
        if (mode == QStringLiteral("substring")) {
            parsed.match = SearchMatchMode::Substring;
        } else if (mode == QStringLiteral("exact")) {
            parsed.match = SearchMatchMode::Exact;
        } else if (mode == QStringLiteral("glob")) {
            parsed.match = SearchMatchMode::Glob;
        } else if (mode == QStringLiteral("regex")) {
            parsed.match = SearchMatchMode::Regex;
        } else {
            error = QStringLiteral("Option 'match' must be one of: substring, exact, glob, regex");
            return false;
        }
    }

    if (options.contains(QStringLiteral("offset"))
        && !toNonNegative(options.value(QStringLiteral("offset")), parsed.offset)) {
        error = QStringLiteral("Option 'offset' must be a non-negative integer");
        return false;
    }

    if (options.contains(QStringLiteral("limit"))
        && !toNonNegative(options.value(QStringLiteral("limit")), parsed.limit)) {
        error = QStringLiteral("Option 'limit' must be a non-negative integer");
        return false;
    }

    if (options.contains(QStringLiteral("installedOnly"))) {
        const QVariant installedValue = options.value(QStringLiteral("installedOnly"));
        if (installedValue.typeId() != QMetaType::Bool) {
            error = QStringLiteral("Option 'installedOnly' must be a boolean");
            return false;
        }
        parsed.installedOnly = installedValue.toBool();
    }

    if (options.contains(QStringLiteral("repoAlias"))) {
        const QVariant aliasValue = options.value(QStringLiteral("repoAlias"));
        if (aliasValue.typeId() != QMetaType::QString) {
            error = QStringLiteral("Option 'repoAlias' must be a string");
            return false;
        }
        parsed.repoAlias = aliasValue.toString();
        if (parsed.repoAlias.contains(QLatin1Char('/'))
            || parsed.repoAlias.contains(QLatin1Char('\\'))) {
            error = QStringLiteral("Option 'repoAlias' contains illegal characters");
            return false;
        }
    }

    // 正規表現は D-Bus 境界でコンパイル検証し、実行時の例外を避ける。
    if (parsed.match == SearchMatchMode::Regex) {
        const QRegularExpression re(parsed.query);
        if (!re.isValid()) {
            error = QStringLiteral("Option 'query' is not a valid regular expression: %1")
                        .arg(re.errorString());
            return false;
        }
    }

    out = parsed;
    return true;
}

} // namespace qZypper
