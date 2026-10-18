import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtCore
import org.presire.qzypper.gui

    // 全パッケージ更新 結果通知ダイアログ
    Dialog {
        id: updateInfoDialog
        title: qsTr("Update All Packages")
        anchors.centerIn: parent
        modal: true
        standardButtons: Dialog.Ok
        width: 400

        property string message: ""

        Label {
            text: updateInfoDialog.message
            textFormat: Text.PlainText
            wrapMode: Text.WordWrap
            anchors.left: parent.left
            anchors.right: parent.right
        }
    }
