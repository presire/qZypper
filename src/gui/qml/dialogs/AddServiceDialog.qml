import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.presire.qzypper.gui

Dialog {
    id: root
    title: qsTr("Add Service")
    modal: true
    width: 450
    height: 250
    anchors.centerIn: parent
    closePolicy: Dialog.CloseOnEscape
    standardButtons: Dialog.NoButton

    signal serviceAdded()

    property bool awaiting: false        // 追加要求の応答待ち
    property string errorMessage: ""     // 追加失敗時のエラー表示

    onOpened: {
        awaiting = false
        errorMessage = ""
        urlField.text = "https://"
        aliasField.text = ""
        urlField.forceActiveFocus()
    }

    Connections {
        target: PackageController
        function onAddServiceFinished(success) {
            if (!root.awaiting)
                return
            root.awaiting = false
            if (success) {
                root.serviceAdded()
                root.close()
            } else {
                root.errorMessage = qsTr("Failed to add service.")
            }
        }
    }

    contentItem: ColumnLayout {
        spacing: 12

        Label {
            text: qsTr("Enter the service URL and alias")
            font.pixelSize: 14
            font.bold: true
        }

        GridLayout {
            columns: 2
            columnSpacing: 12
            rowSpacing: 8
            Layout.fillWidth: true

            Label { text: qsTr("URL:") }
            TextField {
                id: urlField
                Layout.fillWidth: true
                placeholderText: "https://example.com/repo/repoindex.xml"
                selectByMouse: true
            }

            Label { text: qsTr("Alias:") }
            TextField {
                id: aliasField
                Layout.fillWidth: true
                placeholderText: qsTr("Unique identifier")
            }
        }

        Item { Layout.fillHeight: true }

        RowLayout {
            Layout.fillWidth: true
            Item { Layout.fillWidth: true }
            Button {
                text: qsTr("Cancel")
                onClicked: root.close()
            }
            Button {
                text: qsTr("Add")
                highlighted: true
                enabled: !root.awaiting && urlField.text.length > 8 && aliasField.text.length > 0
                onClicked: {
                    root.errorMessage = ""
                    root.awaiting = true
                    PackageController.addService(urlField.text, aliasField.text)
                }
            }
        }

        Label {
            visible: root.errorMessage.length > 0
            text: root.errorMessage
            textFormat: Text.PlainText
            color: palette.brightText
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
        }
    }
}
