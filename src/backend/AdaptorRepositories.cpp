#include <QDBusError>
#include "PackageManagerAdaptor.h"
#include "ZyppManager.h"
#include "RepoPolicy.h"

namespace qZypper {
/** @brief 全リポジトリ更新を認可後に開始する */
bool PackageManagerAdaptor::RefreshRepos()
{
    return runPrivilegedBool(QStringLiteral("org.presire.qzypper.refresh-repos"), [this] {
        return ZyppManager::instance().refreshRepos([this](const std::string &alias, int pct) {
            m_executor.control().heartbeat();
            QMetaObject::invokeMethod(this, [this, alias, pct] {
                emitToOwner(QStringLiteral("RepoRefreshProgress"), {QString::fromStdString(alias), pct});
            }, Qt::QueuedConnection);
        });
    });
}

/** @brief 自動更新が有効なリポジトリの期限切れメタデータ更新を認可後に開始する */
bool PackageManagerAdaptor::RefreshReposIfNeeded()
{
    return runPrivilegedBool(QStringLiteral("org.presire.qzypper.refresh-repos"), [this] {
        return ZyppManager::instance().refreshReposIfNeeded([this](const std::string &alias, int pct) {
            m_executor.control().heartbeat();
            QMetaObject::invokeMethod(this, [this, alias, pct] {
                emitToOwner(QStringLiteral("RepoRefreshProgress"), {QString::fromStdString(alias), pct});
            }, Qt::QueuedConnection);
        });
    });
}

/** @brief 指定リポジトリ更新を認可後に開始する */
bool PackageManagerAdaptor::RefreshSingleRepo(const QString &alias)
{
    return runPrivilegedBool(QStringLiteral("org.presire.qzypper.refresh-repos"), [this, alias] {
        return ZyppManager::instance().refreshRepo(alias.toStdString(), [this](const std::string &name, int pct) {
            m_executor.control().heartbeat();
            QMetaObject::invokeMethod(this, [this, name, pct] {
                emitToOwner(QStringLiteral("RepoRefreshProgress"), {QString::fromStdString(name), pct});
            }, Qt::QueuedConnection);
        });
    });
}

/** @brief 指紋を検証し専用Polkitアクションで承認する */
bool PackageManagerAdaptor::TrustKey(const QString &fingerprint)
{
    if (!preparePrivileged()) return false;

    const auto normalized = KeyRingReceiver::normalizeFingerprint(fingerprint.toStdString());

    if (normalized.empty()) {
        sendErrorReply(QDBusError::InvalidArgs, QStringLiteral("Invalid signing key fingerprint"));
        return false;
    }

    startPrivileged(QStringLiteral("org.presire.qzypper.trust-key"), [normalized] {
        AsyncOutcome result;
        auto &manager = ZyppManager::instance();
        result.value = manager.approveKeyFingerprint(normalized);

        if (!result.value.toBool()) {
            result.errorName = QStringLiteral("org.freedesktop.DBus.Error.Failed");
            result.errorText = QString::fromStdString(manager.lastError());
        }
        return result;
    });
    return false;
}

/** @brief URL指定の追加結果を既存のマップ形式で返す */
QVariantMap PackageManagerAdaptor::AddRepo(const QString &url, const QString &name)
{
    if (!preparePrivileged()) return {};

    startPrivileged(QStringLiteral("org.presire.qzypper.manage-repos"), [url, name] {
        auto &manager = ZyppManager::instance();
        const bool ok = manager.addRepo(url.toStdString(), name.toStdString());
        QVariantMap value{{"success", ok}};

        if (!ok) value["errorMessage"] = QString::fromStdString(manager.lastError());

        AsyncOutcome result; result.value = value; return result;
    });
    return {};
}

/** @brief 部分更新型のプロパティを厳密に解釈して追加する */
QVariantMap PackageManagerAdaptor::AddRepoFull(const QVariantMap &properties)
{
    if (!preparePrivileged()) return {};

    RepoChanges parsed;
    std::optional<QString> alias;
    QString error;
    std::string priorityError;

    if (!PartialUpdate::takeString(properties, "alias", alias, error)
        || !RepoChanges::fromVariantMap(properties, parsed, error)) {
        sendErrorReply(QDBusError::InvalidArgs, error); return {};
    }

    if (parsed.priority && !RepoPolicy::validatePriority(*parsed.priority, priorityError)) {
        sendErrorReply(QDBusError::InvalidArgs, QString::fromStdString(priorityError)); return {};
    }

    RepoInfo info;
    info.alias = alias.value_or(QString());
    info.name = parsed.name.value_or(QString());
    info.url = parsed.url.value_or(QString());
    info.enabled = parsed.enabled.value_or(info.enabled);
    info.autoRefresh = parsed.autoRefresh.value_or(info.autoRefresh);
    info.keepPackages = parsed.keepPackages.value_or(info.keepPackages);

    if (parsed.priority) info.priority = int(*parsed.priority);

    startPrivileged(QStringLiteral("org.presire.qzypper.manage-repos"), [info] {
        auto &manager = ZyppManager::instance();
        const bool ok = manager.addRepo(info);
        QVariantMap value{{"success", ok}};
        if (!ok) value["errorMessage"] = QString::fromStdString(manager.lastError());
        AsyncOutcome result; result.value = value; return result;
    });
    return {};
}

/** @brief 認可後にリポジトリを削除する */
bool PackageManagerAdaptor::RemoveRepo(const QString &alias)
{
    return runPrivilegedBool(QStringLiteral("org.presire.qzypper.manage-repos"), [alias] { return ZyppManager::instance().removeRepo(alias.toStdString()); });
}

/** @brief 認可後に有効状態を変更する */
bool PackageManagerAdaptor::SetRepoEnabled(const QString &alias, bool enabled)
{
    return runPrivilegedBool(QStringLiteral("org.presire.qzypper.manage-repos"), [alias, enabled] { return ZyppManager::instance().setRepoEnabled(alias.toStdString(), enabled); });
}

/** @brief 型と優先度を検査した部分更新を適用する */
bool PackageManagerAdaptor::ModifyRepo(const QString &alias, const QVariantMap &properties)
{
    RepoChanges changes;
    QString error;
    std::string priorityError;
    if (!RepoChanges::fromVariantMap(properties, changes, error)) {
        if (preparePrivileged()) sendErrorReply(QDBusError::InvalidArgs, error);
        return false;
    }
    if (changes.priority && !RepoPolicy::validatePriority(*changes.priority, priorityError)) {
        if (preparePrivileged()) sendErrorReply(QDBusError::InvalidArgs, QString::fromStdString(priorityError));
        return false;
    }
    return runPrivilegedBool(QStringLiteral("org.presire.qzypper.manage-repos"), [alias, changes] { return ZyppManager::instance().modifyRepo(alias.toStdString(), changes); });
}

/** @brief 認可後にサービスを追加する */
bool PackageManagerAdaptor::AddService(const QString &url, const QString &alias)
{
    return runPrivilegedBool(QStringLiteral("org.presire.qzypper.manage-repos"), [url, alias] { return ZyppManager::instance().addService(url.toStdString(), alias.toStdString()); });
}

/** @brief 認可後にサービスを削除する */
bool PackageManagerAdaptor::RemoveService(const QString &alias)
{
    return runPrivilegedBool(QStringLiteral("org.presire.qzypper.manage-repos"), [alias] { return ZyppManager::instance().removeService(alias.toStdString()); });
}

/** @brief 型検査済みのサービス部分更新を適用する */
bool PackageManagerAdaptor::ModifyService(const QString &alias, const QVariantMap &properties)
{
    ServiceChanges changes;
    QString error;
    if (!ServiceChanges::fromVariantMap(properties, changes, error)) {
        if (preparePrivileged()) sendErrorReply(QDBusError::InvalidArgs, error);
        return false;
    }
    return runPrivilegedBool(QStringLiteral("org.presire.qzypper.manage-repos"), [alias, changes] { return ZyppManager::instance().modifyService(alias.toStdString(), changes); });
}

/** @brief 認可後にサービスを更新する */
bool PackageManagerAdaptor::RefreshService(const QString &alias)
{
    return runPrivilegedBool(QStringLiteral("org.presire.qzypper.manage-repos"), [alias] { return ZyppManager::instance().refreshService(alias.toStdString()); });
}

} // namespace qZypper
