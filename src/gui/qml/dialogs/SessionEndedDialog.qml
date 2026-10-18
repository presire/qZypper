import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// 編集セッション解放 (ReleaseSession) 後に再開始を促すダイアログ。
Dialog {
    id: sessionEndedDialog
    anchors.centerIn: parent
    modal: true
    closePolicy: Dialog.NoAutoClose
    standardButtons: Dialog.NoButton
    width: 420
    title: qsTr("Session Ended")

    signal startSessionRequested()
    signal quitRequested()

    contentItem: ColumnLayout {
        spacing: 12

        Label {
            text: qsTr("The package editing session has ended. Start a new session to continue.")
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
        }
    }

    footer: Item {
        implicitHeight: 48

        RowLayout {
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            spacing: 8

            Button {
                text: qsTr("Start Session")
                highlighted: true
                onClicked: {
                    // 開始に失敗した場合に再試行できるよう、このダイアログは閉じない。
                    // 接続に成功した時点で Main.qml が閉じる
                    sessionEndedDialog.startSessionRequested()
                }
            }
            Button {
                text: qsTr("Quit")
                onClicked: {
                    sessionEndedDialog.close()
                    sessionEndedDialog.quitRequested()
                }
            }
        }
    }
}
