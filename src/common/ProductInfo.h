/**
 * @file ProductInfo.h
 * @brief D-Busで転送する製品情報の共通型
 *
 * バックエンドのlibzypp製品情報をGUIへ渡すための値型
 * D-Busのメソッド境界を越えるためQVariantMapへの変換を提供する
 *
 * 入力: ZyppManagerがzypp::Productから構築した値
 * 出力: ProductInfoとそのQVariantMap表現
 * 依存: Qt Core
 */

#ifndef QZYPPER_COMMON_PRODUCTINFO_H
#define QZYPPER_COMMON_PRODUCTINFO_H

#include <QString>
#include <QVariantMap>
#include <QMetaType>

namespace qZypper {

/**
 * @struct ProductInfo
 * @brief プール内の製品1件を表す情報
 *
 * インストール済み製品ではインストール済みオブジェクトの内容を優先し、
 * 利用可能な候補はavailable、インストール済みバージョンはinstalledVersionで別に示す
 * isBaseはインストール済みオブジェクトがターゲットディストリビューションかを表す
 */
struct ProductInfo {
    QString name;              // 製品名
    QString shortName;         // 製品の短い表示名
    QString summary;           // 概要
    QString version;           // 表示対象オブジェクトのバージョン
    QString installedVersion;  // インストール済みバージョン (未インストール時は空)
    QString vendor;            // ベンダー
    QString arch;              // アーキテクチャ
    QString repoAlias;         // リポジトリエイリアス
    QString repoName;          // リポジトリ名
    int status = 0;            // パッケージ状態
    bool installed = false;    // インストール済みか
    bool available = false;    // 利用可能な候補があるか
    bool isBase = false;       // ベース製品 (ターゲットディストリビューション) か

    /**
     * @brief D-Bus転送用のQVariantMapへ変換する
     * @return 製品属性を持つマップ
     * @note 読み取り専用で、メンバーは変更しない
     */
    QVariantMap toVariantMap() const {
        return {
            {"name",             name},
            {"shortName",        shortName},
            {"summary",          summary},
            {"version",          version},
            {"installedVersion", installedVersion},
            {"vendor",           vendor},
            {"arch",             arch},
            {"repoAlias",        repoAlias},
            {"repoName",         repoName},
            {"status",           status},
            {"installed",        installed},
            {"available",        available},
            {"isBase",           isBase}
        };
    }
};

} // namespace qZypper

Q_DECLARE_METATYPE(qZypper::ProductInfo)

#endif // QZYPPER_COMMON_PRODUCTINFO_H
