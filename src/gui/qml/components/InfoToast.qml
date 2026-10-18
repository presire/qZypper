import QtQuick
import QtQuick.Controls

// 情報通知用の短命トースト。PoolReloaded 等で利用する。
Rectangle {
    id: infoToast
    anchors.horizontalCenter: parent.horizontalCenter
    anchors.bottom: parent.bottom
    anchors.bottomMargin: 64
    implicitWidth: infoToastLabel.implicitWidth + 20
    implicitHeight: infoToastLabel.implicitHeight + 20
    color: palette.toolTipBase
    border.color: palette.mid
    radius: 4
    visible: false
    z: 10000

    function show(text) {
        infoToastLabel.text = text
        infoToast.visible = true
        infoToastTimer.restart()
    }

    Label {
        id: infoToastLabel
        anchors.centerIn: parent
        color: palette.toolTipText
        textFormat: Text.PlainText
    }

    Timer {
        id: infoToastTimer
        interval: 4000
        onTriggered: infoToast.visible = false
    }
}
