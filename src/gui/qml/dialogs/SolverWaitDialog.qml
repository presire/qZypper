import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.presire.qzypper.gui

// ソルバー実行中のウェイトダイアログ。PackageController.busy に連動して開閉する。
Dialog {
    id: solverWaitDialog
    anchors.centerIn: parent
    modal: true
    closePolicy: Dialog.NoAutoClose
    width: 320
    standardButtons: Dialog.NoButton
    title: qsTr("Resolving dependencies...")

    // 他ダイアログ (起動/コミット/コンフリクト/リフレッシュ) 表示中は開かないためのフラグ
    property bool blocked: false

    contentItem: RowLayout {
        spacing: 12

        BusyIndicator {
            Layout.preferredWidth: 32
            Layout.preferredHeight: 32
            running: solverWaitDialog.visible
        }

        Label {
            text: qsTr("Resolving dependencies...\nPlease wait.")
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
        }
    }

    Connections {
        target: PackageController
        function onBusyChanged() {
            if (PackageController.busy) {
                if (!solverWaitDialog.blocked)
                    solverWaitDialog.open()
            } else {
                solverWaitDialog.close()
            }
        }
    }
}
