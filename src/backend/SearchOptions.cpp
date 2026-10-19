#include <regex.h>
#include <algorithm>
#include <vector>
#include "SearchOptions.h"
#include "InputValidation.h"

namespace qZypper {
namespace {
    constexpr int kMaxSearchFlags = 127;            // SearchFlagに定義された全ビットの和
    constexpr int kMaxQueryBytes = 256;             // 検索文字列の最大バイト数
    constexpr quint64 kMaxRegexRepeatTotal = 1000;  // 正規表現の繰り返し上限の合計の最大値

    /**
    * @brief 整数として扱える型かどうかを判定する
    * @param value 判定対象
    * @return D-Bus由来の整数型ならtrue
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
    * @brief 非負の整数として取り出す
    * @param value 判定対象
    * @param out 取り出し結果
    * @return 非負整数ならtrue
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

    /**
    * @brief 正規表現がregcompの資源を過大に消費しないかを検査する
    *
    * glibcのregcompは繰り返しを展開・入れ子にしてコンパイルするため、256バイト以内でも入れ子の繰り返し (例 : (a{32767}) {32767}、((a) +) +) や
    * 連続した量指定子 (例 : a+++) で大量のメモリとCPUを消費する
    *
    * そのため次を拒否する:
    * - 量指定子 (*, +, ?, {m,n}) を含むグループへの量指定子
    * - 量指定子の直後の量指定子
    * - {m,n}の上限の合計がkMaxRegexRepeatTotalを超えるもの
    * - 後方参照 (\1〜\9、照合時のバックトラックが指数的になり得る)
    *
    * @param pattern 検査対象 (UTF-8)
    * @param error 拒否時のエラーメッセージ
    * @return 許可できればtrue
    */
    bool checkRegexComplexity(const QByteArray &pattern, QString &error)
    {
        enum class Previous { None, Atom, Group, QuantifiedGroup, Quantifier };

        const QString tooMany = QStringLiteral("Option 'query' uses too many repetitions (limit %1)").arg(kMaxRegexRepeatTotal);
        const QString nested = QStringLiteral("Option 'query' must not use nested or consecutive repetitions");

        std::vector<bool> groupHasQuantifier{false};
        Previous previous = Previous::None;
        quint64 total = 0;

        const int size = static_cast<int>(pattern.size());
        for (int i = 0; i < size; ++i) {
            const char c = pattern.at(i);
            int quantifierEnd = -1;
            quint64 bound = 0;

            if (c == '\\') {
                if (i + 1 < size && pattern.at(i + 1) >= '1' && pattern.at(i + 1) <= '9') {
                    error = QStringLiteral("Option 'query' must not use back-references");
                    return false;
                }

                ++i;
                previous = Previous::Atom;
                continue;
            }

            if (c == '[') {
                // ブラケット式内の'{'や量指定子はリテラル
                // 先頭の'^'と']'は終端ではない
                int j = i + 1;

                if (j < size && pattern.at(j) == '^')
                    ++j;

                if (j < size && pattern.at(j) == ']')
                    ++j;

                while (j < size && pattern.at(j) != ']') {
                    const char next = j + 1 < size ? pattern.at(j + 1) : '\0';
                    if (pattern.at(j) == '[' && (next == ':' || next == '.' || next == '=')) {
                        const char terminator[] = {next, ']', '\0'};
                        const int close = static_cast<int>(pattern.indexOf(terminator, j + 2));
                        j = close < 0 ? size : close + 2;
                        continue;
                    }
                    ++j;
                }

                i = j;
                previous = Previous::Atom;
                continue;
            }

            if (c == '(') {
                groupHasQuantifier.push_back(false);
                previous = Previous::None;
                continue;
            }

            if (c == ')') {
                if (groupHasQuantifier.size() > 1) {
                    const bool inner = groupHasQuantifier.back();
                    groupHasQuantifier.pop_back();

                    if (inner)
                        groupHasQuantifier.back() = true;

                    previous = inner ? Previous::QuantifiedGroup : Previous::Group;
                }
                else {
                    previous = Previous::Atom;
                }

                continue;
            }

            if (c == '|') {
                previous = Previous::None;
                continue;
            }

            if (c == '*' || c == '+' || c == '?') {
                quantifierEnd = i;
                bound = 1;
            }
            else if (c == '{') {
                int j = i + 1;
                quint64 low = 0;
                quint64 high = 0;
                bool hasLow = false;
                bool hasHigh = false;

                while (j < size && pattern.at(j) >= '0' && pattern.at(j) <= '9' && low <= 0xFFFFFFFFULL) {
                    low = low * 10 + static_cast<quint64>(pattern.at(j) - '0');
                    hasLow = true;
                    ++j;
                }

                if (j < size && pattern.at(j) == ',') {
                    ++j;
                    while (j < size && pattern.at(j) >= '0' && pattern.at(j) <= '9' && high <= 0xFFFFFFFFULL) {
                        high = high * 10 + static_cast<quint64>(pattern.at(j) - '0');
                        hasHigh = true;
                        ++j;
                    }
                }

                if (j < size && pattern.at(j) == '}' && (hasLow || hasHigh)) {
                    quantifierEnd = j;
                    bound = std::max<quint64>(1, hasHigh ? high : low);
                }
            }

            if (quantifierEnd < 0) {
                previous = Previous::Atom;
                continue;
            }

            if (previous == Previous::Quantifier || previous == Previous::QuantifiedGroup) {
                error = nested;
                return false;
            }

            if (bound > kMaxRegexRepeatTotal || total + bound > kMaxRegexRepeatTotal) {
                error = tooMany;
                return false;
            }

            total += bound;
            groupHasQuantifier.back() = true;
            previous = Previous::Quantifier;
            i = quantifierEnd;
        }

        return true;
    }

} // namespace

/**
 * @brief 拡張検索オプションを型、範囲、正規表現の安全性で検証する
 * @param options D-Busで受け取った検索条件
 * @param out 成功時の正規化済み検索条件の出力先
 * @param error 失敗時の理由の出力先
 * @return 全条件が検索に使用できる場合はtrue
 */
bool parseSearchOptions(const QVariantMap &options, SearchOptions &out, QString &error)
{
    // 制御文字・過大な文字列長などは既存の共通検証で弾く
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
    }
    else {
        parsed.flags = 1; // SearchFlag::Nameを既定とする
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
        }
        else if (mode == QStringLiteral("exact")) {
            parsed.match = SearchMatchMode::Exact;
        }
        else if (mode == QStringLiteral("glob")) {
            parsed.match = SearchMatchMode::Glob;
        }
        else if (mode == QStringLiteral("regex")) {
            parsed.match = SearchMatchMode::Regex;
        }
        else {
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
        if (parsed.repoAlias.contains(QLatin1Char('/')) || parsed.repoAlias.contains(QLatin1Char('\\'))) {
            error = QStringLiteral("Option 'repoAlias' contains illegal characters");
            return false;
        }
    }

    // 正規表現はD-Bus境界でコンパイル検証し、実行時の例外を避ける
    // libzypp (StrMatcher) / libsolv (datamatcher) はPOSIX EREをREG_EXTENDED | REG_NOSUB | REG_NEWLINEで実行するため、
    // 同じフラグ (照合が大文字小文字無視のためREG_ICASEを追加) で事前検証する
    if (parsed.match == SearchMatchMode::Regex) {
        const QByteArray pattern = parsed.query.toUtf8();
        if (!checkRegexComplexity(pattern, error))
            return false;

        regex_t regex;
        const int rc = ::regcomp(&regex, pattern.constData(), REG_EXTENDED | REG_NOSUB | REG_NEWLINE | REG_ICASE);
        if (rc != 0) {
            char buffer[256];
            ::regerror(rc, &regex, buffer, sizeof(buffer));
            error = QStringLiteral("Option 'query' is not a valid regular expression: %1").arg(QString::fromLocal8Bit(buffer));
            return false;
        }

        ::regfree(&regex);
    }

    out = parsed;
    return true;
}

} // namespace qZypper
