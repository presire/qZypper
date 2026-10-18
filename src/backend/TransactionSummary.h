#ifndef QZYPPER_BACKEND_TRANSACTIONSUMMARY_H
#define QZYPPER_BACKEND_TRANSACTIONSUMMARY_H

#include <QString>
#include <QVariantMap>

#include <functional>
#include <vector>

namespace qZypper {

/**
 * @brief 1 パッケージの遷移前後情報 (純粋データ)。
 *
 * 旧 (インストール済み) と新 (導入予定) の有無および
 * バージョン・ベンダ・アーキテクチャを保持する。
 */
struct ChangeTuple {
    bool hasOld = false;      ///< 旧オブジェクトが存在するか
    bool hasNew = false;      ///< 新オブジェクトが存在するか
    QString oldVersion;       ///< 旧バージョン
    QString oldVendor;        ///< 旧ベンダ
    QString oldArch;          ///< 旧アーキテクチャ
    QString newVersion;       ///< 新バージョン
    QString newVendor;        ///< 新ベンダ
    QString newArch;          ///< 新アーキテクチャ
};

/**
 * @brief 遷移の危険種別。
 */
struct ChangeRisk {
    bool install = false;       ///< 新規インストール
    bool remove = false;        ///< 削除
    bool update = false;        ///< 更新 (同種別・非ダウングレード)
    bool downgrade = false;     ///< ダウングレード
    bool vendorChange = false;  ///< ベンダ変更
    bool archChange = false;    ///< アーキテクチャ変更
};

/**
 * @brief バージョン比較関数の型。
 *
 * 戻り値は旧 < 新 で負、等しければ 0、旧 > 新 で正。
 */
using VersionCompareFn = std::function<int(const QString &oldVersion,
                                           const QString &newVersion)>;

/**
 * @brief 遷移前後情報から危険種別を分類する (純粋関数)。
 * @param tuple 遷移前後情報
 * @param cmp バージョン比較関数
 * @return 分類結果
 */
ChangeRisk classifyChange(const ChangeTuple &tuple, const VersionCompareFn &cmp);

/**
 * @brief サマリに載せる 1 変更分の詳細。
 */
struct ChangeEntry {
    QString name;         ///< パッケージ名
    QString oldVersion;   ///< 旧バージョン
    QString newVersion;   ///< 新バージョン
    QString oldVendor;    ///< 旧ベンダ
    QString newVendor;    ///< 新ベンダ
    QString oldArch;      ///< 旧アーキテクチャ
    QString newArch;      ///< 新アーキテクチャ
    QString repoAlias;    ///< 導入元 (または削除元) リポジトリ
    ChangeRisk risk;      ///< 危険種別
};

/**
 * @brief トランザクションの集計値。
 */
struct TransactionTally {
    int installCount = 0;         ///< 新規インストール件数
    int updateCount = 0;          ///< 更新件数
    int removeCount = 0;          ///< 削除件数
    int downgradeCount = 0;       ///< ダウングレード件数
    int vendorChangeCount = 0;    ///< ベンダ変更件数
    int archChangeCount = 0;      ///< アーキテクチャ変更件数
    qlonglong downloadSize = 0;   ///< ダウンロード合計サイズ (バイト)
    qlonglong installedSizeChange = 0; ///< インストールサイズ変化量 (バイト)
    std::vector<ChangeEntry> removals;      ///< 削除されるパッケージ
    std::vector<ChangeEntry> downgrades;    ///< ダウングレードされるパッケージ
    std::vector<ChangeEntry> vendorChanges; ///< ベンダが変わるパッケージ
    std::vector<ChangeEntry> archChanges;   ///< アーキテクチャが変わるパッケージ
};

/**
 * @brief 1 変更を集計へ加算する (純粋関数)。
 * @param tally 集計先
 * @param entry 加算する変更
 */
void accumulateChange(TransactionTally &tally, const ChangeEntry &entry);

/**
 * @brief 集計値を D-Bus 転送用の a{sv} へ変換する。
 * @param tally 集計値
 * @return サマリマップ
 */
QVariantMap tallyToVariantMap(const TransactionTally &tally);

/**
 * @brief 1 変更を D-Bus 転送用の a{sv} へ変換する。
 * @param entry 変更
 * @return 変更マップ
 */
QVariantMap changeEntryToVariantMap(const ChangeEntry &entry);

} // namespace qZypper

#endif // QZYPPER_BACKEND_TRANSACTIONSUMMARY_H
