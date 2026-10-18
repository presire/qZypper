#include "PackageManagerAdaptor.h"
#include "InputValidation.h"
#include "LocalRpm.h"
#include "RepoFileParser.h"
#include "ZyppManager.h"

#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusError>
#include <QDBusMessage>
#include <QDBusUnixFileDescriptor>
#include <QStringList>
#include <memory>

#include <string>
#include <vector>

namespace qZypper {
namespace {

/** @brief .repo ファイル内容の最大サイズ (64 KiB)。 */
constexpr std::size_t kMaxRepoFileBytes = kMaxRepoFileContentBytes;

/** @brief エイリアス検証を QString コールバックへ適合させる。 */
bool checkAlias(const QString &alias, QString &err)
{
    std::string detail;
    if (ZyppManager::validateAlias(alias.toStdString(), detail))
        return true;
    err = QString::fromStdString(detail);
    return false;
}

/** @brief URL 検証を QString コールバックへ適合させる。 */
bool checkUrl(const QString &url, QString &err)
{
    std::string detail;
    if (ZyppManager::validateUrl(url.toStdString(), detail))
        return true;
    err = QString::fromStdString(detail);
    return false;
}

/** @brief .repo のリポタイプ表記を libzypp の正規名へ変換する。 */
QString translateRepoType(const QString &type)
{
    if (type == QStringLiteral("rpmmd"))
        return QStringLiteral("rpm-md");
    return type;
}

/** @brief 構文解析結果を D-Bus 用 a{sv} 群へ変換する。 */
QVariantList sectionsToVariantList(const RepoFileParseResult &parsed)
{
    QVariantList out;
    if (!parsed.ok) {
        QVariantMap error;
        error["error"] = parsed.error;
        out.append(error);
        return out;
    }
    for (const RepoFileSection &section : parsed.sections)
        out.append(repoFileSectionToMap(section));
    for (const auto &skipped : parsed.skipped) {
        QVariantMap entry;
        entry["alias"] = skipped.first;
        entry["error"] = skipped.second;
        out.append(entry);
    }
    return out;
}

/** @brief ローカル RPM 導入結果を D-Bus 用 a{sv} へ変換する。 */
QVariantMap installResultToMap(const ZyppManager::LocalRpmInstallResult &result)
{
    QVariantList problems;
    for (const ConflictInfo &conflict : result.problems) {
        QVariantMap entry;
        entry["description"] = conflict.description;
        entry["details"]     = conflict.details;
        QVariantList solutions;
        for (const QVariantMap &solution : conflict.solutions)
            solutions.append(solution);
        entry["solutions"]   = solutions;
        problems.append(entry);
    }

    QVariantList packages;
    for (const std::string &name : result.packages)
        packages.append(QString::fromStdString(name));

    QVariantList unsignedPackages;
    for (const std::string &name : result.unsignedPackages)
        unsignedPackages.append(QString::fromStdString(name));

    QVariantMap map;
    map["success"]          = result.success;
    map["problems"]         = problems;
    map["summary"]          = result.summary;
    map["packages"]         = packages;
    map["unsignedPackages"] = unsignedPackages;
    return map;
}

} // namespace

/**
 * @brief ローカル RPM を認可後に一時リポジトリ経由で導入し解決する。
 *
 * ファイルは GUI がユーザー権限で開いた記述子として受け取る。root がユーザー指定パスを
 * 開くことによる権限昇格 (読めないファイルの取込み)・symlink・FIFO 停止を避けるため。
 * @param files GUI が開いた RPM ファイルの記述子群
 * @param fileNames 各記述子に対応するファイル名 (basename)
 * @return {success, problems, summary, packages, unsignedPackages}
 */
QVariantMap PackageManagerAdaptor::InstallLocalRpms(const QList<QDBusUnixFileDescriptor> &files,
                                                    const QStringList &fileNames)
{
    if (!preparePrivileged())
        return {};
    if (files.isEmpty() || files.size() != fileNames.size()) {
        sendErrorReply(QDBusError::InvalidArgs, QStringLiteral("No RPM files given or name count mismatch"));
        return {};
    }
    if (files.size() > static_cast<int>(kMaxLocalRpmCount)) {
        sendErrorReply(QDBusError::InvalidArgs, QStringLiteral("Too many RPM files"));
        return {};
    }

    // 変異・認可の前に全記述子を検証して複製する (fail-closed)。
    auto adopted = std::make_shared<std::vector<LocalRpmFile>>();
    adopted->reserve(static_cast<std::size_t>(files.size()));
    for (int i = 0; i < files.size(); ++i) {
        LocalRpmFile file;
        std::string err;
        if (!files.at(i).isValid()
            || !adoptLocalRpmFd(files.at(i).fileDescriptor(), fileNames.at(i).toStdString(),
                                kMaxLocalRpmBytes, file, err)) {
            sendErrorReply(QDBusError::InvalidArgs,
                           QStringLiteral("Rejected '%1': %2").arg(fileNames.at(i),
                                                                    QString::fromStdString(err)));
            return {};
        }
        adopted->push_back(std::move(file));
    }

    startPrivileged(QStringLiteral("org.presire.qzypper.install-packages"), [adopted] {
        AsyncOutcome result;
        result.value = installResultToMap(ZyppManager::instance().installLocalRpms(std::move(*adopted)));
        return result;
    });
    return {};
}

/**
 * @brief .repo 内容を構文解析してリポジトリを追加する。
 * @param content .repo ファイルの内容 (最大 64 KiB)
 * @return {success, added, skipped}
 */
QVariantMap PackageManagerAdaptor::ImportRepoFile(const QString &content)
{
    if (!preparePrivileged())
        return {};

    RepoFileParseResult parsed;
    if (!parseRepoFile(content, kMaxRepoFileBytes, checkAlias, checkUrl, parsed)) {
        sendErrorReply(QDBusError::InvalidArgs, parsed.error);
        return {};
    }

    std::vector<RepoInfo> toAdd;
    QVariantList skipped;
    for (const RepoFileSection &section : parsed.sections) {
        if (!section.invalidReason.isEmpty()) {
            QVariantMap entry;
            entry["alias"]  = section.alias;
            entry["reason"] = section.invalidReason;
            skipped.append(entry);
            continue;
        }
        // GPG 検査の無効化は受け入れない (fail-closed)。
        if ((section.gpgCheck && !*section.gpgCheck)
            || (section.repoGpgCheck && !*section.repoGpgCheck)
            || (section.pkgGpgCheck && !*section.pkgGpgCheck)) {
            QVariantMap entry;
            entry["alias"]  = section.alias;
            entry["reason"] = QStringLiteral("disabling GPG checks is not allowed");
            skipped.append(entry);
            continue;
        }
        RepoInfo info;
        info.alias        = section.alias;
        info.name         = section.name;
        info.url          = section.url;
        info.enabled      = section.enabled;
        info.autoRefresh  = section.autoRefresh;
        info.priority     = static_cast<int>(section.priority);
        info.keepPackages = section.keepPackages;
        info.type         = translateRepoType(section.type);
        toAdd.push_back(info);
    }
    for (const auto &pair : parsed.skipped) {
        QVariantMap entry;
        entry["alias"]  = pair.first;
        entry["reason"] = pair.second;
        skipped.append(entry);
    }

    startPrivileged(QStringLiteral("org.presire.qzypper.manage-repos"), [toAdd, skipped] {
        auto &manager = ZyppManager::instance();
        QVariantList added;
        QVariantList failed = skipped;
        for (const RepoInfo &info : toAdd) {
            if (manager.addRepo(info)) {
                added.append(info.alias);
            } else {
                QVariantMap entry;
                entry["alias"]  = info.alias;
                entry["reason"] = QString::fromStdString(manager.lastError());
                failed.append(entry);
            }
        }
        QVariantMap value;
        value["success"] = !added.isEmpty() && failed.isEmpty();
        value["added"]   = added;
        value["skipped"] = failed;
        AsyncOutcome result;
        result.value = value;
        return result;
    });
    return {};
}

/**
 * @brief .repo 内容を構文解析する (認可なし・読取専用)。
 * @param content .repo ファイルの内容 (最大 64 KiB)
 * @return 解析済みセクションとスキップ理由の a{sv} 群
 */
QVariantList PackageManagerAdaptor::ParseRepoFile(const QString &content)
{
    return runRead([content]() -> QVariant {
        RepoFileParseResult parsed;
        parseRepoFile(content, kMaxRepoFileBytes, checkAlias, checkUrl, parsed);
        return QVariant(sectionsToVariantList(parsed));
    }).toList();
}

} // namespace qZypper
