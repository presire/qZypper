pragma Singleton
/**
 * @file PackageStatusInfo.qml
 * @brief パッケージステータスの定数と表示情報をまとめたシングルトン
 *
 * src/common/Types.hのPackageStatusと1:1で対応する数値をQML側から参照するための窓口になる
 * PackageTable、StatusFilterPanel、StatusIcon、Utilから参照される
 *
 * 入力 : なし
 * 出力 : なし (ステータス値ごとの表示名・アイコン・判定結果のみ)
 * 依存 : src/common/Types.h (PackageStatusの数値定義)
 */
import QtQuick

// ステータスの定数・表示名・アイコン・判定関数を提供するシングルトン本体
QtObject {
    id: info  // シングルトン自身の参照名

    // 数値はC++側のenumと一致させる必要があるため明示的に指定する
    enum Status {
        NoInst = 0,
        Install = 1,
        AutoInstall = 2,
        KeepInstalled = 3,
        Update = 4,
        AutoUpdate = 5,
        Del = 6,
        AutoDel = 7,
        Taboo = 8,
        Protected = 9
    }

    // ステータス数 (ビットマスクの幅)
    readonly property int count: 10           // フィルタ用ビットマスクのビット数
    readonly property int defaultMask: 0x3FF  // 全ステータスを表示する初期マスク

    // フィルタパネルのグループ定義 (行順はMyrlyn準拠)
    // 内側の配列ごとに区切りを入れる
    readonly property var filterGroups: [
        [PackageStatusInfo.Install, PackageStatusInfo.Update, PackageStatusInfo.Del],
        [PackageStatusInfo.AutoInstall, PackageStatusInfo.AutoUpdate, PackageStatusInfo.AutoDel],
        [PackageStatusInfo.Taboo, PackageStatusInfo.Protected],
        [PackageStatusInfo.KeepInstalled, PackageStatusInfo.NoInst]
    ]

    // filterGroupsを平坦化した行順
    // 現在はどこからも参照されていない (StatusFilterPanelはfilterGroupsを直接使用する)
    readonly property var filterOrder: {
        var order = []
        for (var g = 0; g < filterGroups.length; ++g) {
            order = order.concat(filterGroups[g])
        }
        return order
    }

    /**
     * @brief ステータス値に対応する表示名を返す
     * @param status PackageStatusの数値
     * @return qsTrで翻訳された表示名、未知の値なら空文字列
     */
    function label(status) {
        switch (status) {
            case PackageStatusInfo.NoInst:        return qsTr("Do Not Install")
            case PackageStatusInfo.Install:       return qsTr("Install")
            case PackageStatusInfo.AutoInstall:   return qsTr("Auto-Install")
            case PackageStatusInfo.KeepInstalled: return qsTr("Keep")
            case PackageStatusInfo.Update:        return qsTr("Update")
            case PackageStatusInfo.AutoUpdate:    return qsTr("Auto-Update")
            case PackageStatusInfo.Del:           return qsTr("Delete")
            case PackageStatusInfo.AutoDel:       return qsTr("Auto-Delete")
            case PackageStatusInfo.Taboo:         return qsTr("Taboo")
            case PackageStatusInfo.Protected:     return qsTr("Protected")
            default:                              return ""
        }
    }

    /**
     * @brief ステータス値に対応するアイコンのリソースURLを返す
     * @param status PackageStatusの数値
     * @return qrcスキームのSVGパス、未知の値なら空文字列
     */
    function iconSource(status) {
        var base = "qrc:/qt/qml/org/presire/qzypper/gui/icons/status/"
        switch (status) {
            case PackageStatusInfo.NoInst:        return base + "no-inst.svg"
            case PackageStatusInfo.Install:       return base + "install.svg"
            case PackageStatusInfo.AutoInstall:   return base + "auto-install.svg"
            case PackageStatusInfo.KeepInstalled: return base + "keep.svg"
            case PackageStatusInfo.Update:        return base + "update.svg"
            case PackageStatusInfo.AutoUpdate:    return base + "auto-update.svg"
            case PackageStatusInfo.Del:           return base + "delete.svg"
            case PackageStatusInfo.AutoDel:       return base + "auto-delete.svg"
            case PackageStatusInfo.Taboo:         return base + "taboo.svg"
            case PackageStatusInfo.Protected:     return base + "protected.svg"
            default:                              return ""
        }
    }

    /**
     * @brief 一覧のチェックボックスをONで表示するステータスかを判定する
     * @param status PackageStatusの数値
     * @return インストール済み、または、インストール/更新予定ならtrue
     */
    function isChecked(status) {
        return status === PackageStatusInfo.KeepInstalled
            || status === PackageStatusInfo.Install
            || status === PackageStatusInfo.AutoInstall
            || status === PackageStatusInfo.Update
            || status === PackageStatusInfo.AutoUpdate
            || status === PackageStatusInfo.Protected
    }

    /**
     * @brief 削除予定のステータスかを判定する
     * @param status PackageStatusの数値
     * @return DelまたはAutoDelならtrue
     */
    function isMarkedForRemoval(status) {
        return status === PackageStatusInfo.Del || status === PackageStatusInfo.AutoDel
    }

    /**
     * @brief フィルタ用ビットマスクに指定ステータスが含まれるかを判定する
     * @param mask ステータスビットの集合
     * @param status PackageStatusの数値
     * @return ビットが立っていればtrue
     */
    function maskHas(mask, status) {
        return ((mask >> status) & 1) === 1
    }
}
