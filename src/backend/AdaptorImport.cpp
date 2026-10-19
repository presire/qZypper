#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusError>
#include <QDBusMessage>
#include <QDBusUnixFileDescriptor>
#include <QStringList>
#include <memory>
#include <string>
#include <vector>
#include "PackageManagerAdaptor.h"
#include "InputValidation.h"
#include "LocalRpm.h"
#include "RepoFileParser.h"
#include "RepoPolicy.h"
#include "ZyppManager.h"

namespace qZypper {
namespace {

/** @brief .repoファイル内容の最大サイズ (64[KiB]) */
constexpr std::size_t kMaxRepoFileBytes = kMaxRepoFileContentBytes;

/** @brief エイリアス検証をQStringコールバックへ適合させる */
bool checkAlias(const QString &alias, QString &err)
{
    std::string detail;
    if (RepoPolicy::validateAlias(alias.toStdString(), detail))
        return true;

    err = QString::fromStdString(detail);
    return false;
}

/** @brief URL検証をQStringコールバックへ適合させる */
bool checkUrl(const QString &url, QString &err)
{
    std::string detail;
    if (RepoPolicy::validateUrl(url.toStdString(), detail))
        return true;

    err = QString::fromStdString(detail);
    return false;
}

/** @brief .repoのリポタイプ表記をlibzyppの正規名へ変換する */
QString translateRepoType(const QString &type)
{
    if (type == QStringLiteral("rpmmd"))
        return QStringLiteral("rpm-md");

    return type;
}

/** @brief 構文解析結果をD-Bus用a{sv}群へ変換する */
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

/** @brief ローカルRPM導入結果をD-Bus用a{sv}へ変換する */
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
 * @brief ローカルRPMを認可後に一時リポジトリ経由で導入し解決する
 *
 * ファイルは、UIがユーザ権限で開いた記述子として受け取る
 * rootがユーザ指定パスを開くことによる権限昇格 (読めないファイルの取込み)・symlink・FIFO停止を避けるため
 *
 * @param files UIが開いたRPMファイルの記述子群
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

    // 変異・認可の前に全記述子を検証して複製する (fail-closed)
    auto adopted = std::make_shared<std::vector<LocalRpmFile>>();
    adopted->reserve(static_cast<std::size_t>(files.size()));
    for (int i = 0; i < files.size(); ++i) {
        LocalRpmFile file;
        std::string err;
        if (!files.at(i).isValid() || !adoptLocalRpmFd(files.at(i).fileDescriptor(), fileNames.at(i).toStdString(),
            kMaxLocalRpmBytes, file, err)) {
            sendErrorReply(QDBusError::InvalidArgs, QStringLiteral("Rejected '%1': %2").arg(fileNames.at(i), QString::fromStdString(err)));
            return {};
        }
        adopted->push_back(std::move(file));
    }

    startPrivileged(QStringLiteral("org.presire.qzypper.install-packages"), [adopted] {
        AsyncOutcome result;
        auto &manager = ZyppManager::instance();
        result.value = installResultToMap(manager.installLocalRpms(std::move(*adopted)));
        if (manager.lastOperationCancelled()) {
            result.errorName = QStringLiteral("org.presire.qzypper.Error.Cancelled");
            result.errorText = QString::fromStdString(manager.lastError());
        }
        return result;
    });
    return {};
}

/**
 * @brief .repo内容を構文解析してリポジトリを追加する
 * @param content .repoファイルの内容 (最大64 KiB)
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

        // GPG検査の無効化は受け入れない (fail-closed)
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
            }
            else {
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
 * @brief .repo内容を構文解析する (認可なし・読取専用)
 * @param content .repoファイルの内容 (最大64[KiB])
 * @return 解析済みセクションとスキップ理由のa{sv}群
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
