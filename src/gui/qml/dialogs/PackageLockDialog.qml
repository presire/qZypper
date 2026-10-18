import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Zypp ロック検出 (org.presire.qzypper.Error.ZyppLocked) 時のフレンドリーダイアログ。
// 競合している外部パッケージマネージャを閉じた後に再試行できる。
Dialog {
    id: lockDialog
    anchors.centerIn: parent
    modal: true
    width: 440
    title: qsTr("Package System Locked")

    property string lockerName: ""
    property int lockerPid: 0
    property string lockerMessage: ""

    signal retryRequested()

    contentItem: ColumnLayout {
        spacing: 12

        Label {
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            textFormat: Text.PlainText
            text: qsTr("Another package manager (%1, PID %2) is using the package system. Close it and retry.")
                  .arg(lockDialog.lockerName.length > 0 ? lockDialog.lockerName : qsTr("unknown"))
                  .arg(lockDialog.lockerPid)
        }

        Label {
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            visible: lockDialog.lockerMessage.length > 0
            opacity: 0.7
            font.pixelSize: 12
            textFormat: Text.PlainText
            text: lockDialog.lockerMessage
        }
    }

    footer: Item {
        implicitHeight: 48

        RowLayout {
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            spacing: 8

            Button {
                text: qsTr("Retry")
                highlighted: true
                onClicked: {
                    lockDialog.close()
                    lockDialog.retryRequested()
                }
            }
            Button {
                text: qsTr("Cancel")
                onClicked: lockDialog.close()
            }
        }
    }
}
