import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.presire.qzypper.gui

// ハンバーガーメニュー (ナビゲーションDrawer)。
Drawer {
    id: navDrawer
    width: 300
    height: parent ? parent.height : 600
    edge: Qt.LeftEdge

    // 各操作は Main 側で処理するためシグナルで通知する。
    signal updateAllRequested()
    signal applyChangesRequested()
    signal manageReposRequested()
    signal refreshReposRequested()
    signal aboutqZypperRequested()
    signal aboutQtRequested()
    signal endSessionRequested()
    signal distUpgradeRequested()
    signal packageLocksRequested()
    signal historyRequested()
    signal verifySystemRequested()
    signal installLocalRpmsRequested()

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // ヘッダ
        Pane {
            Layout.fillWidth: true
            padding: 16
            background: Rectangle { color: palette.highlight }

            Label {
                text: "qZypper"
                font.pixelSize: 18
                font.bold: true
                color: palette.highlightedText
            }
        }

        // -- パッケージセクション --
        Label {
            text: qsTr("Packages")
            font.bold: true
            font.pixelSize: 11
            color: palette.placeholderText
            Layout.fillWidth: true
            Layout.topMargin: 12
            Layout.leftMargin: 16
            Layout.bottomMargin: 4
        }

        ItemDelegate {
            text: qsTr("Update All Packages")
            icon.name: "system-software-update"
            icon.color: palette.text
            Layout.fillWidth: true
            enabled: !PackageController.busy
            onClicked: {
                navDrawer.close()
                navDrawer.updateAllRequested()
            }
        }
        ItemDelegate {
            text: qsTr("Distribution Upgrade")
            icon.name: "package-upgrade"
            icon.color: palette.text
            Layout.fillWidth: true
            enabled: !PackageController.busy
            Accessible.name: qsTr("Distribution Upgrade")
            Accessible.description: qsTr("Upgrade the whole distribution, optionally allowing vendor changes")
            onClicked: {
                navDrawer.close()
                navDrawer.distUpgradeRequested()
            }
        }
        ItemDelegate {
            text: qsTr("Apply Changes")
            icon.name: "dialog-ok-apply"
            icon.color: palette.text
            Layout.fillWidth: true
            enabled: !PackageController.busy && PackageController.hasPendingChanges
            onClicked: {
                navDrawer.close()
                navDrawer.applyChangesRequested()
            }
        }

        MenuSeparator { Layout.fillWidth: true }

        // -- 設定セクション --
        Label {
            text: qsTr("Settings")
            font.bold: true
            font.pixelSize: 11
            color: palette.placeholderText
            Layout.fillWidth: true
            Layout.topMargin: 8
            Layout.leftMargin: 16
            Layout.bottomMargin: 4
        }

        ItemDelegate {
            text: qsTr("Manage Repositories")
            icon.name: "repository"
            icon.color: palette.text
            Layout.fillWidth: true
            onClicked: {
                navDrawer.close()
                navDrawer.manageReposRequested()
            }
        }

        ItemDelegate {
            text: qsTr("Refresh Repositories")
            icon.name: "view-refresh"
            icon.color: palette.text
            Layout.fillWidth: true
            enabled: !PackageController.busy
            onClicked: {
                navDrawer.close()
                navDrawer.refreshReposRequested()
            }
        }

        ItemDelegate {
            text: qsTr("Install Local RPM Files...")
            icon.name: "application-x-rpm"
            icon.color: palette.text
            Layout.fillWidth: true
            enabled: !PackageController.busy
            Accessible.name: qsTr("Install Local RPM Files")
            Accessible.description: qsTr("Select RPM files on this computer and install them with dependency resolution")
            onClicked: {
                navDrawer.close()
                navDrawer.installLocalRpmsRequested()
            }
        }

        ItemDelegate {
            text: qsTr("Package Locks...")
            icon.name: "changes-prevent"
            icon.color: palette.text
            Layout.fillWidth: true
            Accessible.name: qsTr("Package Locks")
            Accessible.description: qsTr("View and manage persistent package locks")
            onClicked: {
                navDrawer.close()
                navDrawer.packageLocksRequested()
            }
        }

        ItemDelegate {
            text: qsTr("Installation History...")
            icon.name: "document-open-recent"
            icon.color: palette.text
            Layout.fillWidth: true
            Accessible.name: qsTr("Installation History")
            Accessible.description: qsTr("Browse the libzypp history of package transactions")
            onClicked: {
                navDrawer.close()
                navDrawer.historyRequested()
            }
        }

        ItemDelegate {
            text: qsTr("Verify System")
            icon.name: "dialog-ok"
            icon.color: palette.text
            Layout.fillWidth: true
            enabled: !PackageController.busy
            Accessible.name: qsTr("Verify System")
            Accessible.description: qsTr("Verify system integrity and repair changed or missing files")
            onClicked: {
                navDrawer.close()
                navDrawer.verifySystemRequested()
            }
        }

        MenuSeparator { Layout.fillWidth: true }

        // -- ヘルプセクション --
        Label {
            text: qsTr("Help")
            font.bold: true
            font.pixelSize: 11
            color: palette.placeholderText
            Layout.fillWidth: true
            Layout.topMargin: 8
            Layout.leftMargin: 16
            Layout.bottomMargin: 4
        }

        ItemDelegate {
            text: qsTr("About qZypper")
            icon.name: "help-about"
            icon.color: palette.text
            Layout.fillWidth: true
            onClicked: {
                navDrawer.close()
                navDrawer.aboutqZypperRequested()
            }
        }

        ItemDelegate {
            text: qsTr("About Qt")
            icon.name: "qt"
            icon.color: palette.text
            Layout.fillWidth: true
            onClicked: {
                navDrawer.close()
                navDrawer.aboutQtRequested()
            }
        }

        Item { Layout.fillHeight: true }

        // -- 終了 --
        MenuSeparator { Layout.fillWidth: true }

        ItemDelegate {
            text: qsTr("End Editing Session")
            icon.name: "process-stop"
            icon.color: palette.text
            Layout.fillWidth: true
            enabled: PackageController.initialized
            onClicked: {
                navDrawer.close()
                navDrawer.endSessionRequested()
            }
        }

        ItemDelegate {
            text: qsTr("Quit")
            icon.name: "application-exit"
            icon.color: palette.text
            Layout.fillWidth: true
            onClicked: Qt.quit()
        }
    }
}
