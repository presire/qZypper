/**
 * @file ZyppLicenses.cpp
 * @brief コミット前のlibzyppライセンス確認を実装する
 *
 * PackageManagerAdaptorからZyppManagerを通じて呼ばれ、所有者への対話結果を現在の候補へ反映する
 *
 * 入力 : PromptAskFnによる所有者の承認または拒否回答
 * 出力 : LicenseConfirmationResultと候補の承認状態または拒否状態
 * 依存 : ZyppManager、LicenseConfirmation、libzypp
 */
#include "ZyppManager.h"
#include "LicenseConfirmation.h"

namespace qZypper {

/**
 * @brief 選択済み候補のライセンスを確認し、拒否した候補を選択から除外する
 * @param askCallback 所有者へライセンス対話を送るコールバック
 * @return 承認数、拒否候補、選択変更、または失敗内容を持つ確認結果
 * @note m_mutexを保持し、確認中に候補プールが変化しないようにする
 * @note 承認時は候補のライセンス確認状態とm_acceptedLicensesを更新する
 * @note 拒否時はlibzyppの選択をTABOOまたはPROTECTEDへ変更する
 * @note libzypp例外はエラー結果へ変換し、呼び出し側へ伝える
 */
LicenseConfirmationResult ZyppManager::confirmLicenses(PromptAskFn askCallback)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    LicenseConfirmationResult result;
    // 初期化前のプールでは候補を安全に検査できないため、対話せず失敗を返す
    if (!m_initialized) {
        result.errorMessage = QStringLiteral("Backend not initialized");
        return result;
    }

    try {
        // 拒否時の状態変更で選択全体が変わるため、確認開始時の対象を保持する
        std::vector<zypp::PoolItem> pending;
        for (const auto &item : m_zypp->pool()) {
            if (item.status().transacts() && !item.status().isToBeUninstalled()) {
                pending.push_back(item);
            }
        }

        for (const auto &item : pending) {
            // キャンセル後に追加の対話や選択変更を行わないため、次の候補を処理しない
            if (m_cancelRequested.load()) {
                result.errorMessage = QStringLiteral("License confirmation cancelled; resolve and confirm again");
                return result;
            }
            // 先行する拒否で選択から外れた候補は、再度対話しない
            if (!item.status().transacts() || item.status().isToBeUninstalled()) {
                continue;
            }

            const auto solv = item.satSolvable();
            // ライセンス確認を要求しない候補は、承認状態を変更しない
            if (!solv || !solv.needToAcceptLicense()) {
                continue;
            }

            const std::string text = solv.licenseToConfirm();
            const auto key = licenseApprovalKey(solv.name(), solv.edition().asString(),
                                                solv.arch().asString(), item.repoInfo().alias(), text);
            if (m_acceptedLicenses.count(key)) {
                item.status().setLicenceConfirmed(true);
                continue;
            }

            // 本文なしまたは対話手段がない場合は安全側に倒し、未確認候補をコミットへ渡さない
            if (text.empty() || !askCallback) {
                result.errorMessage = QStringLiteral("Cannot confirm the license for package %1")
                                          .arg(QString::fromStdString(solv.name()));
                return result;
            }

            // PackageManagerAdaptorを経由して、D-Bus所有者に承認または拒否を問い合わせる
            PromptRequest request;
            request.type = QStringLiteral("license");
            request.data = buildLicensePromptData(QString::fromStdString(solv.name()),
                QString::fromStdString(solv.edition().asString()), QString::fromStdString(text));
            request.actions = {QStringLiteral("accept"), QStringLiteral("reject")};
            request.safeDefault = {{QStringLiteral("action"), QStringLiteral("reject")}};
            request.timeout = std::chrono::milliseconds(300000);
            const bool accepted = licenseAnswerAccepted(askCallback(request));

            // 回答待ち中のキャンセルでは、回答を候補状態へ反映しない
            if (m_cancelRequested.load()) {
                result.errorMessage = QStringLiteral("License confirmation cancelled; resolve and confirm again");
                return result;
            }
            if (accepted) {
                m_acceptedLicenses.insert(key);
                item.status().setLicenceConfirmed(true);
                ++result.acceptedCount;
            }
            else {
                // libzyppのSelectableを変更し、次回の依存関係解決で拒否候補を除外する
                const auto sel = zypp::ui::Selectable::get(solv.kind(), solv.name());
                const auto status = sel ? licenseRejectedStatus(sel->status()) : std::nullopt;
                if (!status || !sel->setStatus(*status)) {
                    result.errorMessage = QStringLiteral("Cannot exclude package %1 after its license was declined")
                                              .arg(QString::fromStdString(solv.name()));
                    return result;
                }
                result.changed = true;
                result.rejectedPackages.append(QString::fromStdString(solv.name()));
            }
        }
        result.rejectedPackages.removeDuplicates();
        result.success = true;
    }
    catch (const zypp::Exception &error) {
        // libzyppの失敗内容をD-Bus経由でGUIへ返し、確認済みとして扱わない
        m_lastError = error.asString();
        result.errorMessage = QString::fromStdString(m_lastError);
    }
    catch (const std::exception &) {
        // 予期しない例外も安全側に倒し、再解決と再確認を要求する
        result.errorMessage = QStringLiteral("License confirmation failed; resolve and confirm again");
    }
    return result;
}

} // namespace qZypper
