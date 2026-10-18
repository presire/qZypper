import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// ソルバーテストケースの保存先を表示する情報ダイアログ。
// パスは選択してコピーできる。
Dialog {
    id: testcaseDialog

    title: qsTr("Solver Testcase")
    anchors.centerIn: parent
    modal: true
    standardButtons: Dialog.Ok
    width: Math.min((parent ? parent.width : 600) * 0.7, 560)

    property string testcasePath: ""

    ColumnLayout {
        anchors.fill: parent
        spacing: 8

        Label {
            text: qsTr("The solver testcase has been saved to:")
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 6

            TextField {
                id: pathField
                Layout.fillWidth: true
                text: testcaseDialog.testcasePath
                readOnly: true
                selectByMouse: true
                activeFocusOnTab: true
                Accessible.name: qsTr("Testcase file path")
                Accessible.description: qsTr("Path of the saved solver testcase file")
            }

            Button {
                text: qsTr("Copy")
                activeFocusOnTab: true
                Accessible.name: qsTr("Copy path to clipboard")
                onClicked: {
                    pathField.selectAll()
                    pathField.copy()
                }
            }
        }
    }
}
