#include <QStringList>
#include "SolverSettingsParse.h"
#include "InputValidation.h"

namespace qZypper {

/**
 * @brief ソルバー設定の上書きマップを厳格に検証して解析する
 * @param settings D-Bus由来の設定マップ
 * @param out 解析結果の格納先 (失敗時は変更しない)
 * @param error 失敗理由
 * @return 検証成功ならtrue
 */
bool parseSolverSettings(const QVariantMap &settings, ZyppManager::SolverOverrides &out,
                         QString &error)
{
    // 制御文字・過大な文字列長などは既存の共通検証で弾く
    QString argError;
    if (!validateArguments(QVariantList{settings}, argError)) {
        error = argError;
        return false;
    }

    if (settings.isEmpty()) {
        error = QStringLiteral("Solver settings must not be empty");
        return false;
    }

    static const QStringList kAllowedKeys = {
        QStringLiteral("installRecommends"),
        QStringLiteral("cleandepsOnRemove"),
        QStringLiteral("allowVendorChange")
    };

    for (auto it = settings.constBegin(); it != settings.constEnd(); ++it) {
        if (!kAllowedKeys.contains(it.key())) {
            error = QStringLiteral("Unknown solver setting: %1").arg(it.key());
            return false;
        }
        if (it.value().typeId() != QMetaType::Bool) {
            error = QStringLiteral("Solver setting '%1' must be a boolean").arg(it.key());
            return false;
        }
    }

    ZyppManager::SolverOverrides parsed;
    if (settings.contains(QStringLiteral("installRecommends")))
        parsed.installRecommends = settings.value(QStringLiteral("installRecommends")).toBool();
    if (settings.contains(QStringLiteral("cleandepsOnRemove")))
        parsed.cleandepsOnRemove = settings.value(QStringLiteral("cleandepsOnRemove")).toBool();
    if (settings.contains(QStringLiteral("allowVendorChange")))
        parsed.allowVendorChange = settings.value(QStringLiteral("allowVendorChange")).toBool();

    out = parsed;
    return true;
}

} // namespace qZypper
