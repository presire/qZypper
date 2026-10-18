import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtCore
import org.presire.qzypper.gui

    // -- 起動時ウェイトダイアログ --
    Dialog {
        id: startupDialog
        anchors.centerIn: parent
        modal: true
        closePolicy: Dialog.NoAutoClose
        standardButtons: Dialog.NoButton
        title: qsTr("Starting")

        ColumnLayout {
            spacing: 16

            Label {
                text: qsTr("Initializing. Please wait...")
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
            }

            BusyIndicator {
                running: true
                Layout.alignment: Qt.AlignHCenter
                Layout.preferredWidth: 48
                Layout.preferredHeight: 48
            }
        }
    }
