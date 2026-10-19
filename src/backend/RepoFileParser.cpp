#include "RepoFileParser.h"
#include <QStringList>
#include <algorithm>

namespace qZypper {
namespace {

/**
 * @brief 三値ブール表記 (1/true/yes/onと0/false/no/off) を解析する
 * @param value 解析対象の値
 * @return 解釈できる場合はその値、不能ならnullopt
 */
std::optional<bool> parseBoolValue(const QString &value)
{
    const QString v = value.trimmed().toLower();
    if (v == QLatin1String("1") || v == QLatin1String("true")
        || v == QLatin1String("yes") || v == QLatin1String("on"))
        return true;
    if (v == QLatin1String("0") || v == QLatin1String("false")
        || v == QLatin1String("no") || v == QLatin1String("off"))
        return false;
    return std::nullopt;
}

} // namespace

/**
 * @brief .repo (INI) テキストを純粋関数として厳密に解析する
 * @param content .repoの内容
 * @param maxBytes 受け付ける最大バイト数
 * @param validateAlias エイリアス検証関数
 * @param validateUrl URL検証関数
 * @param out 解析結果の出力先
 * @return ファイル全体の構文が正しい場合はtrue
 */
bool parseRepoFile(const QString &content,
                   std::size_t maxBytes,
                   const std::function<bool(const QString &, QString &)> &validateAlias,
                   const std::function<bool(const QString &, QString &)> &validateUrl,
                   RepoFileParseResult &out)
{
    out = RepoFileParseResult{};

    if (content.toUtf8().size() > static_cast<qsizetype>(maxBytes)) {
        out.error = QStringLiteral("Repository file exceeds %1 bytes").arg(static_cast<qulonglong>(maxBytes));
        return false;
    }

    RepoFileSection current;
    bool haveSection = false;

    // 現在のセクションを確定し、検証を通ればsections、通らなければskippedへ回す
    auto finishSection = [&]() {
        if (!haveSection)
            return;
        if (!current.invalidReason.isEmpty()) {
            out.skipped.emplace_back(current.alias, current.invalidReason);
        }
        else {
            QString err;
            if (!validateAlias(current.alias, err)) {
                out.skipped.emplace_back(current.alias, err);
            }
            else if (current.url.isEmpty()) {
                out.skipped.emplace_back(current.alias, QStringLiteral("missing baseurl"));
            }
            else if (!validateUrl(current.url, err)) {
                out.skipped.emplace_back(current.alias, err);
            }
            else {
                if (current.name.isEmpty())
                    current.name = current.alias;

                const auto duplicate = std::find_if(out.sections.cbegin(), out.sections.cend(),
                    [&](const RepoFileSection &s) { return s.alias == current.alias; });

                if (duplicate != out.sections.cend())
                    out.skipped.emplace_back(current.alias, QStringLiteral("duplicate alias"));
                else
                    out.sections.push_back(current);
            }
        }

        current = RepoFileSection{};
        haveSection = false;
    };

    const QStringList lines = content.split(QLatin1Char('\n'));
    for (int index = 0; index < lines.size(); ++index) {
        QString line = lines.at(index);
        if (line.endsWith(QLatin1Char('\r')))
            line.chop(1);

        const QString trimmed = line.trimmed();

        if (trimmed.isEmpty() || trimmed.startsWith(QLatin1Char('#')) || trimmed.startsWith(QLatin1Char(';')))
            continue;

        const int lineNumber = index + 1;
        if (trimmed.startsWith(QLatin1Char('['))) {
            if (!trimmed.endsWith(QLatin1Char(']'))) {
                out.error = QStringLiteral("Line %1: malformed section header").arg(lineNumber);
                return false;
            }

            finishSection();
            const QString alias = trimmed.mid(1, trimmed.size() - 2).trimmed();

            if (alias.isEmpty()) {
                out.error = QStringLiteral("Line %1: empty section name").arg(lineNumber);
                return false;
            }

            current.alias = alias;
            haveSection = true;
            continue;
        }

        if (!haveSection) {
            out.error = QStringLiteral("Line %1: key outside of a section").arg(lineNumber);
            return false;
        }

        const int separator = trimmed.indexOf(QLatin1Char('='));

        if (separator <= 0) {
            out.error = QStringLiteral("Line %1: expected key=value").arg(lineNumber);
            return false;
        }

        const QString key = trimmed.left(separator).trimmed().toLower();
        const QString value = trimmed.mid(separator + 1).trimmed();

        auto markInvalid = [&](const QString &reason) {
            if (current.invalidReason.isEmpty())
                current.invalidReason = reason;
        };

        auto applyBool = [&](std::optional<bool> &target, const QString &label) {
            const std::optional<bool> parsed = parseBoolValue(value);
            if (!parsed)
                markInvalid(QStringLiteral("invalid boolean for '%1'").arg(label));
            else
                target = *parsed;
        };

        if (key == QLatin1String("name")) {
            current.name = value;
        }
        else if (key == QLatin1String("baseurl")) {
            if (current.url.isEmpty())
                current.url = value;
        }
        else if (key == QLatin1String("mirrorlist") || key == QLatin1String("metalink")) {
            markInvalid(QStringLiteral("'%1' is not supported").arg(key));
        }
        else if (key == QLatin1String("enabled")) {
            const std::optional<bool> parsed = parseBoolValue(value);
            if (!parsed)
                markInvalid(QStringLiteral("invalid boolean for 'enabled'"));
            else
                current.enabled = *parsed;
        }
        else if (key == QLatin1String("autorefresh")) {
            const std::optional<bool> parsed = parseBoolValue(value);
            if (!parsed)
                markInvalid(QStringLiteral("invalid boolean for 'autorefresh'"));
            else
                current.autoRefresh = *parsed;
        }
        else if (key == QLatin1String("keeppackages")) {
            const std::optional<bool> parsed = parseBoolValue(value);
            if (!parsed)
                markInvalid(QStringLiteral("invalid boolean for 'keeppackages'"));
            else
                current.keepPackages = *parsed;
        }
        else if (key == QLatin1String("gpgcheck")) {
            applyBool(current.gpgCheck, key);
        }
        else if (key == QLatin1String("repo_gpgcheck")) {
            applyBool(current.repoGpgCheck, key);
        }
        else if (key == QLatin1String("pkg_gpgcheck")) {
            applyBool(current.pkgGpgCheck, key);
        }
        else if (key == QLatin1String("gpgkey")) {
            if (current.gpgKey.isEmpty())
                current.gpgKey = value;
        }
        else if (key == QLatin1String("priority")) {
            bool ok = false;
            const long long priority = value.toLongLong(&ok);
            if (!ok || priority < 1 || priority > 200)
                markInvalid(QStringLiteral("invalid priority"));
            else
                current.priority = priority;
        }
        else if (key == QLatin1String("type")) {
            const QString type = value.toLower();
            if (type != QLatin1String("rpmmd") && type != QLatin1String("plaindir")
                && type != QLatin1String("yast2"))
                markInvalid(QStringLiteral("unsupported type"));
            else
                current.type = type;
        }
        else if (key == QLatin1String("path")) {
            current.path = value;
        }
        else {
            markInvalid(QStringLiteral("unknown key '%1'").arg(key));
        }
    }

    finishSection();
    out.ok = true;
    return true;
}

/**
 * @brief セクションをUIプレビュー / RepoInfo用のQVariantMapへ変換する
 * @param section 変換対象セクション
 * @return 正規化キーを持つマップ
 */
QVariantMap repoFileSectionToMap(const RepoFileSection &section)
{
    QVariantMap map;
    map.insert(QStringLiteral("alias"), section.alias);
    map.insert(QStringLiteral("name"), section.name.isEmpty() ? section.alias : section.name);
    map.insert(QStringLiteral("url"), section.url);
    map.insert(QStringLiteral("enabled"), section.enabled);
    map.insert(QStringLiteral("autoRefresh"), section.autoRefresh);
    map.insert(QStringLiteral("priority"), QVariant::fromValue<qlonglong>(section.priority));
    map.insert(QStringLiteral("keepPackages"), section.keepPackages);
    if (!section.type.isEmpty())
        map.insert(QStringLiteral("type"), section.type);
    if (!section.path.isEmpty())
        map.insert(QStringLiteral("path"), section.path);
    if (section.gpgCheck)
        map.insert(QStringLiteral("gpgCheck"), *section.gpgCheck);
    if (section.repoGpgCheck)
        map.insert(QStringLiteral("repoGpgCheck"), *section.repoGpgCheck);
    if (section.pkgGpgCheck)
        map.insert(QStringLiteral("pkgGpgCheck"), *section.pkgGpgCheck);
    if (!section.gpgKey.isEmpty())
        map.insert(QStringLiteral("gpgKey"), section.gpgKey);
    if (!section.invalidReason.isEmpty())
        map.insert(QStringLiteral("error"), section.invalidReason);
    return map;
}

} // namespace qZypper
