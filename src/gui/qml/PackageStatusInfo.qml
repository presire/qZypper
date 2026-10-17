pragma Singleton
import QtQuick

// パッケージステータス (src/common/Types.h の PackageStatus と 1:1) の定数・表示情報
QtObject {
    id: info

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
    readonly property int count: 10

    // 全ステータスを表示するマスク (既定値)
    readonly property int defaultMask: 0x3FF

    // フィルタパネルの行順 (Myrlyn 準拠)。内側の配列ごとに区切りを入れる
    readonly property var filterGroups: [
        [PackageStatusInfo.Install, PackageStatusInfo.Update, PackageStatusInfo.Del],
        [PackageStatusInfo.AutoInstall, PackageStatusInfo.AutoUpdate, PackageStatusInfo.AutoDel],
        [PackageStatusInfo.Taboo, PackageStatusInfo.Protected],
        [PackageStatusInfo.KeepInstalled, PackageStatusInfo.NoInst]
    ]

    // filterGroups を平坦化した行順
    readonly property var filterOrder: {
        var order = []
        for (var g = 0; g < filterGroups.length; ++g)
            order = order.concat(filterGroups[g])
        return order
    }

    // ステータスの表示名
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

    // ステータスアイコンのリソースパス
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

    // インストール済み、またはインストール/更新予定のステータスか (一覧のチェック状態)
    function isChecked(status) {
        return status === PackageStatusInfo.KeepInstalled
            || status === PackageStatusInfo.Install
            || status === PackageStatusInfo.AutoInstall
            || status === PackageStatusInfo.Update
            || status === PackageStatusInfo.AutoUpdate
            || status === PackageStatusInfo.Protected
    }

    // 削除予定のステータスか
    function isMarkedForRemoval(status) {
        return status === PackageStatusInfo.Del || status === PackageStatusInfo.AutoDel
    }

    // マスクにステータスが含まれるか
    function maskHas(mask, status) {
        return ((mask >> status) & 1) === 1
    }
}
