import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.presire.qzypper.gui

// リポジトリリフレッシュ中のモーダルオーバーレイ。
Popup {
    id: refreshOverlay
    anchors.centerIn: parent
    width: parent ? parent.width : 800
    height: parent ? parent.height : 600
    modal: true
    closePolicy: Popup.NoAutoClose

    background: Rectangle {
        color: palette.window
        opacity: 0.85
    }

    property bool cancelling: false

    // Popup内のShortcutはモーダル中でもウィンドウレベルで動作する
    Shortcut {
        sequence: "Escape"
        enabled: refreshOverlay.visible && !refreshOverlay.cancelling
        onActivated: refreshOverlay.doCancel()
    }

    contentItem: Item {
        id: refreshOverlayRoot

        Column {
            anchors.centerIn: parent
            spacing: 16

            BusyIndicator {
                anchors.horizontalCenter: parent.horizontalCenter
                running: refreshOverlay.visible
            }

            Label {
                anchors.horizontalCenter: parent.horizontalCenter
                text: PackageController.statusMessage
                textFormat: Text.PlainText
                font.pixelSize: 16
            }

            Label {
                anchors.horizontalCenter: parent.horizontalCenter
                text: refreshOverlay.cancelling
                      ? qsTr("Will be cancelled after the current repository finishes")
                      : qsTr("Press Esc or button to cancel")
                opacity: 0.6
                font.pixelSize: 13
            }

            Button {
                anchors.horizontalCenter: parent.horizontalCenter
                text: qsTr("Cancel")
                visible: !refreshOverlay.cancelling
                onClicked: refreshOverlay.doCancel()
            }
        }
    }

    function doCancel() {
        cancelling = true
        PackageController.cancelRefresh()
    }

    onClosed: cancelling = false

    Connections {
        target: PackageController
        function onRefreshFinished(success) {
            refreshOverlay.close()
        }
    }
}
