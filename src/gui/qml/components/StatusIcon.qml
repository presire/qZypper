import QtQuick
import QtQuick.Controls
import org.presire.qzypper.gui

// パッケージステータスのアイコン (ホバーでステータス名をツールチップ表示)
Image {
    id: root

    property int status: PackageStatusInfo.NoInst
    property int iconSize: 16
    // ツールチップを表示するか (行全体にツールチップがある場合などは false にする)
    property bool showToolTip: true

    width: iconSize
    height: iconSize
    sourceSize.width: iconSize
    sourceSize.height: iconSize
    fillMode: Image.PreserveAspectFit
    smooth: true
    source: PackageStatusInfo.iconSource(status)

    Accessible.role: Accessible.Graphic
    Accessible.name: PackageStatusInfo.label(status)

    HoverHandler {
        id: hover
        enabled: root.showToolTip
    }

    ToolTip.visible: root.showToolTip && hover.hovered
    ToolTip.delay: 500
    ToolTip.text: PackageStatusInfo.label(root.status)
}
