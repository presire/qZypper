import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.presire.qzypper.gui

// ディストリビューション更新の確認ダイアログ。
// ベンダー変更を許可するかを選んでから dist-upgrade を開始する。
Dialog {
    id: distUpgradeDialog

    title: qsTr("Distribution Upgrade")
    anchors.centerIn: parent
    modal: true
    standardButtons: Dialog.NoButton
    width: Math.min((parent ? parent.width : 600) * 0.7, 520)

    // 承認時に allowVendorChange を添えて通知
    signal confirmed(bool allowVendorChange)

    ColumnLayout {
        anchors.fill: parent
        spacing: 10

        Label {
            text: qsTr("A distribution upgrade resolves the entire system to the latest available versions. Packages may be installed, updated, removed, or switched to a different vendor.")
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
        }

        CheckBox {
            id: vendorChangeCheck
            text: qsTr("Allow vendor change")
            activeFocusOnTab: true
            Accessible.name: qsTr("Allow vendor change")
            Accessible.description: qsTr("Permit packages to switch to a different vendor during the upgrade")
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 8

            Item { Layout.fillWidth: true }

            Button {
                text: qsTr("Cancel")
                activeFocusOnTab: true
                Accessible.name: qsTr("Cancel")
                onClicked: distUpgradeDialog.reject()
            }

            Button {
                text: qsTr("Start Upgrade")
                highlighted: true
                enabled: !PackageController.busy
                activeFocusOnTab: true
                Accessible.name: qsTr("Start distribution upgrade")
                onClicked: {
                    var allowVendorChange = vendorChangeCheck.checked
                    distUpgradeDialog.accept()
                    distUpgradeDialog.confirmed(allowVendorChange)
                }
            }
        }
    }
}
