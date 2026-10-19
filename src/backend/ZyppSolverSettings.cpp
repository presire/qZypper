#include <QDebug>
#include <QVariantMap>
#include <zypp/ZConfig.h>
#include "ZyppManager.h"

namespace qZypper {
namespace {
/**
 * @brief zypp.conf由来のソルバー設定既定値を返す
 * @return installRecommends/cleandepsOnRemove/allowVendorChangeを持つマップ
 */
QVariantMap solverDefaults()
{
    const zypp::ZConfig& config = zypp::ZConfig::instance();
    return QVariantMap{
        {QStringLiteral("installRecommends"), !config.solver_onlyRequires()},
        {QStringLiteral("cleandepsOnRemove"), config.solver_cleandepsOnRemove()},
        {QStringLiteral("allowVendorChange"), config.solver_allowVendorChange()}
    };
}
}

/**
 * @brief 有効なソルバー設定とzypp.conf既定値を返す
 *
 * 上書きが設定されていない項目は既定値を返す
 * libzyppを初期化していない場合も既定値だけを返す
 *
 * @return 有効値3項目とdefaultsマップ
 */
QVariantMap ZyppManager::getSolverSettings() const
{
    std::lock_guard<std::mutex> lock(m_mutex);

    const QVariantMap defaults = solverDefaults();
    return QVariantMap{
        {QStringLiteral("installRecommends"),
         m_solverOverrides.installRecommends.value_or(
             defaults.value(QStringLiteral("installRecommends")).toBool())},
        {QStringLiteral("cleandepsOnRemove"),
         m_solverOverrides.cleandepsOnRemove.value_or(
             defaults.value(QStringLiteral("cleandepsOnRemove")).toBool())},
        {QStringLiteral("allowVendorChange"),
         m_solverOverrides.allowVendorChange.value_or(
             defaults.value(QStringLiteral("allowVendorChange")).toBool())},
        {QStringLiteral("defaults"), defaults}
    };
}

/**
 * @brief 渡された項目だけをソルバー設定の上書きへ反映する
 *
 * 既存の問題リストは新しい設定と無関係になるため破棄する
 *
 * @param patch 上書きする項目 (未設定の項目は変更しない)
 * @return 反映した場合はtrue、リゾルバへの適用に失敗した場合は上書きを元に戻し、m_lastErrorを設定してfalse
 */
bool ZyppManager::setSolverSettings(const SolverOverrides& patch)
{
    std::lock_guard<std::mutex> lock(m_mutex);

    const SolverOverrides previous = m_solverOverrides;

    m_problems.clear();

    if (patch.installRecommends) {
        m_solverOverrides.installRecommends = patch.installRecommends;
    }

    if (patch.cleandepsOnRemove) {
        m_solverOverrides.cleandepsOnRemove = patch.cleandepsOnRemove;
    }

    if (patch.allowVendorChange) {
        m_solverOverrides.allowVendorChange = patch.allowVendorChange;
    }

    // マージ後の上書きを即座にリゾルバへ反映する
    // resolveDependenciesを経ずにresolvePoolを呼ぶ経路 (推奨/不要分類の取得など) でも
    // 新しい設定で解決させるため
    if (m_initialized && m_zypp) {
        // 適用に失敗したら上書きを元に戻す
        // GetSolverSettingsが返す値とリゾルバが実際に使う値が食い違わないようにするため
        const auto rollback = [this, &previous](const std::string &detail) {
            m_lastError = "Failed to apply solver settings: " + detail;
            m_solverOverrides = previous;

            try {
                m_zypp->resolver()->setDefaultSolverFlags();
                applySolverOverridesLocked(m_zypp->resolver());
            }
            catch (const zypp::Exception& e) {
                qWarning() << "Failed to restore solver overrides:" << QString::fromStdString(e.asString());
            }
            catch (const std::exception& e) {
                qWarning() << "Failed to restore solver overrides:" << e.what();
            }
        };

        try {
            applySolverOverridesLocked(m_zypp->resolver());
        }
        catch (const zypp::Exception& e) {
            qWarning() << "Failed to apply solver overrides:" << QString::fromStdString(e.asString());
            rollback(e.asString());
            return false;
        }
        catch (const std::exception& e) {
            qWarning() << "Failed to apply solver overrides:" << e.what();
            rollback(e.what());
            return false;
        }
    }

    return true;
}

/**
 * @brief ソルバー設定の上書きを破棄して既定値へ戻す
 */
void ZyppManager::resetSolverOverrides()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    resetSolverOverridesLocked();
}

/**
 * @brief ソルバー設定の上書きを破棄して既定値へ戻す (ロック保持中)
 *
 * 初期化済みの場合はリゾルバへzypp.conf既定値を再適用する
 */
void ZyppManager::resetSolverOverridesLocked()
{
    m_solverOverrides = {};

    if (m_initialized && m_zypp) {
        try {
            m_zypp->resolver()->setDefaultSolverFlags();
        }
        catch (const zypp::Exception& e) {
            qWarning() << "Failed to restore default solver flags:" << QString::fromStdString(e.asString());
        }
        catch (const std::exception& e) {
            qWarning() << "Failed to restore default solver flags:" << e.what();
        }
    }

    m_problems.clear();
}

/**
 * @brief 設定済みの上書きだけをリゾルバへ適用する
 * @param resolver 対象のリゾルバ
 * @note m_mutex保持中に呼ぶこと
 */
void ZyppManager::applySolverOverridesLocked(const zypp::Resolver_Ptr& resolver)
{
    if (!resolver)
        return;

    if (m_solverOverrides.installRecommends)
        resolver->setOnlyRequires(!m_solverOverrides.installRecommends.value());
    if (m_solverOverrides.cleandepsOnRemove)
        resolver->setCleandepsOnRemove(m_solverOverrides.cleandepsOnRemove.value());
    if (m_solverOverrides.allowVendorChange)
        resolver->setAllowVendorChange(m_solverOverrides.allowVendorChange.value());
}

} // namespace qZypper
