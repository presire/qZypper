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

    onOpened: {
        urlField.text = "https://"
        aliasField.text = ""
        urlField.forceActiveFocus()
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
                enabled: urlField.text.length > 8 && aliasField.text.length > 0
                onClicked: {
                    var ok = PackageController.addService(urlField.text, aliasField.text)
                    if (ok) {
                        root.serviceAdded()
                        root.close()
                    }
                }
            }
        }
    }
}
