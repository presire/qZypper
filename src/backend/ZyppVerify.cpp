#include "ZyppManager.h"

#include <zypp/Resolver.h>
#include <zypp/ProblemTypes.h>

#include <QDateTime>
#include <QVariantMap>

#include <cerrno>
#include <cstring>
#include <string>
#include <sys/stat.h>

namespace qZypper {

ZyppManager::SolverResult ZyppManager::verifySystem()
{
    std::lock_guard<std::mutex> guard(m_mutex);
    SolverResult result;
    result.success = false;
    if (!m_initialized) {
        m_lastError = "libzypp not initialized";
        return result;
    }

    m_problems.clear();
    try {
        const auto resolver = m_zypp->resolver();
        resolver->setDefaultSolverFlags();
        result.success = resolver->verifySystem();
    } catch (const zypp::Exception &e) {
        m_lastError = e.asString();
    } catch (const std::exception &e) {
        m_lastError = e.what();
    }

    if (!result.success) {
        m_problems = m_zypp->resolver()->problems();
        for (const auto &problem : m_problems) {
            ConflictInfo conflict;
            conflict.description = QString::fromStdString(problem->description());
            conflict.details = QString::fromStdString(problem->details());
            const auto solutions = problem->solutions();
            for (const auto &solution : solutions) {
                QVariantMap item;
                item.insert(QStringLiteral("description"),
                            QString::fromStdString(solution->description()));
                item.insert(QStringLiteral("details"),
                            QString::fromStdString(solution->details()));
                conflict.solutions.append(item);
            }
            result.problems.push_back(conflict);
        }
    }
    // 修復のために選択されたパッケージを GUI が確認できるよう、解決後の概要を返す
    result.summary = computeTransactionSummaryLocked();
    return result;
}

std::string ZyppManager::createSolverTestcase()
{
    std::lock_guard<std::mutex> guard(m_mutex);
    if (!m_initialized) {
        m_lastError = "libzypp not initialized";
        return {};
    }

    const QDateTime now = QDateTime::currentDateTimeUtc();
    const QString stamp = now.toString(QStringLiteral("yyyyMMdd-HHmmss"));
    const std::string base = "/var/log/qzypper-testcase-" + stamp.toStdString();

    // symlink を辿らないよう mkdir で新規作成する (既存時は EEXIST で失敗)。
    std::string directory;
    for (int attempt = 0; attempt < 16; ++attempt) {
        const std::string candidate =
            attempt == 0 ? base : base + "-" + std::to_string(attempt);
        if (::mkdir(candidate.c_str(), 0700) == 0) {
            directory = candidate;
            break;
        }
        if (errno != EEXIST) {
            m_lastError = std::string("failed to create testcase directory: ")
                          + std::strerror(errno);
            return {};
        }
    }
    if (directory.empty()) {
        m_lastError = "failed to create unique testcase directory";
        return {};
    }

    try {
        if (!m_zypp->resolver()->createSolverTestcase(directory)) {
            m_lastError = "createSolverTestcase failed";
            return {};
        }
    } catch (const zypp::Exception &e) {
        m_lastError = e.asString();
        return {};
    } catch (const std::exception &e) {
        m_lastError = e.what();
        return {};
    }
    return directory;
}

} // namespace qZypper
